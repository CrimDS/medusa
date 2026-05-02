/*
	ImageCodecBC7.h - D3D12 BC7 codec (DX12 only)
	Registered as ImageCodecBC7D3D12 alongside ImageCodecDXT*D3D12.
	Phase 1 stub: factory class registered, encode/decode return -1 until
	the bc7enc vendor drop in phase 2.  The renderer memcpys BC blocks
	straight into the locked surface (see ImageCodecDXT.cpp:6) so render
	doesn't depend on this codec — only Image::convert and the Resourcer
	encode path do.
	(c)2026 Palestar
*/

#ifndef IMAGE_CODEC_BC7_D3D12_H
#define IMAGE_CODEC_BC7_D3D12_H

#include "Draw/ImageCodec.h"

class ImageCodecBC7D3D12 : public ImageCodec
{
public:
	DECLARE_WIDGET_CLASS();

	ImageCodecBC7D3D12();
	virtual int			encode( const Buffer & input, Buffer & output, const SizeInt & size, EncodeLevel nLevel );
	virtual int			decode( const Buffer & input, Buffer & output, const SizeInt & size );
};

#endif

// EOF
