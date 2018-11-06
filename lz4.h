/*
 * blz4 - Example of LZ4 compression with BriefLZ algorithms
 *
 * C/C++ header file
 *
 * Copyright (c) 2018 Joergen Ibsen
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 *   1. The origin of this software must not be misrepresented; you must
 *      not claim that you wrote the original software. If you use this
 *      software in a product, an acknowledgment in the product
 *      documentation would be appreciated but is not required.
 *
 *   2. Altered source versions must be plainly marked as such, and must
 *      not be misrepresented as being the original software.
 *
 *   3. This notice may not be removed or altered from any source
 *      distribution.
 */

#ifndef LZ4_H_INCLUDED
#define LZ4_H_INCLUDED

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LZ4_VER_MAJOR 0        /**< Major version number */
#define LZ4_VER_MINOR 1        /**< Minor version number */
#define LZ4_VER_PATCH 0        /**< Patch version number */
#define LZ4_VER_STRING "0.1.0" /**< Version number as a string */

#ifdef LZ4_DLL
#  if defined(_WIN32) || defined(__CYGWIN__)
#    ifdef LZ4_DLL_EXPORTS
#      define LZ4_API __declspec(dllexport)
#    else
#      define LZ4_API __declspec(dllimport)
#    endif
#    define LZ4_LOCAL
#  else
#    if __GNUC__ >= 4
#      define LZ4_API __attribute__ ((visibility ("default")))
#      define LZ4_LOCAL __attribute__ ((visibility ("hidden")))
#    else
#      define LZ4_API
#      define LZ4_LOCAL
#    endif
#  endif
#else
#  define LZ4_API
#  define LZ4_LOCAL
#endif

/**
 * Return value on error.
 */
#ifndef LZ4_ERROR
#  define LZ4_ERROR ((unsigned long) (-1))
#endif

/**
 * Get bound on compressed data size.
 *
 * @see lz4_pack_level
 *
 * @param src_size number of bytes to compress
 * @return maximum size of compressed data
 */
LZ4_API unsigned long
lz4_max_packed_size(unsigned long src_size);

/**
 * Get required size of `workmem` buffer.
 *
 * @see lz4_pack_level
 *
 * @param src_size number of bytes to compress
 * @param level compression level
 * @return required size in bytes of `workmem` buffer
 */
LZ4_API unsigned long
lz4_workmem_size_level(unsigned long src_size, int level);

/**
 * Compress `src_size` bytes of data from `src` to `dst`.
 *
 * Compression levels between 5 and 9 offer a trade-off between
 * time/space and ratio. Level 10 is optimal but very slow.
 *
 * @param src pointer to data
 * @param dst pointer to where to place compressed data
 * @param src_size number of bytes to compress
 * @param workmem pointer to memory for temporary use
 * @param level compression level
 * @return size of compressed data
 */
LZ4_API unsigned long
lz4_pack_level(const void *src, void *dst, unsigned long src_size,
               void *workmem, int level);

/**
 * Decompress data from `src` to `dst`.
 *
 * @param src pointer to compressed data
 * @param dst pointer to where to place decompressed data
 * @param packed_size size of compressed data
 * @return size of decompressed data
 */
LZ4_API unsigned long
lz4_depack(const void *src, void *dst, unsigned long packed_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LZ4_H_INCLUDED */
