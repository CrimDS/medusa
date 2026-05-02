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
#include "Standard/Progress.h"
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

// Slab the squish::CompressImage call so we can report per-slab progress
// and the UI doesn't appear hung on big mips.  squish itself processes
// the whole input in one call; chunking by N block-rows lets us call
// Progress::report between chunks.
static void CompressImageSlabbed( const squish::u8 * pSrc, void * pDst,
								  int width, int height, int squishFlags,
								  int bytesPerBlock, const char * pStatus )
{
	const int blocksH       = ( height + 3 ) / 4;
	const int blocksW       = ( width  + 3 ) / 4;
	const int dstRowPitch   = blocksW * bytesPerBlock;
	const int srcRowPitch   = width * 4;
	const int CHUNK_BROWS   = 16;	// 16 block-rows == 64 source rows per slab

	for ( int br = 0; br < blocksH; br += CHUNK_BROWS )
	{
		Progress::report( br, blocksH, pStatus );

		int chunkBRows = blocksH - br;
		if ( chunkBRows > CHUNK_BROWS )
			chunkBRows = CHUNK_BROWS;

		int srcStartY  = br * 4;
		int chunkRows  = chunkBRows * 4;
		if ( srcStartY + chunkRows > height )
			chunkRows = height - srcStartY;

		const squish::u8 * pSlabSrc = pSrc + srcStartY * srcRowPitch;
		void * pSlabDst             = (squish::u8 *)pDst + br * dstRowPitch;

		squish::CompressImage( (squish::u8 *)pSlabSrc, width, chunkRows, pSlabDst, squishFlags );
	}

	Progress::report( blocksH, blocksH, pStatus );
}

static void DecompressImageSlabbed( squish::u8 * pDst, const void * pSrc,
									int width, int height, int squishFlags,
									int bytesPerBlock, const char * pStatus )
{
	const int blocksH       = ( height + 3 ) / 4;
	const int blocksW       = ( width  + 3 ) / 4;
	const int srcRowPitch   = blocksW * bytesPerBlock;
	const int dstRowPitch   = width * 4;
	const int CHUNK_BROWS   = 16;

	for ( int br = 0; br < blocksH; br += CHUNK_BROWS )
	{
		Progress::report( br, blocksH, pStatus );

		int chunkBRows = blocksH - br;
		if ( chunkBRows > CHUNK_BROWS )
			chunkBRows = CHUNK_BROWS;

		int dstStartY  = br * 4;
		int chunkRows  = chunkBRows * 4;
		if ( dstStartY + chunkRows > height )
			chunkRows = height - dstStartY;

		squish::u8 *       pSlabDst = pDst + dstStartY * dstRowPitch;
		const squish::u8 * pSlabSrc = (const squish::u8 *)pSrc + br * srcRowPitch;

		squish::DecompressImage( pSlabDst, width, chunkRows, (void *)pSlabSrc, squishFlags );
	}

	Progress::report( blocksH, blocksH, pStatus );
}

//---------------------------------------------------------------------------------------------------
// DXT1

IMPLEMENT_FACTORY( ImageCodecDXT1D3D12, ImageCodec );

ImageCodecDXT1D3D12::ImageCodecDXT1D3D12()
{}

int ImageCodecDXT1D3D12::encode( const Buffer & input, Buffer & output, const SizeInt & size, EncodeLevel nLevel )
{
	Buffer rgba;
	SwapRedBlue( input, rgba );
	output.allocate( squish::GetStorageRequirements( size.width, size.height, squish::kDxt1 ) );
	CompressImageSlabbed( (squish::u8 *)rgba.buffer(), output.buffer(),
		size.width, size.height, squish::kDxt1, 8, "DXT1 encoding" );
	return output.bufferSize();
}

int ImageCodecDXT1D3D12::decode( const Buffer & input, Buffer & output, const SizeInt & size )
{
	Buffer rgba;
	rgba.allocate( size.width * size.height * sizeof(Color) );
	DecompressImageSlabbed( (squish::u8 *)rgba.buffer(), input.buffer(),
		size.width, size.height, squish::kDxt1, 8, "DXT1 decoding" );
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
	CompressImageSlabbed( (squish::u8 *)rgba.buffer(), output.buffer(),
		size.width, size.height, squish::kDxt3, 16, "DXT3 encoding" );
	return output.bufferSize();
}

int ImageCodecDXT3D3D12::decode( const Buffer & input, Buffer & output, const SizeInt & size )
{
	Buffer rgba;
	rgba.allocate( size.width * size.height * sizeof(Color) );
	DecompressImageSlabbed( (squish::u8 *)rgba.buffer(), input.buffer(),
		size.width, size.height, squish::kDxt3, 16, "DXT3 decoding" );
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
	CompressImageSlabbed( (squish::u8 *)rgba.buffer(), output.buffer(),
		size.width, size.height, squish::kDxt5, 16, "DXT5 encoding" );
	return output.bufferSize();
}

int ImageCodecDXT5D3D12::decode( const Buffer & input, Buffer & output, const SizeInt & size )
{
	Buffer rgba;
	rgba.allocate( size.width * size.height * sizeof(Color) );
	DecompressImageSlabbed( (squish::u8 *)rgba.buffer(), input.buffer(),
		size.width, size.height, squish::kDxt5, 16, "DXT5 decoding" );
	SwapRedBlue( rgba, output );
	return output.bufferSize();
}

//---------------------------------------------------------------------------------------------------
// EOF
