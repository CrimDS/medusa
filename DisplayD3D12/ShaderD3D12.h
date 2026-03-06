/**
	@file ShaderD3D12.h
	@brief DirectX 12 shader wrapper - replaces the DX9 Shader class

	(c)2024 Palestar
*/

#ifndef SHADER_D3D12_H
#define SHADER_D3D12_H

#include "Factory/Widget.h"
#include "Standard/WeakReference.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <map>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

class DisplayDeviceD3D12;		// forward declare

//---------------------------------------------------------------------------------------------------

class ShaderD3D12 : public Widget
{
public:
	DECLARE_WIDGET_CLASS();

	typedef Reference< ShaderD3D12 >		Ref;
	typedef WeakReference< ShaderD3D12 >	WeakRef;

	//! Types
	struct ConstantInfo
	{
		std::string		name;
		UINT			offset;			// byte offset within the constant buffer
		UINT			size;			// size in bytes
	};

	typedef std::map< std::string, Value >			ConstantMap;
	typedef std::map< std::string, ConstantInfo >	ConstantLayoutMap;

	//! Construction
	ShaderD3D12();
	virtual ~ShaderD3D12();

	//! Accessors
	bool						valid() const;					// true if this shader is ready to be used
	bool						released() const;				// true if this shader has been released
	const CharString &			shaderName() const;				// filename of this shader

	ID3DBlob *					vertexShaderBlob() const;		// compiled VS bytecode
	ID3DBlob *					pixelShaderBlob() const;		// compiled PS bytecode

	D3D12_SHADER_BYTECODE		vertexShaderBytecode() const;	// VS bytecode descriptor for PSO creation
	D3D12_SHADER_BYTECODE		pixelShaderBytecode() const;	// PS bytecode descriptor for PSO creation

	UINT						constantBufferSize() const;		// total CB size (256-byte aligned)

	//! Mutators
	bool						load( DisplayDeviceD3D12 * pDevice, const char * pShaderName );
	void						release();

	void						setConstant( const char * pConstant, const Value & value );
	void						clearConstant( const char * pConstant );

	bool						apply();
	void						applyConstants( void * pCBData, UINT cbSize );

	bool						needsReload() const;			// check if shader file has been modified

protected:
	//! Internal
	bool						compileShader( const wchar_t * pFilePath, const char * pEntryPoint,
									const char * pTarget, ComPtr<ID3DBlob> & pBlobOut );
	void						reflectConstantLayout( ID3DBlob * pBlob, ConstantLayoutMap & layoutOut );

	static void					applyConstant( void * pCBData, UINT cbSize,
									const ConstantInfo & info, const Value & value );

	//! Data
	bool						m_bValid;					// true if this shader is ready to be used
	bool						m_bReleased;				// true if this shader has been released
	CharString					m_sShaderName;				// filename of this shader
	dword						m_nShaderFileTime;			// timestamp from the shader file
	CharString					m_sFullPath;				// full resolved path to the shader file

	ComPtr<ID3DBlob>			m_pVSBlob;					// compiled vertex shader bytecode
	ComPtr<ID3DBlob>			m_pPSBlob;					// compiled pixel shader bytecode

	ConstantMap					m_Constants;				// shader constants set by the engine
	ConstantLayoutMap			m_VSConstantLayout;			// VS constant buffer variable layout
	ConstantLayoutMap			m_PSConstantLayout;			// PS constant buffer variable layout
	UINT						m_nVSConstantBufferSize;	// VS constant buffer size (aligned)
	UINT						m_nPSConstantBufferSize;	// PS constant buffer size (aligned)
};

//---------------------------------------------------------------------------------------------------

inline bool ShaderD3D12::valid() const
{
	return m_bValid;
}

inline bool ShaderD3D12::released() const
{
	return m_bReleased;
}

inline const CharString & ShaderD3D12::shaderName() const
{
	return m_sShaderName;
}

inline ID3DBlob * ShaderD3D12::vertexShaderBlob() const
{
	return m_pVSBlob.Get();
}

inline ID3DBlob * ShaderD3D12::pixelShaderBlob() const
{
	return m_pPSBlob.Get();
}

inline D3D12_SHADER_BYTECODE ShaderD3D12::vertexShaderBytecode() const
{
	D3D12_SHADER_BYTECODE bc = {};
	if ( m_pVSBlob )
	{
		bc.pShaderBytecode = m_pVSBlob->GetBufferPointer();
		bc.BytecodeLength = m_pVSBlob->GetBufferSize();
	}
	return bc;
}

inline D3D12_SHADER_BYTECODE ShaderD3D12::pixelShaderBytecode() const
{
	D3D12_SHADER_BYTECODE bc = {};
	if ( m_pPSBlob )
	{
		bc.pShaderBytecode = m_pPSBlob->GetBufferPointer();
		bc.BytecodeLength = m_pPSBlob->GetBufferSize();
	}
	return bc;
}

inline UINT ShaderD3D12::constantBufferSize() const
{
	// return the larger of VS/PS CB sizes, 256-byte aligned
	UINT nMax = m_nVSConstantBufferSize > m_nPSConstantBufferSize ? m_nVSConstantBufferSize : m_nPSConstantBufferSize;
	return (nMax + 255) & ~255;
}

//---------------------------------------------------------------------------------------------------

#endif

//---------------------------------------------------------------------------------------------------
//EOF
