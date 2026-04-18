/**
	@file ShaderD3D12.cpp
	@brief DirectX 12 shader wrapper implementation

	(c)2024 Palestar
*/

#define MEDUSA_TRACE_ON

#include "ShaderD3D12.h"
#include "DisplayDeviceD3D12.h"
#include "D3D12Helpers.h"

#include "Debug/Trace.h"
#include "File/FileDisk.h"
#include "Display/DisplayDevice.h"
#include "Reflection/TypeTemplate.h"
#include "Standard/Size.h"
#include "Math/Vector3.h"
#include "Math/Matrix33.h"
#include "Standard/Color.h"

#include <DirectXMath.h>

#ifndef ENABLE_SHADER_DEBUGGING
#define ENABLE_SHADER_DEBUGGING		0
#endif

//---------------------------------------------------------------------------------------------------

IMPLEMENT_LIGHT_FACTORY( ShaderD3D12, Widget );

//---------------------------------------------------------------------------------------------------

struct ShaderEntry
{
	const char *		pEntryPoint;
	const char *		pTarget;
};

static ShaderEntry VERTEX_SHADER_ENTRIES[] =
{
	{ "vs_main",		"vs_5_1"	},
	{ "vs_main30",		"vs_5_1"	},
	{ "vs_main20",		"vs_5_1"	},
};

static ShaderEntry PIXEL_SHADER_ENTRIES[] =
{
	{ "ps_main",		"ps_5_1"	},
	{ "ps_main30",		"ps_5_1"	},
	{ "ps_main20",		"ps_5_1"	},
};

//---------------------------------------------------------------------------------------------------

ShaderD3D12::ShaderD3D12() :
	m_bValid( false ),
	m_bReleased( false ),
	m_nShaderFileTime( 0 ),
	m_nVSConstantBufferSize( 0 ),
	m_nPSConstantBufferSize( 0 )
{}

ShaderD3D12::~ShaderD3D12()
{
	release();
}

//---------------------------------------------------------------------------------------------------

bool ShaderD3D12::compileShader( const wchar_t * pFilePath, const char * pEntryPoint,
	const char * pTarget, ComPtr<ID3DBlob> & pBlobOut )
{
	UINT compileFlags = 0;
#if defined(_DEBUG)
	compileFlags |= D3DCOMPILE_DEBUG;
#endif
#if ENABLE_SHADER_DEBUGGING
	compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	ComPtr<ID3DBlob> pErrors;
	HRESULT hr = D3DCompileFromFile( pFilePath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		pEntryPoint, pTarget, compileFlags, 0, &pBlobOut, &pErrors );

	if ( FAILED(hr) )
	{
		if ( pErrors )
		{
			const char * pError = (const char *)pErrors->GetBufferPointer();
			// ignore entry point not found errors - we try multiple entry points
			if ( strstr( pError, "entrypoint not found" ) == nullptr
				&& strstr( pError, "entry point" ) == nullptr )
			{
				TRACE( CharString().format( "Shader compile error (%s/%s): %s", pEntryPoint, pTarget, pError ) );
			}
		}
		return false;
	}

	return true;
}

//---------------------------------------------------------------------------------------------------

void ShaderD3D12::reflectConstantLayout( ID3DBlob * pBlob, ConstantLayoutMap & layoutOut )
{
	if ( !pBlob )
		return;

	ComPtr<ID3D12ShaderReflection> pReflection;
	HRESULT hr = D3DReflect( pBlob->GetBufferPointer(), pBlob->GetBufferSize(),
		IID_ID3D12ShaderReflection, (void **)&pReflection );
	if ( FAILED(hr) || !pReflection )
		return;

	D3D12_SHADER_DESC shaderDesc = {};
	pReflection->GetDesc( &shaderDesc );

	for ( UINT cb = 0; cb < shaderDesc.ConstantBuffers; ++cb )
	{
		ID3D12ShaderReflectionConstantBuffer * pCB = pReflection->GetConstantBufferByIndex( cb );
		if ( !pCB )
			continue;

		D3D12_SHADER_BUFFER_DESC cbDesc = {};
		pCB->GetDesc( &cbDesc );

		for ( UINT v = 0; v < cbDesc.Variables; ++v )
		{
			ID3D12ShaderReflectionVariable * pVar = pCB->GetVariableByIndex( v );
			if ( !pVar )
				continue;

			D3D12_SHADER_VARIABLE_DESC varDesc = {};
			pVar->GetDesc( &varDesc );

			ConstantInfo info;
			info.name = varDesc.Name;
			info.offset = varDesc.StartOffset;
			info.size = varDesc.Size;

			layoutOut[ varDesc.Name ] = info;
		}
	}
}

//---------------------------------------------------------------------------------------------------

bool ShaderD3D12::load( DisplayDeviceD3D12 * pDevice, const char * pShaderName )
{
	release();

	m_sShaderName = pShaderName;
	m_bReleased = false;

	// First try to find an HLSL version in the DisplayD3D12/Shaders/ directory
	CharString sHLSLPath;
	CharString sOriginalPath = DisplayDevice::sm_sShadersPath + m_sShaderName;

	// Construct the D3D12 shader path: look for <shaderDir>/<basename>.hlsl alongside the .fx files.
	// Strip any directory prefix to get just the filename, then swap extension to .hlsl.
	CharString sD3D12ShadersPath;
	{
		CharString sBaseName = m_sShaderName;

		// Keep only the filename portion (strip leading dir like "Shaders/")
		int nLastSlash = sBaseName.reverseFind( '/' );
		int nLastBack  = sBaseName.reverseFind( '\\' );
		int nSep = nLastSlash > nLastBack ? nLastSlash : nLastBack;
		CharString sDir;
		if ( nSep >= 0 )
		{
			sDir = sBaseName;
			sDir.left( nSep + 1 );								// e.g. "Shaders/"
			sBaseName.right( sBaseName.length() - nSep - 1 );
		}

		// Strip extension and re-append .hlsl
		int nDot = sBaseName.reverseFind( '.' );
		if ( nDot >= 0 )
			sBaseName.left( nDot );

		sD3D12ShadersPath = DisplayDevice::sm_sShadersPath + sDir + sBaseName + ".hlsl";
	}

	// Try D3D12 HLSL path first, then fall back to original .fx path
	if ( FileDisk::fileDate( sD3D12ShadersPath ) != 0 )
	{
		m_sFullPath = sD3D12ShadersPath;
	}
	else if ( FileDisk::fileDate( sOriginalPath ) != 0 )
	{
		m_sFullPath = sOriginalPath;
	}
	else
	{
		TRACE( CharString().format( "Failed to load shader %s, file not found!", (const char *)m_sShaderName ) );
		return false;
	}

	m_nShaderFileTime = FileDisk::fileDate( m_sFullPath );

	TRACE( "ShaderD3D12 loading from path: %s", (const char *)m_sFullPath );

	// Convert path to wide string for D3DCompileFromFile
	wchar_t wszPath[MAX_PATH];
	MultiByteToWideChar( CP_ACP, 0, m_sFullPath, -1, wszPath, MAX_PATH );

	// Compile vertex shader - try entry points in order
	bool bVSCompiled = false;
	for ( int i = 0; i < sizeof(VERTEX_SHADER_ENTRIES) / sizeof(VERTEX_SHADER_ENTRIES[0]); ++i )
	{
		if ( compileShader( wszPath, VERTEX_SHADER_ENTRIES[i].pEntryPoint,
			VERTEX_SHADER_ENTRIES[i].pTarget, m_pVSBlob ) )
		{
			bVSCompiled = true;
			break;
		}
	}

	// Compile pixel shader - try entry points in order
	bool bPSCompiled = false;
	for ( int i = 0; i < sizeof(PIXEL_SHADER_ENTRIES) / sizeof(PIXEL_SHADER_ENTRIES[0]); ++i )
	{
		if ( compileShader( wszPath, PIXEL_SHADER_ENTRIES[i].pEntryPoint,
			PIXEL_SHADER_ENTRIES[i].pTarget, m_pPSBlob ) )
		{
			bPSCompiled = true;
			break;
		}
	}

	// Reflect constant buffer layouts from compiled shaders
	if ( m_pVSBlob )
	{
		reflectConstantLayout( m_pVSBlob.Get(), m_VSConstantLayout );
		// Determine VS constant buffer size from reflection
		m_nVSConstantBufferSize = 0;
		for ( ConstantLayoutMap::iterator it = m_VSConstantLayout.begin(); it != m_VSConstantLayout.end(); ++it )
		{
			UINT nEnd = it->second.offset + it->second.size;
			if ( nEnd > m_nVSConstantBufferSize )
				m_nVSConstantBufferSize = nEnd;
		}
	}

	if ( m_pPSBlob )
	{
		reflectConstantLayout( m_pPSBlob.Get(), m_PSConstantLayout );
		// Determine PS constant buffer size from reflection
		m_nPSConstantBufferSize = 0;
		for ( ConstantLayoutMap::iterator it = m_PSConstantLayout.begin(); it != m_PSConstantLayout.end(); ++it )
		{
			UINT nEnd = it->second.offset + it->second.size;
			if ( nEnd > m_nPSConstantBufferSize )
				m_nPSConstantBufferSize = nEnd;
		}
	}

	if ( bVSCompiled || bPSCompiled )
	{
		m_bValid = true;
		TRACE( CharString().format( "ShaderD3D12 '%s' loaded (VS:%s PS:%s).",
			(const char *)m_sShaderName,
			bVSCompiled ? "yes" : "no",
			bPSCompiled ? "yes" : "no" ) );
	}
	else
	{
		m_bValid = false;
		TRACE( CharString().format( "ERROR: Failed to load shader '%s'.", (const char *)m_sShaderName ) );
	}

	return m_bValid;
}

//---------------------------------------------------------------------------------------------------

void ShaderD3D12::release()
{
	m_pVSBlob.Reset();
	m_pPSBlob.Reset();
	m_VSConstantLayout.clear();
	m_PSConstantLayout.clear();
	m_nVSConstantBufferSize = 0;
	m_nPSConstantBufferSize = 0;

	m_bValid = false;
	m_bReleased = true;
	m_nShaderFileTime = 0;
}

//---------------------------------------------------------------------------------------------------

void ShaderD3D12::setConstant( const char * pConstant, const Value & value )
{
	m_Constants[ pConstant ] = value;
}

void ShaderD3D12::clearConstant( const char * pConstant )
{
	m_Constants.erase( pConstant );
}

//---------------------------------------------------------------------------------------------------

bool ShaderD3D12::needsReload() const
{
	if ( !DisplayDevice::sm_bEnableShaderDebug )
		return false;
	if ( m_sFullPath.length() == 0 )
		return false;
	return m_nShaderFileTime != FileDisk::fileDate( m_sFullPath );
}

//---------------------------------------------------------------------------------------------------

bool ShaderD3D12::apply()
{
	if ( !m_bValid )
		return false;
	return true;
}

//---------------------------------------------------------------------------------------------------

void ShaderD3D12::applyConstants( void * pCBData, UINT cbSize )
{
	if ( !pCBData || cbSize == 0 )
		return;

	// Zero out the constant buffer first
	memset( pCBData, 0, cbSize );

	// Merge both VS and PS constant layouts - write values for any constant found in either layout
	// Build a combined layout for lookup
	ConstantLayoutMap combinedLayout;
	for ( ConstantLayoutMap::iterator it = m_VSConstantLayout.begin(); it != m_VSConstantLayout.end(); ++it )
		combinedLayout[ it->first ] = it->second;
	for ( ConstantLayoutMap::iterator it = m_PSConstantLayout.begin(); it != m_PSConstantLayout.end(); ++it )
	{
		// PS layout may overlap or extend, use the one with the larger offset for combined buffer
		if ( combinedLayout.find( it->first ) == combinedLayout.end() )
			combinedLayout[ it->first ] = it->second;
	}

	// Apply each constant from our map
	for ( ConstantMap::iterator iConstant = m_Constants.begin();
		iConstant != m_Constants.end(); ++iConstant )
	{
		ConstantLayoutMap::iterator iLayout = combinedLayout.find( iConstant->first );
		if ( iLayout == combinedLayout.end() )
			continue;		// constant not found in shader, skip

		applyConstant( pCBData, cbSize, iLayout->second, iConstant->second );
	}
}

//---------------------------------------------------------------------------------------------------

void ShaderD3D12::applyConstant( void * pCBData, UINT cbSize,
	const ConstantInfo & info, const Value & value )
{
	static Type * TYPE_INT = TypeTemplate<int>::instance();
	static Type * TYPE_FLOAT = TypeTemplate<float>::instance();
	static Type * TYPE_DOUBLE = TypeTemplate<double>::instance();
	static Type * TYPE_BOOL = TypeTemplate<bool>::instance();
	static Type * TYPE_SIZEINT = TypeTemplate<SizeInt>::instance();
	static Type * TYPE_SIZEFLT = TypeTemplate<SizeFloat>::instance();
	static Type * TYPE_VECTOR3 = TypeTemplate<Vector3>::instance();
	static Type * TYPE_MATRIX33 = TypeTemplate<Matrix33>::instance();
	static Type * TYPE_COLOR = TypeTemplate<Color>::instance();
	static Type * TYPE_SHADERMATRIX = TypeTemplate<ShaderMatrix>::instance();
	static Type * TYPE_SHADERFLOAT4 = TypeTemplate<ShaderFloat4>::instance();
	static Type * TYPE_SHADERFLOAT3 = TypeTemplate<ShaderFloat3>::instance();
	static Type * TYPE_XMFLOAT4X4 = TypeTemplate<XMFLOAT4X4>::instance();
	static Type * TYPE_XMFLOAT4 = TypeTemplate<XMFLOAT4>::instance();
	static Type * TYPE_XMFLOAT3 = TypeTemplate<XMFLOAT3>::instance();

	if ( !pCBData || !value.data() )
		return;

	// Ensure we don't write beyond the constant buffer bounds
	if ( info.offset + info.size > cbSize )
		return;

	byte * pDst = (byte *)pCBData + info.offset;

	if ( value.type() == TYPE_INT )
	{
		int nValue = (int)value;
		UINT nCopy = (info.size < sizeof(int)) ? info.size : sizeof(int);
		memcpy( pDst, &nValue, nCopy );
	}
	else if ( value.type() == TYPE_FLOAT )
	{
		float fValue = (float)value;
		UINT nCopy = (info.size < sizeof(float)) ? info.size : sizeof(float);
		memcpy( pDst, &fValue, nCopy );
	}
	else if ( value.type() == TYPE_DOUBLE )
	{
		float fValue = (float)(double)value;
		UINT nCopy = (info.size < sizeof(float)) ? info.size : sizeof(float);
		memcpy( pDst, &fValue, nCopy );
	}
	else if ( value.type() == TYPE_BOOL )
	{
		int nBool = (bool)value ? 1 : 0;
		UINT nCopy = (info.size < sizeof(int)) ? info.size : sizeof(int);
		memcpy( pDst, &nBool, nCopy );
	}
	else if ( value.type() == TYPE_XMFLOAT4X4 || value.type() == TYPE_SHADERMATRIX )
	{
		UINT nCopy = (info.size < sizeof(XMFLOAT4X4)) ? info.size : sizeof(XMFLOAT4X4);
		memcpy( pDst, value.data(), nCopy );
	}
	else if ( value.type() == TYPE_XMFLOAT4 || value.type() == TYPE_SHADERFLOAT4 )
	{
		UINT nCopy = (info.size < sizeof(XMFLOAT4)) ? info.size : sizeof(XMFLOAT4);
		memcpy( pDst, value.data(), nCopy );
	}
	else if ( value.type() == TYPE_XMFLOAT3 || value.type() == TYPE_SHADERFLOAT3 )
	{
		UINT nCopy = (info.size < sizeof(XMFLOAT3)) ? info.size : sizeof(XMFLOAT3);
		memcpy( pDst, value.data(), nCopy );
	}
	else if ( value.type() == TYPE_VECTOR3 )
	{
		Vector3 vValue = (Vector3)value;
		XMFLOAT4 v4;
		v4.x = vValue.x;
		v4.y = vValue.y;
		v4.z = vValue.z;
		v4.w = 1.0f;
		UINT nCopy = (info.size < sizeof(XMFLOAT4)) ? info.size : sizeof(XMFLOAT4);
		memcpy( pDst, &v4, nCopy );
	}
	else if ( value.type() == TYPE_MATRIX33 )
	{
		Matrix33 mValue = (Matrix33)value;

		XMFLOAT4X4 m;
		m._11 = mValue.i.x;	m._12 = mValue.i.y;	m._13 = mValue.i.z;	m._14 = 0.0f;
		m._21 = mValue.j.x;	m._22 = mValue.j.y;	m._23 = mValue.j.z;	m._24 = 0.0f;
		m._31 = mValue.k.x;	m._32 = mValue.k.y;	m._33 = mValue.k.z;	m._34 = 0.0f;
		m._41 = 0.0f;			m._42 = 0.0f;			m._43 = 0.0f;			m._44 = 1.0f;

		UINT nCopy = (info.size < sizeof(XMFLOAT4X4)) ? info.size : sizeof(XMFLOAT4X4);
		memcpy( pDst, &m, nCopy );
	}
	else if ( value.type() == TYPE_COLOR )
	{
		Color color = (Color)value;
		float fColor[4];
		fColor[0] = color.r / 255.0f;
		fColor[1] = color.g / 255.0f;
		fColor[2] = color.b / 255.0f;
		fColor[3] = color.a / 255.0f;
		UINT nCopy = (info.size < sizeof(fColor)) ? info.size : sizeof(fColor);
		memcpy( pDst, fColor, nCopy );
	}
	else if ( value.type() == TYPE_SIZEINT || value.type() == TYPE_SIZEFLT )
	{
		SizeFloat sz = (SizeFloat)value;
		float fSize[2];
		fSize[0] = sz.width;
		fSize[1] = sz.height;
		UINT nCopy = (info.size < sizeof(fSize)) ? info.size : sizeof(fSize);
		memcpy( pDst, fSize, nCopy );
	}
}

//---------------------------------------------------------------------------------------------------
//EOF
