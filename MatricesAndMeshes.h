#pragma once

#include "SMesh.h"
#include "HelperFunctions.h"

#include <dxgi1_6.h>

const DirectX::XMMATRIX CubeViewTransforms[6] =
{
	DirectX::XMMatrixLookToLH(
		DirectX::XMVectorZero(),
		DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),
		DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)),

	DirectX::XMMatrixLookToLH(
		DirectX::XMVectorZero(),
		DirectX::XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f),
		DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)),

	DirectX::XMMatrixLookToLH(
		DirectX::XMVectorZero(),
		DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
		DirectX::XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f)),

	DirectX::XMMatrixLookToLH(
		DirectX::XMVectorZero(),
		DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f),
		DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)),

	DirectX::XMMatrixLookToLH(
		DirectX::XMVectorZero(),
		DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
		DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)),

	DirectX::XMMatrixLookToLH(
		DirectX::XMVectorZero(),
		DirectX::XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f),
		DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)),
};

const DirectX::XMMATRIX CubeProjectionTransform = DirectX::XMMatrixOrthographicLH(2.0f, 2.0f, 0.1f, 10.0f);

// Large triengle for screen presentation
const std::vector<SVertex> PresentVertices =
{
	// 1 triangle that fills the entire render target.
	SVertex{ { -1.0f, -3.0f, 0.0f }, { 0.0f, 2.0f }, {0.0f, 0.0f, 1.0f} },    // Bottom left
	SVertex{ { -1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f }, {0.0f, 0.0f, 1.0f} },    // Top left
	SVertex{ { 3.0f, 1.0f, 0.0f }, { 2.0f, 0.0f }, {0.0f, 0.0f, 1.0f} },    // Top right
};

const std::vector<UINT32> PresentIndicies = { 0, 1, 2 };

// Simple square
const std::vector<SVertex> SquareVertices =
{
	SVertex{ { -0.25f, 0.25f, 0.0f }, { 0.0f, 1.0f }, {0.0f, 0.0f, -1.0f} }, // top left
	SVertex{ { 0.25f, 0.25f, 0.0f }, { 1.0f, 1.0f }, {0.0f, 0.0f, -1.0f} }, // top right
	SVertex{ { 0.25f, -0.25f, 0.0f }, { 1.0f, 0.0f }, {0.0f, 0.0f, -1.0f} }, // bottom right
	SVertex{ { -0.25f, 0.25f, 0.0f }, { 0.0f, 1.0f }, {0.0f, 0.0f, -1.0f} }, // top left
	SVertex{ { 0.25f, -0.25f, 0.0f }, { 1.0f, 0.0f }, {0.0f, 0.0f, -1.0f} }, // bottom right
	SVertex{ { -0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f }, {0.0f, 0.0f, -1.0f} } // bottom left
};

const std::vector<UINT32> SquareIndicies = {
	0, 1, 2, // first triangle
	3, 4, 5  // second triangle
};

// StaticMesh_Cube
const std::vector<SVertex> CubeVertices =
{
	// plane x = -1
	SVertex{ { -1.0f, -1.0f, +1.0f }, { 0.0f, 1.0f }, {-1.0f, 0.0f, 0.0f} },
	SVertex{ { -1.0f, +1.0f, +1.0f }, { 1.0f, 1.0f }, {-1.0f, 0.0f, 0.0f} },
	SVertex{ { -1.0f, +1.0f, -1.0f }, { 1.0f, 0.0f }, {-1.0f, 0.0f, 0.0f} },
	SVertex{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f }, {-1.0f, 0.0f, 0.0f} },

	// plane x = +1
	SVertex{ { +1.0f, -1.0f, +1.0f }, { 0.0f, 1.0f }, {+1.0f, 0.0f, 0.0f} },
	SVertex{ { +1.0f, +1.0f, +1.0f }, { 1.0f, 1.0f }, {+1.0f, 0.0f, 0.0f} },
	SVertex{ { +1.0f, +1.0f, -1.0f }, { 1.0f, 0.0f }, {+1.0f, 0.0f, 0.0f} },
	SVertex{ { +1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f }, {+1.0f, 0.0f, 0.0f} },

	// plane y = -1
	SVertex{ { -1.0f, -1.0f, +1.0f }, { 0.0f, 1.0f }, {0.0f, -1.0f, 0.0f} },
	SVertex{ { +1.0f, -1.0f, +1.0f }, { 1.0f, 1.0f }, {0.0f, -1.0f, 0.0f} },
	SVertex{ { +1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f }, {0.0f, -1.0f, 0.0f} },
	SVertex{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f }, {0.0f, -1.0f, 0.0f} },

	// plane y = +1
	SVertex{ { -1.0f, +1.0f, +1.0f }, { 0.0f, 1.0f }, {0.0f, +1.0f, 0.0f} },
	SVertex{ { +1.0f, +1.0f, +1.0f }, { 1.0f, 1.0f }, {0.0f, +1.0f, 0.0f} },
	SVertex{ { +1.0f, +1.0f, -1.0f }, { 1.0f, 0.0f }, {0.0f, +1.0f, 0.0f} },
	SVertex{ { -1.0f, +1.0f, -1.0f }, { 0.0f, 0.0f }, {0.0f, +1.0f, 0.0f} },

	// plane z = -1
	SVertex{ { -1.0f, +1.0f, -1.0f }, { 0.0f, 1.0f }, {0.0f, 0.0f, -1.0f} },
	SVertex{ { +1.0f, +1.0f, -1.0f }, { 1.0f, 1.0f }, {0.0f, 0.0f, -1.0f} },
	SVertex{ { +1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f }, {0.0f, 0.0f, -1.0f} },
	SVertex{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f }, {0.0f, 0.0f, -1.0f} },

	// plane z = +1
	SVertex{ { -1.0f, +1.0f, +1.0f }, { 0.0f, 1.0f }, {0.0f, 0.0f, +1.0f} },
	SVertex{ { +1.0f, +1.0f, +1.0f }, { 1.0f, 1.0f }, {0.0f, 0.0f, +1.0f} },
	SVertex{ { +1.0f, -1.0f, +1.0f }, { 1.0f, 0.0f }, {0.0f, 0.0f, +1.0f} },
	SVertex{ { -1.0f, -1.0f, +1.0f }, { 0.0f, 0.0f }, {0.0f, 0.0f, +1.0f} },
};

const std::vector<UINT32> CubeIndicies = {
	2, 1, 0, 3, 2, 0,
	4, 5, 6, 4, 6, 7,
	8, 9, 10, 8, 10, 11,
	14, 13, 12, 15, 14, 12,
	18, 17, 16, 19, 18, 16,
	20, 21, 22, 20, 22, 23,
};

// StaticMesh_CubeFacingInside
const std::vector<SVertex> CubeInVertices =
{
	// plane x = -1
	SVertex{ { -1.0f, -1.0f, +1.0f }, { 0.0f, 1.0f }, {+1.0f, 0.0f, 0.0f} },
	SVertex{ { -1.0f, +1.0f, +1.0f }, { 1.0f, 1.0f }, {+1.0f, 0.0f, 0.0f} },
	SVertex{ { -1.0f, +1.0f, -1.0f }, { 1.0f, 0.0f }, {+1.0f, 0.0f, 0.0f} },
	SVertex{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f }, {+1.0f, 0.0f, 0.0f} },

	// plane x = +1
	SVertex{ { +1.0f, -1.0f, +1.0f }, { 0.0f, 1.0f }, {-1.0f, 0.0f, 0.0f} },
	SVertex{ { +1.0f, +1.0f, +1.0f }, { 1.0f, 1.0f }, {-1.0f, 0.0f, 0.0f} },
	SVertex{ { +1.0f, +1.0f, -1.0f }, { 1.0f, 0.0f }, {-1.0f, 0.0f, 0.0f} },
	SVertex{ { +1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f }, {-1.0f, 0.0f, 0.0f} },

	// plane y = -1
	SVertex{ { -1.0f, -1.0f, +1.0f }, { 0.0f, 1.0f }, {0.0f, +1.0f, 0.0f} },
	SVertex{ { +1.0f, -1.0f, +1.0f }, { 1.0f, 1.0f }, {0.0f, +1.0f, 0.0f} },
	SVertex{ { +1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f }, {0.0f, +1.0f, 0.0f} },
	SVertex{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f }, {0.0f, +1.0f, 0.0f} },

	// plane y = +1
	SVertex{ { -1.0f, +1.0f, +1.0f }, { 0.0f, 1.0f }, {0.0f, -1.0f, 0.0f} },
	SVertex{ { +1.0f, +1.0f, +1.0f }, { 1.0f, 1.0f }, {0.0f, -1.0f, 0.0f} },
	SVertex{ { +1.0f, +1.0f, -1.0f }, { 1.0f, 0.0f }, {0.0f, -1.0f, 0.0f} },
	SVertex{ { -1.0f, +1.0f, -1.0f }, { 0.0f, 0.0f }, {0.0f, -1.0f, 0.0f} },

	// plane z = -1
	SVertex{ { -1.0f, +1.0f, -1.0f }, { 0.0f, 1.0f }, {0.0f, 0.0f, +1.0f} },
	SVertex{ { +1.0f, +1.0f, -1.0f }, { 1.0f, 1.0f }, {0.0f, 0.0f, +1.0f} },
	SVertex{ { +1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f }, {0.0f, 0.0f, +1.0f} },
	SVertex{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f }, {0.0f, 0.0f, +1.0f} },

	// plane z = +1
	SVertex{ { -1.0f, +1.0f, +1.0f }, { 0.0f, 1.0f }, {0.0f, 0.0f, -1.0f} },
	SVertex{ { +1.0f, +1.0f, +1.0f }, { 1.0f, 1.0f }, {0.0f, 0.0f, -1.0f} },
	SVertex{ { +1.0f, -1.0f, +1.0f }, { 1.0f, 0.0f }, {0.0f, 0.0f, -1.0f} },
	SVertex{ { -1.0f, -1.0f, +1.0f }, { 0.0f, 0.0f }, {0.0f, 0.0f, -1.0f} },
};

const std::vector<UINT32> CubeInIndicies = {
	0, 1, 2, 0, 2, 3,
	6, 5, 4, 7, 6, 4,
	10, 9, 8, 11, 10, 8,
	12, 13, 14, 12, 14, 15,
	16, 17, 18, 16, 18, 19,
	22, 21, 20, 23, 22, 20,
};