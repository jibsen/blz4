//
// blz4 - Example of LZ4 compression with BriefLZ algorithms
//
// C packer
//
// Copyright (c) 2018 Joergen Ibsen
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
//   1. The origin of this software must not be misrepresented; you must
//      not claim that you wrote the original software. If you use this
//      software in a product, an acknowledgment in the product
//      documentation would be appreciated but is not required.
//
//   2. Altered source versions must be plainly marked as such, and must
//      not be misrepresented as being the original software.
//
//   3. This notice may not be removed or altered from any source
//      distribution.
//

#include "lz4.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>

#if _MSC_VER >= 1400
#  include <intrin.h>
#  define LZ4_BUILTIN_MSVC
#elif defined(__clang__) && defined(__has_builtin)
#  if __has_builtin(__builtin_clz)
#    define LZ4_BUILTIN_GCC
#  endif
#elif __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
#  define LZ4_BUILTIN_GCC
#endif

// Number of bits of hash to use for lookup.
//
// The size of the lookup table (and thus workmem) depends on this.
//
// Values between 10 and 18 work well. Lower values generally make compression
// speed faster but ratio worse. The default value 17 (128k entries) is a
// compromise.
//
#ifndef LZ4_HASH_BITS
#  define LZ4_HASH_BITS 17
#endif

#define LOOKUP_SIZE (1UL << LZ4_HASH_BITS)

#define WORKMEM_SIZE (LOOKUP_SIZE * sizeof(unsigned long))

#define NO_MATCH_POS ((unsigned long) -1)

static int
lz4_log2(unsigned long n)
{
	assert(n > 0);

#if defined(LZ4_BUILTIN_MSVC)
	unsigned long msb_pos;
	_BitScanReverse(&msb_pos, n);
	return (int) msb_pos;
#elif defined(LZ4_BUILTIN_GCC)
	return (int) sizeof(n) * CHAR_BIT - 1 - __builtin_clzl(n);
#else
	int bits = 0;

	while (n >>= 1) {
		++bits;
	}

	return bits;
#endif
}

// Hash four bytes starting a p.
//
// This is Fibonacci hashing, also known as Knuth's multiplicative hash. The
// constant is a prime close to 2^32/phi.
//
static unsigned long
lz4_hash4_bits(const unsigned char *p, int bits)
{
	assert(bits > 0 && bits <= 32);

	unsigned long val = (unsigned long) p[0]
	                 | ((unsigned long) p[1] << 8)
	                 | ((unsigned long) p[2] << 16)
	                 | ((unsigned long) p[3] << 24);

	return ((val * 2654435761UL) & 0xFFFFFFFFUL) >> (32 - bits);
}

static unsigned long
lz4_literal_cost(unsigned long nlit)
{
	unsigned long cost = 0;

	while (nlit >= 15 + 255) {
		++cost;
		nlit -= 255;
	}
	if (nlit >= 15) {
		++cost;
		nlit = 15;
	}

	return cost;
}

static unsigned long
lz4_match_cost(unsigned long len)
{
	unsigned long cost = 1 + 2;

	while (len >= 19 + 255) {
		++cost;
		len -= 255;
	}
	if (len >= 19) {
		++cost;
		len = 19;
	}

	return cost;
}

unsigned long
lz4_max_packed_size(unsigned long src_size)
{
	return src_size + src_size / 255 + 16;
}

// Include compression algorithms used by lz4_pack_level
#include "lz4_leparse.h"
#include "lz4_ssparse.h"

unsigned long
lz4_workmem_size_level(unsigned long src_size, int level)
{
	switch (level) {
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
		return lz4_leparse_workmem_size(src_size);
	case 10:
		return lz4_ssparse_workmem_size(src_size);
	default:
		return LZ4_ERROR;
	}
}

unsigned long
lz4_pack_level(const void *src, void *dst, unsigned long src_size,
               void *workmem, int level)
{
	switch (level) {
	case 5:
		return lz4_pack_leparse(src, dst, src_size, workmem, 1, 18);
	case 6:
		return lz4_pack_leparse(src, dst, src_size, workmem, 8, 32);
	case 7:
		return lz4_pack_leparse(src, dst, src_size, workmem, 64, 64);
	case 8:
		return lz4_pack_leparse(src, dst, src_size, workmem, 512, 128);
	case 9:
		return lz4_pack_leparse(src, dst, src_size, workmem, 4096, 256);
	case 10:
		return lz4_pack_ssparse(src, dst, src_size, workmem, ULONG_MAX, ULONG_MAX);
	default:
		return LZ4_ERROR;
	}
}

// clang -g -O1 -fsanitize=fuzzer,address -DLZ4_FUZZING lz4.c depack.c
#if defined(LZ4_FUZZING)
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef LZ4_FUZZ_LEVEL
#  define LZ4_FUZZ_LEVEL 5
#endif

extern int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size > 8 * 1024 * 1024UL) { return 0; }
	void *workmem = malloc(lz4_workmem_size_level(size, LZ4_FUZZ_LEVEL));
	void *packed = malloc(lz4_max_packed_size(size));
	void *depacked = malloc(size);
	if (!workmem || !packed || !depacked) { abort(); }
	unsigned long packed_size = lz4_pack_level(data, packed, size, workmem, LZ4_FUZZ_LEVEL);
	lz4_depack(packed, depacked, packed_size);
	if (memcmp(data, depacked, size)) { abort(); }
	free(depacked);
	free(packed);
	free(workmem);
	return 0;
}
#endif
