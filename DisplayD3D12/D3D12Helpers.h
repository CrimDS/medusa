/*
	D3D12Helpers.h
	DirectX 12 helper utilities and types for the Medusa Engine
	(c)2024 Palestar
*/

#ifndef D3D12_HELPERS_H
#define D3D12_HELPERS_H

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

//----------------------------------------------------------------------------

#define SAFE_RELEASE(p)		do { if (p) { (p)->Release(); (p) = nullptr; } } while(0)
#define FRAME_COUNT			2

//----------------------------------------------------------------------------

inline void ThrowIfFailed(HRESULT hr, const char * msg = nullptr)
{
	if (FAILED(hr))
	{
		char buf[256];
		if (msg)
			sprintf_s(buf, "D3D12 Error: %s (HRESULT=0x%08X)", msg, hr);
		else
			sprintf_s(buf, "D3D12 Error: HRESULT=0x%08X", hr);
		OutputDebugStringA(buf);
	}
}

//----------------------------------------------------------------------------
// Constant buffer alignment helper
inline UINT AlignCB(UINT size)
{
	return (size + 255) & ~255;
}

//----------------------------------------------------------------------------
// XMFLOAT4X4 matrix types used as shader constants (replaces D3DXMATRIX)

struct alignas(16) ShaderMatrix
{
	XMFLOAT4X4 m;

	ShaderMatrix() { XMStoreFloat4x4(&m, XMMatrixIdentity()); }
	ShaderMatrix(const XMMATRIX & mat) { XMStoreFloat4x4(&m, XMMatrixTranspose(mat)); }
	operator XMMATRIX() const { return XMLoadFloat4x4(&m); }
};

struct ShaderFloat4
{
	float x, y, z, w;
	ShaderFloat4() : x(0), y(0), z(0), w(0) {}
	ShaderFloat4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

struct ShaderFloat3
{
	float x, y, z;
	ShaderFloat3() : x(0), y(0), z(0) {}
	ShaderFloat3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};

struct ShaderFloat2
{
	float x, y;
	ShaderFloat2() : x(0), y(0) {}
	ShaderFloat2(float _x, float _y) : x(_x), y(_y) {}
};

//----------------------------------------------------------------------------
// Constant buffer structures matching the HLSL shader cbuffer layout

struct CBPerFrame
{
	ShaderMatrix	mView;
	ShaderMatrix	mProj;
	ShaderMatrix	mProjOrtho;		// ortho matrix for screen-space (TL) vertices
	ShaderFloat4	vCameraPos;		// xyz = pos, w = unused
	ShaderFloat4	vGlobalAmbient;
	ShaderFloat2	szShadowMap;
	float			fShadowDistance;
	float			pad0;
};

struct CBPerObject
{
	ShaderMatrix	mWorld;
};

struct CBPerMaterial
{
	ShaderFloat4	vMatDiffuse;
	ShaderFloat4	vMatSpecular;
	ShaderFloat4	vMatAmbient;
	ShaderFloat4	vMatEmissive;
	float			fMatSpecularPower;
	int				bEnableDiffuse;
	int				bEnableLightMap;
	int				bEnableBumpMap;
	float			fBumpDepth;
	int				bEnableShadowMap;
	int				bEnableAmbient;
	float			pad0;
};

struct CBPerLight
{
	int				nLightType;
	float			pad0[3];
	ShaderFloat4	vLightDiffuse;
	ShaderFloat4	vLightSpecular;
	ShaderFloat4	vLightAmbient;	// xyz = ambient, w = unused
	ShaderFloat4	vLightPosition;	// xyz = pos, w = unused
	ShaderFloat4	vLightDirection;// xyz = dir, w = unused
	ShaderFloat4	vAttenuation;	// xyz = att, w = unused
	ShaderFloat4	vSpot;			// xyz = spot params, w = unused
	ShaderMatrix	mLightView;
	ShaderMatrix	mLightProj;
};

//----------------------------------------------------------------------------
// Descriptor heap wrapper

class DescriptorHeap
{
public:
	DescriptorHeap() : m_DescriptorSize(0), m_NumDescriptors(0), m_NumAllocated(0) {}

	bool Create(ID3D12Device * pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors, bool shaderVisible = false)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = numDescriptors;
		desc.Type = type;
		desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		HRESULT hr = pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_Heap));
		if (FAILED(hr))
			return false;

		m_DescriptorSize = pDevice->GetDescriptorHandleIncrementSize(type);
		m_NumDescriptors = numDescriptors;
		m_NumAllocated = 0;
		return true;
	}

	UINT Allocate()
	{
		if (m_NumAllocated >= m_NumDescriptors)
			return UINT(-1);
		return m_NumAllocated++;
	}

	void Reset() { m_NumAllocated = 0; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(UINT index) const
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle = m_Heap->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += (SIZE_T)index * m_DescriptorSize;
		return handle;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(UINT index) const
	{
		D3D12_GPU_DESCRIPTOR_HANDLE handle = m_Heap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += (UINT64)index * m_DescriptorSize;
		return handle;
	}

	ID3D12DescriptorHeap * Get() const { return m_Heap.Get(); }
	UINT GetDescriptorSize() const { return m_DescriptorSize; }
	UINT GetNumAllocated() const { return m_NumAllocated; }

private:
	ComPtr<ID3D12DescriptorHeap>	m_Heap;
	UINT							m_DescriptorSize;
	UINT							m_NumDescriptors;
	UINT							m_NumAllocated;
};

//----------------------------------------------------------------------------
// Upload ring buffer for dynamic vertex/index data and constant buffers

class UploadRingBuffer
{
public:
	UploadRingBuffer() : m_pMappedData(nullptr), m_BufferSize(0), m_CurrentOffset(0) {}

	bool Create(ID3D12Device * pDevice, UINT bufferSize)
	{
		m_BufferSize = bufferSize;
		m_CurrentOffset = 0;

		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Width = bufferSize;
		resDesc.Height = 1;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels = 1;
		resDesc.SampleDesc.Count = 1;
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		HRESULT hr = pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
			&resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_Buffer));
		if (FAILED(hr))
			return false;

		D3D12_RANGE readRange = { 0, 0 };
		hr = m_Buffer->Map(0, &readRange, (void **)&m_pMappedData);
		if (FAILED(hr))
			return false;

		return true;
	}

	void Reset() { m_CurrentOffset = 0; }

	// Allocate space and return GPU virtual address + CPU pointer
	struct Allocation
	{
		D3D12_GPU_VIRTUAL_ADDRESS	gpuAddress;
		void *						cpuAddress;
		UINT						offset;
	};

	Allocation Allocate(UINT size, UINT alignment = 256)
	{
		UINT alignedOffset = (m_CurrentOffset + alignment - 1) & ~(alignment - 1);
		if (alignedOffset + size > m_BufferSize)
		{
			// wrap around
			alignedOffset = 0;
		}

		Allocation alloc;
		alloc.offset = alignedOffset;
		alloc.cpuAddress = m_pMappedData + alignedOffset;
		alloc.gpuAddress = m_Buffer->GetGPUVirtualAddress() + alignedOffset;
		m_CurrentOffset = alignedOffset + size;
		return alloc;
	}

	// Allocate and copy data in one step
	Allocation AllocateAndCopy(const void * pData, UINT size, UINT alignment = 4)
	{
		Allocation alloc = Allocate(size, alignment);
		memcpy(alloc.cpuAddress, pData, size);
		return alloc;
	}

	ID3D12Resource * GetResource() const { return m_Buffer.Get(); }
	D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const { return m_Buffer ? m_Buffer->GetGPUVirtualAddress() : 0; }

	void Release()
	{
		if (m_Buffer && m_pMappedData)
		{
			m_Buffer->Unmap(0, nullptr);
			m_pMappedData = nullptr;
		}
		m_Buffer.Reset();
	}

private:
	ComPtr<ID3D12Resource>	m_Buffer;
	byte *					m_pMappedData;
	UINT					m_BufferSize;
	UINT					m_CurrentOffset;
};

//----------------------------------------------------------------------------
// Pipeline State Object cache key

struct PSOKey
{
	enum InputLayoutType { IL_VERTEX = 0, IL_VERTEXL = 1, IL_VERTEXTL = 2 };
	enum TopologyType { TOPO_TRIANGLE = 0, TOPO_LINE = 1 };

	InputLayoutType		inputLayout;
	TopologyType		topology;
	UINT				blendMode;		// 0=none, 1=alpha, 2=alpha_inv, 3=additive, 4=additive_inv
	bool				doubleSided;
	bool				depthWrite;
	bool				depthEnable;
	bool				wireframe;
	DXGI_FORMAT			rtvFormat;
	DXGI_FORMAT			dsvFormat;
	UINT				sampleCount;
	const void *		vsBytecode;		// VS blob pointer — discriminates different shaders
	const void *		psBytecode;		// PS blob pointer — discriminates different shaders

	bool operator<(const PSOKey & other) const
	{
		return memcmp(this, &other, sizeof(PSOKey)) < 0;
	}

	bool operator==(const PSOKey & other) const
	{
		return memcmp(this, &other, sizeof(PSOKey)) == 0;
	}
};

//----------------------------------------------------------------------------
// Resource barrier helper

inline void TransitionResource(ID3D12GraphicsCommandList * pCmdList,
	ID3D12Resource * pResource,
	D3D12_RESOURCE_STATES before,
	D3D12_RESOURCE_STATES after)
{
	if (before == after)
		return;

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = pResource;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	pCmdList->ResourceBarrier(1, &barrier);
}

//----------------------------------------------------------------------------

#endif

//----------------------------------------------------------------------------
// EOF
