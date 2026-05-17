/*
** z_alloc_stub.c — zcalloc/zcfree implementations for the Z_SOLO build.
**
** Z_SOLO disables zlib's stdio-using code paths (gz file API, etc.), which
** is what we want — we don't ship a gzguts.h.  Side-effect: the default
** zcalloc/zcfree memory allocators in zutil.c are also excluded under
** Z_SOLO, since they live in the same block.  zlib expects the host to
** provide them.  These are plain malloc/free wrappers, matching what the
** non-Z_SOLO defaults do.
**
** Only built on the Linux (gcc) path; the Windows build uses the full
** stdio-enabled zlib from the engine project files.
*/

#include <stdlib.h>
#include "zutil.h"

voidpf ZLIB_INTERNAL zcalloc(voidpf opaque, unsigned items, unsigned size)
{
	(void)opaque;
	return (voidpf)calloc(items, size);
}

void ZLIB_INTERNAL zcfree(voidpf opaque, voidpf ptr)
{
	(void)opaque;
	free(ptr);
}
