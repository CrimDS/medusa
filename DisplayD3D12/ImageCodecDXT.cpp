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
#include "rgbcx.h"

#include <stdint.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

// Master toggle: 1 = use Rich Geldreich's rgbcx encoder for BC1 (DXT1) and
// BC3 (DXT5).  ~5–10x faster than squish at equivalent or better quality.
// Set to 0 to fall back to squish for A/B comparison or if a regression
// is suspected.  DXT3 (BC2) and all decode paths always go through squish
// — rgbcx doesn't implement either.  The squish source files are still in
// the .vcxproj and the codecs still link against it for those paths.
#define USE_RGBCX 1

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

// Map our generic codec quality knob onto squish's color-fit selector.
// Cluster-fit is the slow/high-quality default; range-fit is roughly 5–10x
// faster with visibly lower quality on gradients (acceptable for FAST/NONE
// callers).  Iterative cluster-fit is much slower than plain cluster-fit
// for marginal extra quality, so BEST stays on plain cluster-fit unless
// somebody explicitly opts in.
static int squishLevelFlags( ImageCodec::EncodeLevel nLevel )
{
	switch ( nLevel )
	{
	case ImageCodec::CL_FAST:
	case ImageCodec::CL_NONE:
		return squish::kColourRangeFit;
	case ImageCodec::CL_BEST:
	case ImageCodec::CL_NORMAL:
	default:
		return squish::kColourClusterFit;
	}
}

#if USE_RGBCX
// rgbcx::init() builds global cluster-fit lookup tables and is documented
// as not thread-safe.  Magic-static guarantees one-time initialization
// (C++11 [stmt.dcl]/4) and is cheap to consult on subsequent calls.
static void EnsureRgbcxInit()
{
	static const bool s_inited = []() {
		rgbcx::init( rgbcx::bc1_approx_mode::cBC1Ideal );
		return true;
	}();
	(void)s_inited;
}

// rgbcx levels are 0..18.  Per the header recommendation, level 5+ competes
// with squish/NVTT for quality; level 10 is the "if in doubt" sweet spot.
// Lower bands target fast preview-grade encodes.
static uint32_t rgbcxLevel( ImageCodec::EncodeLevel nLevel )
{
	switch ( nLevel )
	{
	case ImageCodec::CL_FAST:
	case ImageCodec::CL_NONE:
		return 1;
	case ImageCodec::CL_NORMAL:
		return 5;
	case ImageCodec::CL_BEST:
	default:
		return 10;
	}
}

// Walk the source image in 4x4 blocks, calling rgbcx's per-block encoder
// for each.  Edge blocks where the image isn't a multiple of 4 get the
// missing rows / columns clamp-replicated from the last valid pixel —
// same convention squish uses internally.
static inline void GatherBlock4x4(
	const uint8_t * pSrc, int width, int height, int srcRowPitch,
	int blockX, int blockY, uint8_t pBlock[64] )
{
	for ( int py = 0; py < 4; ++py )
	{
		int sy = blockY + py;
		if ( sy >= height ) sy = height - 1;
		const uint8_t * pRow = pSrc + sy * srcRowPitch;
		for ( int px = 0; px < 4; ++px )
		{
			int sx = blockX + px;
			if ( sx >= width ) sx = width - 1;
			const uint8_t * pTexel = pRow + sx * 4;
			uint8_t * pDst = pBlock + ( py * 4 + px ) * 4;
			pDst[0] = pTexel[0];
			pDst[1] = pTexel[1];
			pDst[2] = pTexel[2];
			pDst[3] = pTexel[3];
		}
	}
}

// Parallel block-row work-stealing encoder.  Mirrors the BC7 codec pattern
// (ImageCodecBC7.cpp): each worker grabs the next available block-row via
// an atomic counter, encodes all blocks in that row, repeats until the
// queue empties.  Output ranges are disjoint per row, so no synchronisation
// is needed beyond the atomic counters.  rgbcx is documented thread-safe
// after init, so no codec-side locking required.  Main thread does NOT
// participate in encoding — it polls progress and pumps Windows messages
// via Progress::report so the dialog stays responsive and the UI throttle
// in ResourcerDoc continues to fire at the per-port granularity.
static void EncodeParallelByRow(
	const uint8_t * pSrc, void * pDst,
	int width, int height, int bytesPerBlock,
	const char * pStatus,
	void (*encodeBlock)( void * pDst, const uint8_t * pPixels, uint32_t level ),
	uint32_t level )
{
	const int blocksH     = ( height + 3 ) / 4;
	const int blocksW     = ( width  + 3 ) / 4;
	const int srcRowPitch = width * 4;

	std::atomic<int> nNextRow( 0 );
	std::atomic<int> nRowsDone( 0 );

	auto worker = [&]() {
		uint8_t block[64];
		while ( true )
		{
			const int by = nNextRow.fetch_add( 1, std::memory_order_relaxed );
			if ( by >= blocksH )
				break;

			uint8_t * pRowDst = (uint8_t *)pDst + ( by * blocksW ) * bytesPerBlock;
			for ( int bx = 0; bx < blocksW; ++bx )
			{
				GatherBlock4x4( pSrc, width, height, srcRowPitch,
					bx * 4, by * 4, block );
				encodeBlock( pRowDst, block, level );
				pRowDst += bytesPerBlock;
			}

			nRowsDone.fetch_add( 1, std::memory_order_relaxed );
		}
	};

	unsigned nThreads = std::thread::hardware_concurrency();
	if ( nThreads == 0 ) nThreads = 1;
	// One worker per row max — pointless to over-spawn for tiny mips.
	if ( (int)nThreads > blocksH ) nThreads = (unsigned)blocksH;

	// Single-threaded fast path for small mips at the bottom of the chain
	// (where thread spawn overhead dominates) and machines reporting
	// hardware_concurrency()==1.
	if ( nThreads <= 1 )
	{
		worker();
		Progress::report( blocksH, blocksH, pStatus );
		return;
	}

	std::vector< std::thread > workers;
	workers.reserve( nThreads );
	for ( unsigned t = 0; t < nThreads; ++t )
		workers.emplace_back( worker );

	// Main thread polls progress + pumps Windows messages via
	// Progress::report (the bridge calls SetPos which calls PumpMessages).
	// 50 ms cadence — frequent enough that the bar moves smoothly,
	// infrequent enough that polling overhead is negligible.
	while ( true )
	{
		const int done = nRowsDone.load( std::memory_order_relaxed );
		Progress::report( done, blocksH, pStatus );
		if ( done >= blocksH )
			break;
		std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
	}

	for ( auto & t : workers )
		t.join();

	Progress::report( blocksH, blocksH, pStatus );
}

// Per-block encode adapters.  rgbcx::encode_bc1 takes two extra bool args
// (allow_3color, use_transparent_texels_for_black) that we always pass the
// same way, so wrap them into the simpler 3-arg signature the parallel
// helper expects.
static void EncodeBlockBC1( void * pDst, const uint8_t * pPixels, uint32_t level )
{
	// allow_3color=true: better punchthrough/black handling at level>=5
	// use_transparent_texels_for_black=false: safer; our shaders don't
	// all ignore alpha on opaque DXT1 textures.
	rgbcx::encode_bc1( level, pDst, pPixels, true, false );
}

static void EncodeBlockBC3( void * pDst, const uint8_t * pPixels, uint32_t level )
{
	rgbcx::encode_bc3( level, pDst, pPixels );
}

static inline void EncodeBC1Parallel( const uint8_t * pSrc, void * pDst,
									  int width, int height, uint32_t level,
									  const char * pStatus )
{
	EncodeParallelByRow( pSrc, pDst, width, height, 8, pStatus,
		&EncodeBlockBC1, level );
}

static inline void EncodeBC3Parallel( const uint8_t * pSrc, void * pDst,
									  int width, int height, uint32_t level,
									  const char * pStatus )
{
	EncodeParallelByRow( pSrc, pDst, width, height, 16, pStatus,
		&EncodeBlockBC3, level );
}
#endif // USE_RGBCX

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
#if USE_RGBCX
	EnsureRgbcxInit();
	EncodeBC1Parallel( (uint8_t *)rgba.buffer(), output.buffer(),
		size.width, size.height, rgbcxLevel( nLevel ), "DXT1 encoding" );
#else
	CompressImageSlabbed( (squish::u8 *)rgba.buffer(), output.buffer(),
		size.width, size.height, squish::kDxt1 | squishLevelFlags( nLevel ), 8, "DXT1 encoding" );
#endif
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
		size.width, size.height, squish::kDxt3 | squishLevelFlags( nLevel ), 16, "DXT3 encoding" );
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
#if USE_RGBCX
	EnsureRgbcxInit();
	EncodeBC3Parallel( (uint8_t *)rgba.buffer(), output.buffer(),
		size.width, size.height, rgbcxLevel( nLevel ), "DXT5 encoding" );
#else
	CompressImageSlabbed( (squish::u8 *)rgba.buffer(), output.buffer(),
		size.width, size.height, squish::kDxt5 | squishLevelFlags( nLevel ), 16, "DXT5 encoding" );
#endif
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
