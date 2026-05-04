/*
	ImageCodecJPEG.cpp

	JPEG codec for medusa's ImageCodec interface.  Backed by stb_image /
	stb_image_write (public domain) — replaces the legacy Intel JPEG Library
	(ijl15) which was x86-only and blocked the x64 client migration.  See
	project_x64_cpp17_migration memory note.

	(c)2005 Palestar Inc, Richard Lyle
*/

#define SYSTEMWIN_DLL
#include "Debug/Assert.h"
#include "SystemWin/ImageCodecJPEG.h"

#include <string.h>
#include <stdlib.h>

// Inline single-translation-unit build of stb_image / stb_image_write — these
// macros must precede the #includes and only ever appear in ONE .cpp.  See
// medusa/ThirdParty/stb/stb_image.h header comment.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO		// we feed bytes only — no fopen
#define STBI_ONLY_JPEG		// JPEG-only decode keeps stb_image's compiled size minimal
#include "../ThirdParty/stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO	// we capture bytes via callback — no fopen
#include "../ThirdParty/stb/stb_image_write.h"

//----------------------------------------------------------------------------

bool EncodeToJPEGBuffer( byte* lpRgbBuffer, dword dwWidth, dword dwHeight, byte** lppJpgBuffer, dword* lpdwJpgBufferSize, int quality);
bool DecodeFromJPEGBuffer( byte* lpJpgBuffer, dword dwJpgBufferSize, byte** lppRgbBuffer,
						  dword* lpdwWidth, dword* lpdwHeight, dword* lpdwNumberOfChannels);

//----------------------------------------------------------------------------

IMPLEMENT_FACTORY( ImageCodecJPEG, ImageCodec );

int	ImageCodecJPEG::encode( const Buffer & input, Buffer & output, const SizeInt & size, EncodeLevel nLevel )
{
	int nQuality = 100;
	if ( nLevel == CL_FAST )
		nQuality = 80;
	else if ( nLevel == CL_NONE )
		nQuality = 50;

	// compress the frame data
	byte *	jpgBuffer = NULL;
	dword	jpgSize = 0;

	if (! EncodeToJPEGBuffer( (byte *)input.buffer(), size.width, size.height, &jpgBuffer, &jpgSize, nQuality ) )
		return -1;

	output.set( jpgBuffer, jpgSize );
	return jpgSize;
}

int ImageCodecJPEG::decode( const Buffer & input, Buffer & output, const SizeInt & size )
{
	dword rgbWidth = 0, rgbHeight = 0, rgbChannels = 0;
	byte * rgbBuffer = NULL;

	// decompress the jpeg data into the buffer
	if (! DecodeFromJPEGBuffer( (byte *)input.buffer(), input.bufferSize(), &rgbBuffer, &rgbWidth, &rgbHeight, &rgbChannels ) )
		return -1;		// an error has occured
	if ( size.width != (int)rgbWidth || size.height != (int)rgbHeight )
	{
		delete [] rgbBuffer;
		return -1;		// image is not the correct size
	}

	int nBytes = rgbWidth * rgbHeight * rgbChannels;
	output.set( rgbBuffer, nBytes );

	return nBytes;
}

//----------------------------------------------------------------------------

// Decode JPEG bytes → RGBA8 pixel buffer.  Output buffer is allocated with new[]
// so the caller's existing `delete[]` matches.  Replaces ijlInit/ijlRead/ijlFree.
static bool DecodeFromJPEGBuffer( byte* lpJpgBuffer, dword dwJpgBufferSize, byte** lppRgbBuffer,
                                   dword* lpdwWidth, dword* lpdwHeight, dword* lpdwNumberOfChannels )
{
	int w = 0, h = 0, srcComp = 0;
	stbi_uc * stbiPixels = stbi_load_from_memory( (const stbi_uc *)lpJpgBuffer, (int)dwJpgBufferSize,
	                                              &w, &h, &srcComp, 4 /* force RGBA out */ );
	if ( stbiPixels == NULL )
	{
		*lppRgbBuffer = NULL;
		*lpdwWidth = 0;
		*lpdwHeight = 0;
		*lpdwNumberOfChannels = 0;
		return false;
	}

	// stbi allocates with malloc; the medusa caller frees with delete[].  Copy into
	// a new[]'d buffer so the deallocator pairs correctly.
	const dword nBytes = (dword)w * (dword)h * 4;
	byte * outBuf = new byte[ nBytes ];
	memcpy( outBuf, stbiPixels, nBytes );
	stbi_image_free( stbiPixels );

	*lppRgbBuffer = outBuf;
	*lpdwWidth = (dword)w;
	*lpdwHeight = (dword)h;
	*lpdwNumberOfChannels = 4;	// always 4 (RGBA), we forced channels=4 above
	return true;
}

//----------------------------------------------------------------------------

// Output collector for stbi_write_jpg_to_func — accumulates JPEG bytes into a
// dynamically grown buffer.  Geometric growth keeps amortised cost O(1) per byte.
namespace {
	struct JpgWriteCtx {
		byte *	buf;
		dword	size;
		dword	cap;
	};

	void jpg_write_callback( void * context, void * data, int size )
	{
		JpgWriteCtx * ctx = (JpgWriteCtx *)context;
		const dword wantSize = ctx->size + (dword)size;
		if ( wantSize > ctx->cap )
		{
			dword newCap = ctx->cap == 0 ? 8192 : ctx->cap * 2;
			while ( newCap < wantSize )
				newCap *= 2;
			byte * newBuf = new byte[ newCap ];
			if ( ctx->buf != NULL )
			{
				memcpy( newBuf, ctx->buf, ctx->size );
				delete [] ctx->buf;
			}
			ctx->buf = newBuf;
			ctx->cap = newCap;
		}
		memcpy( ctx->buf + ctx->size, data, size );
		ctx->size += (dword)size;
	}
}

// Encode RGBA8 pixel buffer → JPEG bytes.  stbi_write_jpg only accepts 1 or 3
// channel input, so we strip alpha to RGB before encoding.  Output buffer is
// allocated with new[] for matching delete[] in the caller.  Replaces ijlWrite.
static bool EncodeToJPEGBuffer( byte* lpRgbBuffer, dword dwWidth, dword dwHeight,
                                 byte** lppJpgBuffer, dword* lpdwJpgBufferSize, int quality )
{
	*lppJpgBuffer = NULL;
	*lpdwJpgBufferSize = 0;

	if ( dwWidth == 0 || dwHeight == 0 || lpRgbBuffer == NULL )
		return false;

	// Strip RGBA → RGB for stbi_write_jpg.  ImageCodec input is always 4-channel
	// per the encode() signature; stb_image_write doesn't support 4-channel JPEG.
	const dword nPixels = dwWidth * dwHeight;
	byte * rgbOnly = new byte[ nPixels * 3 ];
	for ( dword i = 0; i < nPixels; ++i )
	{
		rgbOnly[ i * 3 + 0 ] = lpRgbBuffer[ i * 4 + 0 ];
		rgbOnly[ i * 3 + 1 ] = lpRgbBuffer[ i * 4 + 1 ];
		rgbOnly[ i * 3 + 2 ] = lpRgbBuffer[ i * 4 + 2 ];
	}

	JpgWriteCtx ctx;
	ctx.buf  = NULL;
	ctx.size = 0;
	ctx.cap  = 0;

	const int ok = stbi_write_jpg_to_func( jpg_write_callback, &ctx,
	                                       (int)dwWidth, (int)dwHeight, 3, rgbOnly, quality );
	delete [] rgbOnly;

	if ( !ok )
	{
		delete [] ctx.buf;
		return false;
	}

	*lppJpgBuffer = ctx.buf;
	*lpdwJpgBufferSize = ctx.size;
	return true;
}

//----------------------------------------------------------------------------
//EOF
