/*
	ImageCodecBC7.cpp - D3D12 BC7 codec (DX12 only)
	Wraps Rich Geldreich's bc7enc / bc7decomp (medusa/ThirdParty/bc7enc/).
	Encoder uses modes 1/6 for opaque blocks, 5/6/7 for blocks with alpha —
	bc7enc decides per-block.  Engine Color is BGRA, bc7enc wants RGBA, so
	we swap on both sides like the squish wrapper does.
	(c)2026 Palestar
*/

#include "ImageCodecBC7.h"
#include "Standard/Color.h"
#include "Standard/Progress.h"
#include "bc7enc.h"
#include "bc7decomp.h"

#include <stdint.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

//---------------------------------------------------------------------------------------------------

IMPLEMENT_FACTORY( ImageCodecBC7D3D12, ImageCodec );

ImageCodecBC7D3D12::ImageCodecBC7D3D12()
{}

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

// One-shot lookup-table init.  bc7enc_compress_block_init() is not
// re-entrant; calling it twice is harmless but wasteful.  Decoder needs
// no init.
static void EnsureEncoderInit()
{
	static bool s_bInit = false;
	if ( ! s_bInit )
	{
		bc7enc_compress_block_init();
		s_bInit = true;
	}
}

// Map medusa's EncodeLevel onto bc7enc's quality knobs.  Defaults are
// initialised first via bc7enc_compress_block_params_init() then tuned.
static void ConfigureParams( bc7enc_compress_block_params & params, ImageCodec::EncodeLevel nLevel )
{
	bc7enc_compress_block_params_init( &params );
	bc7enc_compress_block_params_init_linear_weights( &params );

	switch ( nLevel )
	{
	case ImageCodec::CL_BEST:
		params.m_uber_level    = BC7ENC_MAX_UBER_LEVEL;
		params.m_max_partitions = BC7ENC_MAX_PARTITIONS;
		break;
	case ImageCodec::CL_NORMAL:
		params.m_uber_level    = 2;
		params.m_max_partitions = BC7ENC_MAX_PARTITIONS;
		break;
	case ImageCodec::CL_FAST:
		params.m_uber_level    = 0;
		params.m_max_partitions = 16;
		break;
	case ImageCodec::CL_NONE:
	default:
		params.m_uber_level    = 0;
		params.m_max_partitions = 0;	// disables mode 1 partition search
		break;
	}
}

//---------------------------------------------------------------------------------------------------

int ImageCodecBC7D3D12::encode( const Buffer & input, Buffer & output, const SizeInt & size, EncodeLevel nLevel )
{
	// MUST init from the main thread before spawning workers.  After
	// init the encoder's static lookup tables are read-only, so
	// bc7enc_compress_block is reentrant from multiple threads.
	EnsureEncoderInit();

	bc7enc_compress_block_params params;
	ConfigureParams( params, nLevel );

	// Swap BGRA -> RGBA across the whole image up-front (single linear pass).
	Buffer rgba;
	SwapRedBlue( input, rgba );
	const Color * pSrc = (const Color *)rgba.buffer();

	// Block grid covers ceil(size/4) — partial edge blocks replicate the
	// rightmost/bottommost source pixels (matches squish's behaviour for
	// sub-4 mips at the bottom of the chain).
	const int blocksW = (size.width  + 3) / 4;
	const int blocksH = (size.height + 3) / 4;
	output.allocate( blocksW * blocksH * 16 );
	uint8_t * const pDst = (uint8_t *)output.buffer();

	// Block-row work-stealing queue.  Each worker grabs the next
	// available row, encodes all blocks in it, repeats until queue
	// empty.  Each row writes to a disjoint output range so no
	// synchronisation needed beyond the atomic row counter.
	std::atomic<int> nNextRow( 0 );
	std::atomic<int> nRowsDone( 0 );

	auto worker = [&]()
	{
		uint8_t blockPixels[16 * 4];
		while ( true )
		{
			const int by = nNextRow.fetch_add( 1, std::memory_order_relaxed );
			if ( by >= blocksH )
				break;

			uint8_t * pRowDst = pDst + (by * blocksW) * 16;
			for ( int bx = 0; bx < blocksW; ++bx )
			{
				for ( int row = 0; row < 4; ++row )
				{
					int sy = by * 4 + row;
					if ( sy >= size.height ) sy = size.height - 1;
					for ( int col = 0; col < 4; ++col )
					{
						int sx = bx * 4 + col;
						if ( sx >= size.width ) sx = size.width - 1;
						memcpy( &blockPixels[(row * 4 + col) * 4], &pSrc[sy * size.width + sx], sizeof(Color) );
					}
				}

				bc7enc_compress_block( pRowDst, blockPixels, &params );
				pRowDst += 16;
			}

			nRowsDone.fetch_add( 1, std::memory_order_relaxed );
		}
	};

	unsigned nThreads = std::thread::hardware_concurrency();
	if ( nThreads == 0 ) nThreads = 1;
	// One worker per row max — pointless to over-spawn for tiny mips.
	if ( (int)nThreads > blocksH ) nThreads = blocksH;

	// Single-threaded path for small mips at the bottom of the chain
	// (where thread spawn overhead dominates) and machines reporting
	// hardware_concurrency()==1.
	if ( nThreads <= 1 )
	{
		worker();
		Progress::report( blocksH, blocksH, "BC7 encoding" );
		return output.bufferSize();
	}

	std::vector< std::thread > workers;
	workers.reserve( nThreads );
	for ( unsigned t = 0; t < nThreads; ++t )
		workers.emplace_back( worker );

	// Main thread polls progress + pumps Windows messages via
	// Progress::report (the bridge calls SetPos which calls PumpMessages).
	// 50 ms is a good cadence — frequent enough that the bar moves
	// smoothly, infrequent enough that polling overhead is negligible.
	while ( true )
	{
		const int done = nRowsDone.load( std::memory_order_relaxed );
		Progress::report( done, blocksH, "BC7 encoding" );
		if ( done >= blocksH )
			break;
		std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
	}

	for ( auto & t : workers )
		t.join();

	Progress::report( blocksH, blocksH, "BC7 encoding" );
	return output.bufferSize();
}

//---------------------------------------------------------------------------------------------------

int ImageCodecBC7D3D12::decode( const Buffer & input, Buffer & output, const SizeInt & size )
{
	int blocksW = (size.width  + 3) / 4;
	int blocksH = (size.height + 3) / 4;
	if ( (int)input.bufferSize() < blocksW * blocksH * 16 )
		return -1;

	// Decode into an RGBA scratch first, then swap to BGRA at the end —
	// matches the squish wrapper's pattern and keeps the swap a single
	// linear pass.
	Buffer rgba;
	rgba.allocate( size.width * size.height * sizeof(Color) );
	Color * pDst = (Color *)rgba.buffer();

	const uint8_t * pSrc = (const uint8_t *)input.buffer();
	bc7decomp::color_rgba blockPixels[16];

	for ( int by = 0; by < blocksH; ++by )
	{
		Progress::report( by, blocksH, "BC7 decoding" );

		for ( int bx = 0; bx < blocksW; ++bx )
		{
			if ( ! bc7decomp::unpack_bc7( pSrc, blockPixels ) )
				return -1;
			pSrc += 16;

			// Skip block pixels that fall outside the visible image.
			for ( int row = 0; row < 4; ++row )
			{
				int dy = by * 4 + row;
				if ( dy >= size.height ) break;
				for ( int col = 0; col < 4; ++col )
				{
					int dx = bx * 4 + col;
					if ( dx >= size.width ) break;
					memcpy( &pDst[dy * size.width + dx], &blockPixels[row * 4 + col], sizeof(Color) );
				}
			}
		}
	}

	Progress::report( blocksH, blocksH, "BC7 decoding" );
	SwapRedBlue( rgba, output );
	return output.bufferSize();
}

//---------------------------------------------------------------------------------------------------
// EOF
