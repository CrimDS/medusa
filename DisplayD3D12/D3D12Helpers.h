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
#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "Debug/Trace.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

//----------------------------------------------------------------------------

#define SAFE_RELEASE(p)		do { if (p) { (p)->Release(); (p) = nullptr; } } while(0)
#define FRAME_COUNT			2
#define NUM_SHADOW_CASCADES	4

//----------------------------------------------------------------------------

// Logs the HRESULT to OutputDebugString; does NOT actually throw.  Callers
// must still check the HRESULT and handle the failure path themselves.
inline void LogIfFailed(HRESULT hr, const char * msg = nullptr)
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
// Create an UPLOAD-heap committed buffer of dataSize bytes and (optionally)
// memcpy pData into it.  Returned in GENERIC_READ state, ready to bind as a
// vertex/index/constant buffer.
//
// Static vertex/index buffers currently use this for simplicity; ideally
// long-lived geometry would live in DEFAULT-heap memory with a one-shot
// upload+barrier, but that requires lifetime/synchronization plumbing the
// primitive types don't have yet.  See [[project_threading_status]] / the
// renderer review punch list — task #10 (HIGH, pervasive).
inline ComPtr<ID3D12Resource> CreateUploadBuffer( ID3D12Device * pDevice, const void * pData, UINT dataSize )
{
	if ( !pDevice || dataSize == 0 )
		return nullptr;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resDesc.Width = dataSize;
	resDesc.Height = 1;
	resDesc.DepthOrArraySize = 1;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ComPtr<ID3D12Resource> buffer;
	HRESULT hr = pDevice->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE,
		&resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer) );
	if ( FAILED(hr) )
		return nullptr;

	if ( pData )
	{
		void * pMapped = nullptr;
		D3D12_RANGE readRange = { 0, 0 };
		buffer->Map( 0, &readRange, &pMapped );
		if ( pMapped )
		{
			memcpy( pMapped, pData, dataSize );
			buffer->Unmap( 0, nullptr );
		}
	}

	return buffer;
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

//----------------------------------------------------------------------------
// sRGB-correct pipeline (Chunk 1 of the lighting upgrade plan).
//
// When true, the renderer treats colour textures as sRGB-encoded (decoded
// to linear at sample), runs all lighting math in linear space, and gamma-
// encodes at the final blit via an sRGB swap-chain RTV view.  Constant-
// buffer colour values authored as sRGB (material diffuse / specular /
// ambient / emissive, light diffuse / specular, global ambient) must be
// linearised at upload time so they meet the linear texture samples in
// the same colour space.
//
// Flipping back to false restores the legacy gamma-wrong behaviour as a
// single-flag escape hatch — useful for A/B comparison.  Defined in
// DisplayDeviceD3D12.cpp.
extern bool g_bSRGBPipeline;

// Light-intensity scale (Chunk 1 follow-up).  Multiplied into vLightDiffuse
// and vLightSpecular at CB upload to compensate for the loss of "apparent
// light energy" that comes from linearising authored sRGB light colours.
// Background: a warm-sunlight light authored at (1.0, 0.9, 0.8) sRGB
// becomes (1.0, 0.776, 0.604) in linear space — physically correct but
// ~25-30% less energy reaching surfaces than the legacy gamma-wrong path
// delivered.  Auto-exposure lifts everything globally, but lit faces vs
// dark space need different brightness; this knob scales lit surfaces
// only, leaving ambient and unlit areas alone.  Tunable: drop toward 1.0
// for less lift, push toward 2.5 for very strong directional lighting.
extern float g_fLightIntensityScale;

// IEC 61966-2-1 sRGB → linear (per channel).  Piecewise — matches what the
// hardware does when sampling an sRGB-typed SRV, so author-time sRGB CB
// colours decode the same way texture samples do.  A no-op when the sRGB
// pipeline is disabled; safe to call at every upload site without a guard
// at the call site.
inline float srgbToLinear(float x)
{
	if (!g_bSRGBPipeline)
		return x;
	return (x <= 0.04045f) ? (x / 12.92f) : powf((x + 0.055f) / 1.055f, 2.4f);
}

// RGB-only conversion: linearise xyz, leave w (alpha) alone.  Alpha is
// already a linear quantity (opacity, not a perceptual colour value).
inline ShaderFloat4 srgbColorToLinear(const ShaderFloat4 & c)
{
	return ShaderFloat4(srgbToLinear(c.x), srgbToLinear(c.y), srgbToLinear(c.z), c.w);
}

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
	float			fShadowDepthRange;	// far - near of shadow map projection (world units)
	ShaderFloat4	vShadowFocus;		// xyz = world-space shadow focus position, w = unused

	// Chunk 4 — diffuse IBL via Spherical Harmonics.  9 L=2 coefficients
	// (one per band:  L00, L1-1, L10, L11, L2-2, L2-1, L20, L21, L22).
	// .xyz = RGB coefficient, .w = unused/padding.  Computed CPU-side from
	// procedural environment (UNIVERSE_AMBIENT sky + directional sun) and
	// evaluated in the shader via Ramamoorthi 2001 to give directional
	// ambient fill in place of the legacy hemisphere term.
	ShaderFloat4	vSHCoefs[9];

	// Chunk 4.5 — celestial occluders for sun shadow-casting.  xyz = world
	// position, w = radius (world units).  nNumOccluders gives count; up
	// to 32 (DisplayDevice::MAX_OCCLUDERS).  The shader does ray-sphere
	// tests from each lit pixel toward the sun's direction and zeroes
	// directional lighting if any sphere is hit.  LimbGlow uses the same
	// occluder list (re-packed as tangent-plane discs in its own CB) for
	// the per-pixel rim halo test.
	ShaderFloat4	vOccluders[32];
	int				nNumOccluders;
	int				nShadowPCFTaps;	// per-frame from DisplayDevice::sm_nShaderDetail (LOW=4, MED=8, HIGH=16, EXTREME=32)
	int				pad_occ1;
	int				pad_occ2;

	// Chunk 5 — primary directional sun.  vSunDir.xyz is the direction the
	// LIGHT POINTS (i.e. away from the sun, matching addDirectionalLight
	// convention); direction TOWARD the sun is -vSunDir.xyz.  vSunDir.w is
	// 1.0 when a directional light is bound for this frame, 0.0 otherwise
	// (use as a "sun present" gate in shaders that have a non-sun fallback).
	// vSunColor.xyz is the sun's linear RGB; .w unused.  Filled by
	// DisplayDeviceD3D12::updateCBPerFrame from the same m_Lights scan that
	// drives computeDiffuseSH.  Consumed by Planet.hlsl for day/night and
	// atmosphere rim, available to any future shader that wants the sun.
	ShaderFloat4	vSunDir;
	ShaderFloat4	vSunColor;

	// Chunk 5.5 — the visible sun's world position (NOT the directional
	// light's hint vector; vSunDir is an art-directable axis that won't
	// agree with where the sun is drawn).  Populated from
	// DisplayDevice::m_vSunWorldPos which is itself set by
	// NounStar::render via submitSunCandidate (the closest submitted
	// candidate wins).  vSunWorldPos.w = 1 when a sun was submitted this
	// frame, 0 otherwise; shaders that want geometry-correct sun direction
	// (Planet.hlsl) compute toSun = normalize(vSunWorldPos.xyz - worldPos)
	// when w==1, fall back to vSunDir-based direction otherwise.
	ShaderFloat4	vSunWorldPos;
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

	// PBR (metal-rough).  Top-level scalars from Material::m_Roughness/
	// m_Metallic/m_AO; per-texel ORM map (slot t3) multiplies these when
	// bEnableORM is set by ORMMAP surface execute.  bEnablePBR is set
	// CPU-side from PrimitiveMaterialD3D12 when m_sShader names PBR.hlsl
	// (or any future PBR-mode shader) — drives shader behaviour, doesn't
	// reach into the IL or root signature.
	float			fMatRoughness;
	float			fMatMetallic;
	float			fMatAO;
	int				bEnablePBR;

	int				bEnableORMMap;		// t3 — packed AO/Roughness/Metallic
	int				bEnableNormalMap;	// t4 — tangent-space PBR normal
	int				bEnableSpecIBL;		// device-owned t5 (BRDF LUT) + t6 (env cube)
	int				bFlipNormalY;		// 1 = DirectX convention (-Y), 0 = OpenGL (+Y, Substance default)
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
	ShaderMatrix	mCascadeViewProj[NUM_SHADOW_CASCADES];	// combined view*proj per cascade
	ShaderFloat4	vCascadeSplits;	// cascade split distances (world units from shadow focus)
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
		m_FreeList.clear();
		return true;
	}

	UINT Allocate()
	{
		if (!m_FreeList.empty())
		{
			UINT index = m_FreeList.back();
			m_FreeList.pop_back();
			return index;
		}
		if (m_NumAllocated >= m_NumDescriptors)
			return UINT(-1);
		return m_NumAllocated++;
	}

	void Free(UINT index)
	{
		if (index != UINT(-1) && index < m_NumDescriptors)
			m_FreeList.push_back(index);
	}

	void Reset()
	{
		m_NumAllocated = 0;
		m_FreeList.clear();
	}

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
	std::vector<UINT>				m_FreeList;
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
		m_CurrentOffset.store(0, std::memory_order_relaxed);

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

	// Single-threaded reset; called at frame start.  Relaxed because there
	// are no concurrent allocators at this point (frame boundary barrier).
	void Reset() { m_CurrentOffset.store(0, std::memory_order_relaxed); }

	// Allocate space and return GPU virtual address + CPU pointer
	struct Allocation
	{
		D3D12_GPU_VIRTUAL_ADDRESS	gpuAddress;
		void *						cpuAddress;
		UINT						offset;
	};

	// Thread-safe via compare-exchange loop on m_CurrentOffset.  Multiple
	// workers can call concurrently; each gets a non-overlapping slice.
	// Wrap-on-exhaust is intact: when a candidate alignedOffset+size would
	// overrun the buffer we restart from 0.  This is safe because by the
	// time a frame's ring buffer is being reused (post-Reset()), the GPU
	// has finished the prior frame's draws via the frame fence.  Concurrent
	// wrappers each propose their own alignedOffset/newOffset and only the
	// CAS winner commits; losers retry against the updated value.
	Allocation Allocate(UINT size, UINT alignment = 256)
	{
		// Reject allocations that exceed the entire buffer
		if (size > m_BufferSize)
		{
			OutputDebugStringA("UploadRingBuffer: allocation exceeds buffer size!\n");
			Allocation alloc = {};
			return alloc;
		}

		UINT current = m_CurrentOffset.load(std::memory_order_relaxed);
		UINT alignedOffset;
		UINT newOffset;
		bool wrapped = false;
		for (;;)
		{
			alignedOffset = (current + alignment - 1) & ~(alignment - 1);
			wrapped = false;
			if (alignedOffset + size > m_BufferSize)
			{
				alignedOffset = 0;	// wrap around
				wrapped = true;
			}
			newOffset = alignedOffset + size;

			// Acquire on success so any subsequent reads of the buffer (which
			// don't happen — CPU writes via mapped pointer — but kept for
			// future-proofing) synchronise-with prior allocators.
			if (m_CurrentOffset.compare_exchange_weak(
					current, newOffset,
					std::memory_order_acq_rel, std::memory_order_relaxed))
				break;
			// CAS failure refreshes `current`; retry with the new value.
		}

		// One-time warning if wrap actually fires.  Within-frame wrap of the
		// upload ring corrupts earlier-recorded draws' dynamic vertex/CB data
		// because the GPU reads the ring at command-list execute time.
		if (wrapped)
		{
			static bool s_wrapWarned = false;
			if (!s_wrapWarned)
			{
				s_wrapWarned = true;
				TRACE( "WARN: UploadRingBuffer wrap at offset=%u size=%u bufSize=%u. "
					"Within-frame wrap = VB/CB data corruption.",
					current, size, m_BufferSize );
			}
		}

		Allocation alloc;
		alloc.offset = alignedOffset;
		alloc.cpuAddress = m_pMappedData + alignedOffset;
		alloc.gpuAddress = m_Buffer->GetGPUVirtualAddress() + alignedOffset;
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
	std::atomic<UINT>		m_CurrentOffset;	// thread-safe via Allocate's CAS loop
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
		if (inputLayout != other.inputLayout) return inputLayout < other.inputLayout;
		if (topology != other.topology) return topology < other.topology;
		if (blendMode != other.blendMode) return blendMode < other.blendMode;
		if (doubleSided != other.doubleSided) return doubleSided < other.doubleSided;
		if (depthWrite != other.depthWrite) return depthWrite < other.depthWrite;
		if (depthEnable != other.depthEnable) return depthEnable < other.depthEnable;
		if (wireframe != other.wireframe) return wireframe < other.wireframe;
		if (rtvFormat != other.rtvFormat) return rtvFormat < other.rtvFormat;
		if (dsvFormat != other.dsvFormat) return dsvFormat < other.dsvFormat;
		if (sampleCount != other.sampleCount) return sampleCount < other.sampleCount;
		if (vsBytecode != other.vsBytecode) return vsBytecode < other.vsBytecode;
		return psBytecode < other.psBytecode;
	}

	bool operator==(const PSOKey & other) const
	{
		return inputLayout == other.inputLayout && topology == other.topology
			&& blendMode == other.blendMode && doubleSided == other.doubleSided
			&& depthWrite == other.depthWrite && depthEnable == other.depthEnable
			&& wireframe == other.wireframe && rtvFormat == other.rtvFormat
			&& dsvFormat == other.dsvFormat && sampleCount == other.sampleCount
			&& vsBytecode == other.vsBytecode && psBytecode == other.psBytecode;
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
