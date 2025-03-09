// ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// Two pass separable 2D filter.
// Only use small kernels (3x3, 5x5) here to avoid overhead.
// For larger kernels, consider calling filter1D twice instead
// of using a combined filter2D.
//
// For 1D separable filter, see
// https://www.gamedev.net/forums/topic/681645-blur-compute-shader/
// ===== ===== ===== ===== ===== ===== ===== ===== ===== =====

#include "../ShaderSharedStructs.h"

#define g_RootSignature \
    "RootFlags(0), " \
    "RootConstants(b0, num32BitConstants = 4), " \
    "CBV( b1 ), " \
    "DescriptorTable( SRV(t0, numDescriptors = 1) )," \
    "DescriptorTable( UAV(u0, numDescriptors = 1) )"

ConstantBuffer<BlurConstants> g_cb : register(b0);
ConstantBuffer<BlurKernel> g_kernel : register(b1);
Texture2D<float4> g_input : register(t0);
RWTexture2D<float4> g_output : register(u0);

// Blur buffer (including extra piexls outside group boundries)
// Blur N N pixels, needs N + 2 * BlurRadius for Blur radius.
// We need 2 caches, with the first one storing loaded data to
// reduce bandwidth and the second one storing the horizontal
// pass result.
groupshared float4 g_cache_load[FILTER_N_THREADS + 2 * MAX_KERNEL_RADUIS][FILTER_N_THREADS + 2 * MAX_KERNEL_RADUIS];
groupshared float4 g_cache_filter[FILTER_N_THREADS][FILTER_N_THREADS + 2 * MAX_KERNEL_RADUIS];

// Same-padding
float4 LoadPixel(int coordX, int coordY)
{
    int x = min(max(coordX, 0), g_cb.srcWidth - 1);
    int y = min(max(coordY, 0), g_cb.srcHeight - 1);
    return g_input.Load(int3(x, y, g_cb.srcMipLevel));
}

void LoadDataToSharedMemory(int2 groupID, int2 threadID)
{
    // Fill group storage.
	// Threads near edge of group will sample extra pixels.
    
    // X-axis extra sampling.
    if (groupID.x < g_cb.blurRadius)
    {
        g_cache_load[groupID.x][groupID.y + g_cb.blurRadius] = LoadPixel(threadID.x - g_cb.blurRadius, threadID.y);
    }
    else if (groupID.x >= FILTER_N_THREADS - g_cb.blurRadius)
    {
        g_cache_load[groupID.x + 2 * g_cb.blurRadius][groupID.y + g_cb.blurRadius] = LoadPixel(threadID.x + g_cb.blurRadius, threadID.y);
    }
    
    // Y-axis extra sampling.
    if (groupID.y < g_cb.blurRadius)
    {
        g_cache_load[groupID.x + g_cb.blurRadius][groupID.y] = LoadPixel(threadID.x, threadID.y - g_cb.blurRadius);
    }
    else if (groupID.y >= FILTER_N_THREADS - g_cb.blurRadius)
    {
        g_cache_load[groupID.x + g_cb.blurRadius][groupID.y + 2 * g_cb.blurRadius] = LoadPixel(threadID.x, threadID.y + g_cb.blurRadius);
    }
    
    // Corner extra sampling.
    if (groupID.x < g_cb.blurRadius && groupID.y < g_cb.blurRadius)
    {
        g_cache_load[groupID.x][groupID.y] = LoadPixel(threadID.x - g_cb.blurRadius, threadID.y - g_cb.blurRadius);
    }
    else if (groupID.x >= FILTER_N_THREADS - g_cb.blurRadius && groupID.y < g_cb.blurRadius)
    {
        g_cache_load[groupID.x + 2 * g_cb.blurRadius][groupID.y] = LoadPixel(threadID.x + g_cb.blurRadius, threadID.y - g_cb.blurRadius);
    }
    else if (groupID.x < g_cb.blurRadius && groupID.y >= FILTER_N_THREADS - g_cb.blurRadius)
    {
        g_cache_load[groupID.x][groupID.y + 2 * g_cb.blurRadius] = LoadPixel(threadID.x - g_cb.blurRadius, threadID.y + g_cb.blurRadius);
    }
    else if (groupID.x >= FILTER_N_THREADS - g_cb.blurRadius && groupID.y >= FILTER_N_THREADS - g_cb.blurRadius)
    {
        g_cache_load[groupID.x + 2 * g_cb.blurRadius][groupID.y + 2 * g_cb.blurRadius] = LoadPixel(threadID.x + g_cb.blurRadius, threadID.y + g_cb.blurRadius);
    }

	// Sample its own pixel.
    g_cache_load[groupID.x + g_cb.blurRadius][groupID.y + g_cb.blurRadius] = LoadPixel(threadID.x, threadID.y);
}

// Horizontal pass.
// This is expected to be called before vertical pass.
void FilterHorizontal(int2 groupID)
{
    float4 filteredColor;
    
    // Threads near top and bottom edges of group will filter extra rows.
    if (groupID.y < g_cb.blurRadius)
    {
        filteredColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    
        for (int i = -g_cb.blurRadius; i <= g_cb.blurRadius; ++i)
        {
            int k = groupID.x + g_cb.blurRadius + i;
            filteredColor += g_kernel.w[i + g_cb.blurRadius] * g_cache_load[k][groupID.y];
        }
    
        g_cache_filter[groupID.x][groupID.y] = filteredColor;
    }
    else if (groupID.y >= FILTER_N_THREADS - g_cb.blurRadius)
    {
        filteredColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    
        for (int i = -g_cb.blurRadius; i <= g_cb.blurRadius; ++i)
        {
            int k = groupID.x + g_cb.blurRadius + i;
            filteredColor += g_kernel.w[i + g_cb.blurRadius] * g_cache_load[k][groupID.y + 2 * g_cb.blurRadius];
        }
    
        g_cache_filter[groupID.x][groupID.y + 2 * g_cb.blurRadius] = filteredColor;
    }
    
    // Filter at its own position.
    filteredColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    
    for (int i = -g_cb.blurRadius; i <= g_cb.blurRadius; ++i)
    {
        int k = groupID.x + g_cb.blurRadius + i;
        filteredColor += g_kernel.w[i + g_cb.blurRadius] * g_cache_load[k][groupID.y + g_cb.blurRadius];
    }
    
    g_cache_filter[groupID.x][groupID.y + g_cb.blurRadius] = filteredColor;
}

// Vertical pass.
// This is expected to be called after horizontal pass.
float4 FilterVertical(int2 groupID)
{
    float4 filteredColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    
    for (int i = -g_cb.blurRadius; i <= g_cb.blurRadius; ++i)
    {
        int k = groupID.y + g_cb.blurRadius + i;
        filteredColor += g_kernel.w[i + g_cb.blurRadius] * g_cache_filter[groupID.x][k];
    }
    
    return filteredColor;
}

[RootSignature(g_RootSignature)]
[numthreads(FILTER_N_THREADS, FILTER_N_THREADS, 1)]
void CSMain(int3 groupID : SV_GroupThreadID, int3 threadID : SV_DispatchThreadID)
{
    if (g_cb.blurRadius == 0)
    {
        if (threadID.x < g_cb.srcWidth && threadID.y < g_cb.srcHeight)
        {
            g_output[threadID.xy] = LoadPixel(threadID.x, threadID.y);
        }
        return;
    }
    
    // Load data to shared memory (g_cache_load).
    LoadDataToSharedMemory(groupID.xy, threadID.xy);

    // Wait for all threads to finish loading.
    GroupMemoryBarrierWithGroupSync();

    // Horizontal pass.
    // Write to g_cache_filter.
    FilterHorizontal(groupID.xy);
    
    GroupMemoryBarrierWithGroupSync();

    // Vertical pass.
    // Get final filtered color.
    float4 finalColor = FilterVertical(groupID.xy);

    // Write to output.
    if (threadID.x < g_cb.srcWidth && threadID.y < g_cb.srcHeight)
    {
        g_output[threadID.xy] = finalColor;
    }
}