/*
	ImageCodecDXT.cpp - D3D12 version
	CPU-side DXT compression/decompression without D3DX dependency.
	Registered under unique names (ImageCodecDXT1D3D12 etc.) to avoid
	conflicting with the D3D9 codec which also loads alongside this DLL.
	NOTE: The render path (Material.cpp) memcpy's raw BC block data directly
	into the locked surface — this codec is only used for Image::convert().
	(c)2024 Palestar
*/

#include "ImageCodecDXT.h"
#include "squish.h"

//---------------------------------------------------------------------------------------------------

static void SwapRedBlue( const Buffer & input, Buffer & output )
{
	dword nPixels = input.bufferSize() / sizeof(Color);
	output.allocate( input.bufferSize() );

	Color * pIn  = (Color *)input.buffer();
	Color * pOut = (Color *)output.buffer();
	for ( dword p = 0; p < nPixels; ++p )
		pOut[p] = pIn[p].BGRA();
}

//---------------------------------------------------------------------------------------------------
// DXT1

IMPLEMENT_FACTORY( ImageCodecDXT1D3D12, ImageCodec );

ImageCodecDXT1D3D12::ImageCodecDXT1D3D12()
{}

int ImageCodecDXT1D3D12::encode( const Buffer & input, Buffer & output, const SizeInt & size, EncodeLevel nLevel )
{
	// squish expects RGBA; Color is BGRA so swap first
	Buffer rgba;
	SwapRedBlue( input, rgba );

	output.allocate( squish::GetStorageRequirements( size.width, size.height, squish::kDxt1 ) );
	squish::CompressImage( (squish::u8 *)rgba.buffer(), size.width, size.height, output.buffer(), squish::kDxt1 );
	return output.bufferSize();
}

int ImageCodecDXT1D3D12::decode( const Buffer & input, Buffer & output, const SizeInt & size )
{
	// squish outputs RGBA; engine expects BGRA (Color), so swap after decompress
	Buffer rgba;
	rgba.allocate( size.width * size.height * sizeof(Color) );
	squish::DecompressImage( (squish::u8 *)rgba.buffer(), size.width, size.height, input.buffer(), squish::kDxt1 );

	SwapRedBlue( rgba, output );
	return output.bufferSize();
}

//---------------------------------------------------------------------------------------------------
// DXT3

IMPLEMENT_FACTORY( ImageCodecDXT3D3D12, ImageCodec );

ImageCodecDXT3D3D12::ImageCodecDXT3D3D12()
{}

int ImageCodecDXT3D3D12::encode( const Buffer & input, Buffer & output, const SizeInt & size, EncodeLevel nLevel )
{
	Buffer rgba;
	SwapRedBlue( input, rgba );

	output.allocate( squish::GetStorageRequirements( size.width, size.height, squish::kDxt3 ) );
	squish::CompressImage( (squish::u8 *)rgba.buffer(), size.width, size.height, output.buffer(), squish::kDxt3 );
	return output.bufferSize();
}

int ImageCodecDXT3D3D12::decode( const Buffer & input, Buffer & output, const SizeInt & size )
{
	Buffer rgba;
	rgba.allocate( size.width * size.height * sizeof(Color) );
	squish::DecompressImage( (squish::u8 *)rgba.buffer(), size.width, size.height, input.buffer(), squish::kDxt3 );

	SwapRedBlue( rgba, output );
	return output.bufferSize();
}

//---------------------------------------------------------------------------------------------------
// DXT5

IMPLEMENT_FACTORY( ImageCodecDXT5D3D12, ImageCodec );

ImageCodecDXT5D3D12::ImageCodecDXT5D3D12()
{}

int ImageCodecDXT5D3D12::encode( const Buffer & input, Buffer & output, const SizeInt & size, EncodeLevel nLevel )
{
	Buffer rgba;
	SwapRedBlue( input, rgba );

	output.allocate( squish::GetStorageRequirements( size.width, size.height, squish::kDxt5 ) );
	squish::CompressImage( (squish::u8 *)rgba.buffer(), size.width, size.height, output.buffer(), squish::kDxt5 );
	return output.bufferSize();
}

int ImageCodecDXT5D3D12::decode( const Buffer & input, Buffer & output, const SizeInt & size )
{
	Buffer rgba;
	rgba.allocate( size.width * size.height * sizeof(Color) );
	squish::DecompressImage( (squish::u8 *)rgba.buffer(), size.width, size.height, input.buffer(), squish::kDxt5 );

	SwapRedBlue( rgba, output );
	return output.bufferSize();
}

//---------------------------------------------------------------------------------------------------
// EOF
