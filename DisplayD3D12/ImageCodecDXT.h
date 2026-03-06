/*
	ImageCodecDXT.h - D3D12 version
	Uses squish library for CPU-side DXT compression (no D3DX dependency)
	(c)2024 Palestar
*/

#ifndef IMAGE_CODEC_DXT_D3D12_H
#define IMAGE_CODEC_DXT_D3D12_H

#include "Draw/ImageCodec.h"

//---------------------------------------------------------------------------------------------------

class ImageCodecDXT1D3D12 : public ImageCodec
{
public:
	DECLARE_WIDGET_CLASS();

	ImageCodecDXT1D3D12();
	virtual int			encode( const Buffer & input, Buffer & output, const SizeInt & size, EncodeLevel nLevel );
	virtual int			decode( const Buffer & input, Buffer & output, const SizeInt & size );
};

class ImageCodecDXT3D3D12 : public ImageCodec
{
public:
	DECLARE_WIDGET_CLASS();

	ImageCodecDXT3D3D12();
	virtual int			encode( const Buffer & input, Buffer & output, const SizeInt & size, EncodeLevel nLevel );
	virtual int			decode( const Buffer & input, Buffer & output, const SizeInt & size );
};

class ImageCodecDXT5D3D12 : public ImageCodec
{
public:
	DECLARE_WIDGET_CLASS();

	ImageCodecDXT5D3D12();
	virtual int			encode( const Buffer & input, Buffer & output, const SizeInt & size, EncodeLevel nLevel );
	virtual int			decode( const Buffer & input, Buffer & output, const SizeInt & size );
};

//---------------------------------------------------------------------------------------------------

#endif

// EOF
