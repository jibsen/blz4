/*
 * blz4 - Example of LZ4 compression with BriefLZ algorithms
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

#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#  define _CRT_DISABLE_PERFCRIT_LOCKS
#else
#  define _FILE_OFFSET_BITS 64
#  define _ftelli64 ftello64
#endif

#ifdef __MINGW32__
#  define __USE_MINGW_ANSI_STDIO 1
#endif

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "lz4.h"
#include "parg.h"

#define LZ4_LEGACY_MAGIC (0x184C2102UL)

/*
 * The default block size used to process data.
 */
#ifndef BLOCK_SIZE
#  define BLOCK_SIZE (8 * 1024 * 1024UL)
#endif

/*
 * Unsigned char type.
 */
typedef unsigned char byte;

/*
 * Get the low-order 8 bits of a value.
 */
#if CHAR_BIT == 8
#  define octet(v) ((byte) (v))
#else
#  define octet(v) ((v) & 0x00FF)
#endif

/*
 * Store a 32-bit unsigned value in little-endian order.
 */
static void
write_le32(byte *p, unsigned long val)
{
	p[0] = octet(val);
	p[1] = octet(val >> 8);
	p[2] = octet(val >> 16);
	p[3] = octet(val >> 24);
}

/*
 * Read a 32-bit unsigned value in little-endian order.
 */
static unsigned long
read_le32(const byte *p)
{
	return ((unsigned long) octet(p[0]))
	     | ((unsigned long) octet(p[1]) << 8)
	     | ((unsigned long) octet(p[2]) << 16)
	     | ((unsigned long) octet(p[3]) << 24);
}

static unsigned int
ratio(long long x, long long y)
{
	if (x <= LLONG_MAX / 100) {
		x *= 100;
	}
	else {
		y /= 100;
	}

	if (y == 0) {
		y = 1;
	}

	return (unsigned int) (x / y);
}

static void
printf_error(const char *fmt, ...)
{
	va_list arg;

	fputs("blz4: ", stderr);

	va_start(arg, fmt);
	vfprintf(stderr, fmt, arg);
	va_end(arg);

	fputs("\n", stderr);
}

static void
printf_usage(const char *fmt, ...)
{
	va_list arg;

	fputs("blz4: ", stderr);

	va_start(arg, fmt);
	vfprintf(stderr, fmt, arg);
	va_end(arg);

	fputs("\n"
	      "usage: blz4 [-56789 | --optimal] [-v] INFILE OUTFILE\n"
	      "       blz4 -d [-v] INFILE OUTFILE\n"
	      "       blz4 -V | --version\n"
	      "       blz4 -h | --help\n", stderr);
}

static int
compress_file(const char *oldname, const char *packedname, int be_verbose,
              int level)
{
	const byte lz4_magic[4] = { 0x02, 0x21, 0x4C, 0x18 };
	byte header[4];
	FILE *oldfile = NULL;
	FILE *packedfile = NULL;
	byte *data = NULL;
	byte *packed = NULL;
	byte *workmem = NULL;
	long long insize = 0, outsize = 0;
	static const char rotator[] = "-\\|/";
	unsigned int counter = 0;
	size_t n_read;
	clock_t clocks;
	int res = 1;

	/* Allocate memory */
	if ((data = (byte *) malloc(BLOCK_SIZE)) == NULL
	 || (packed = (byte *) malloc(lz4_max_packed_size(BLOCK_SIZE))) == NULL
	 || (workmem = (byte *) malloc(lz4_workmem_size_level(BLOCK_SIZE, level))) == NULL) {
		printf_error("not enough memory");
		goto out;
	}

	/* Open input file */
	if ((oldfile = fopen(oldname, "rb")) == NULL) {
		printf_usage("unable to open input file '%s'", oldname);
		goto out;
	}

	/* Create output file */
	if ((packedfile = fopen(packedname, "wb")) == NULL) {
		printf_usage("unable to open output file '%s'", packedname);
		goto out;
	}

	clocks = clock();

	/* Write LZ4 header magic */
	fwrite(lz4_magic, 1, sizeof(lz4_magic), packedfile);
	outsize += sizeof(lz4_magic);

	/* While we are able to read data from input file .. */
	while ((n_read = fread(data, 1, BLOCK_SIZE, oldfile)) > 0) {
		size_t packedsize;

		/* Show a little progress indicator */
		if (be_verbose) {
			fprintf(stderr, "%c\r", rotator[counter]);
			counter = (counter + 1) & 0x03;
		}

		/* Compress data block */
		packedsize = lz4_pack_level(data, packed, (unsigned long) n_read,
		                            workmem, level);

		/* Check for compression error */
		if (packedsize == 0) {
			printf_error("an error occured while compressing");
			goto out;
		}

		/* Put block-specific values into header */
		write_le32(header, (unsigned long) packedsize);

		/* Write header and compressed data */
		fwrite(header, 1, sizeof(header), packedfile);
		fwrite(packed, 1, packedsize, packedfile);

		/* Sum input and output size */
		insize += n_read;
		outsize += packedsize + sizeof(header);
	}

	clocks = clock() - clocks;

	/* Show result */
	if (be_verbose) {
		fprintf(stderr, "in %lld out %lld ratio %u%% time %.2f\n",
		        insize, outsize, ratio(outsize, insize),
		        (double) clocks / (double) CLOCKS_PER_SEC);
	}

	res = 0;

out:
	/* Close files */
	if (packedfile != NULL) {
		fclose(packedfile);
	}
	if (oldfile != NULL) {
		fclose(oldfile);
	}

	/* Free memory */
	if (workmem != NULL) {
		free(workmem);
	}
	if (packed != NULL) {
		free(packed);
	}
	if (data != NULL) {
		free(data);
	}

	return res;
}

static int
decompress_file(const char *packedname, const char *newname, int be_verbose)
{
	byte header[4];
	FILE *newfile = NULL;
	FILE *packedfile = NULL;
	byte *data = NULL;
	byte *packed = NULL;
	long long insize = 0, outsize = 0;
	static const char rotator[] = "-\\|/";
	unsigned int counter = 0;
	clock_t clocks;
	size_t max_packed_size;
	int res = 1;

	max_packed_size = lz4_max_packed_size(BLOCK_SIZE);

	/* Allocate memory */
	if ((data = (byte *) malloc(BLOCK_SIZE)) == NULL
	 || (packed = (byte *) malloc(max_packed_size)) == NULL) {
		printf_error("not enough memory");
		goto out;
	}

	/* Open input file */
	if ((packedfile = fopen(packedname, "rb")) == NULL) {
		printf_usage("unable to open input file '%s'", packedname);
		goto out;
	}

	/* Create output file */
	if ((newfile = fopen(newname, "wb")) == NULL) {
		printf_usage("unable to open output file '%s'", newname);
		goto out;
	}

	clocks = clock();

	/* Read LZ4 header magic */
	if (fread(header, 1, sizeof(header), packedfile) != sizeof(header)) {
		printf_error("unable to read LZ4 header magic");
		goto out;
	}

	/* Check header is LZ4 legacy magic */
	if (read_le32(header) != LZ4_LEGACY_MAGIC) {
		printf_error("LZ4 header magic mismatch");
		goto out;
	}

	/* While we are able to read a header from input file .. */
	while (fread(header, 1, sizeof(header), packedfile) == sizeof(header)) {
		size_t hdr_packedsize, depackedsize;

		/* Show a little progress indicator */
		if (be_verbose) {
			fprintf(stderr, "%c\r", rotator[counter]);
			counter = (counter + 1) & 0x03;
		}

		/* Get compressed size from header */
		hdr_packedsize = (size_t) read_le32(header);

		/* If header is LZ4 magic value, assume new frame */
		if (hdr_packedsize == LZ4_LEGACY_MAGIC) {
			insize += sizeof(header);
			continue;
		}

		/* Check buffer is sufficient */
		if (hdr_packedsize > max_packed_size) {
			printf_error("compressed size in header too large");
			goto out;
		}

		/* Read compressed data */
		if (fread(packed, 1, hdr_packedsize, packedfile) != hdr_packedsize) {
			printf_error("error reading block from compressed file");
			goto out;
		}

		/* Decompress data */
		depackedsize = lz4_depack(packed, data,
		                          (unsigned long) hdr_packedsize);

		/* Check for decompression error */
		if (depackedsize == LZ4_ERROR) {
			printf_error("an error occured while decompressing");
			goto out;
		}

		/* Write decompressed data */
		fwrite(data, 1, depackedsize, newfile);

		/* Sum input and output size */
		insize += hdr_packedsize + sizeof(header);
		outsize += depackedsize;
	}

	clocks = clock() - clocks;

	/* Show result */
	if (be_verbose) {
		fprintf(stderr, "in %lld out %lld ratio %u%% time %.2f\n",
		        insize, outsize, ratio(insize, outsize),
		        (double) clocks / (double) CLOCKS_PER_SEC);
	}

	res = 0;

out:
	/* Close files */
	if (packedfile != NULL) {
		fclose(packedfile);
	}
	if (newfile != NULL) {
		fclose(newfile);
	}

	/* Free memory */
	if (packed != NULL) {
		free(packed);
	}
	if (data != NULL) {
		free(data);
	}

	return res;
}

static void
print_syntax(void)
{
	fputs("usage: blz4 [options] INFILE OUTFILE\n"
	      "\n"
	      "options:\n"
	      "  -5                     compress faster (default)\n"
	      "  -9                     compress better\n"
	      "      --optimal          optimal but very slow compression\n"
	      "  -d, --decompress       decompress\n"
	      "  -h, --help             print this help and exit\n"
	      "  -v, --verbose          verbose mode\n"
	      "  -V, --version          print version and exit\n"
	      "\n"
	      "PLEASE NOTE: This is an experiment, use at your own risk.\n", stdout);
}

static void
print_version(void)
{
	fputs("blz4 " LZ4_VER_STRING "\n"
	      "\n"
	      "Copyright (c) 2018 Joergen Ibsen\n"
	      "\n"
	      "Licensed under the zlib license (Zlib).\n"
	      "There is NO WARRANTY, to the extent permitted by law.\n", stdout);
}

int
main(int argc, char *argv[])
{
	struct parg_state ps;
	const char *infile = NULL;
	const char *outfile = NULL;
	int flag_decompress = 0;
	int flag_verbose = 0;
	int level = 5;
	int c;

	const struct parg_option long_options[] = {
		{ "decompress", PARG_NOARG, NULL, 'd' },
		{ "help", PARG_NOARG, NULL, 'h' },
		{ "optimal", PARG_NOARG, NULL, 'x' },
		{ "verbose", PARG_NOARG, NULL, 'v' },
		{ "version", PARG_NOARG, NULL, 'V' },
		{ 0, 0, 0, 0 }
	};

	parg_init(&ps);

	while ((c = parg_getopt_long(&ps, argc, argv, "56789dhvVx", long_options, NULL)) != -1) {
		switch (c) {
		case 1:
			if (infile == NULL) {
				infile = ps.optarg;
			}
			else if (outfile == NULL) {
				outfile = ps.optarg;
			}
			else {
				printf_usage("too many arguments");
				return EXIT_FAILURE;
			}
			break;
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			level = c - '0';
			break;
		case 'x':
			level = 10;
			break;
		case 'd':
			flag_decompress = 1;
			break;
		case 'h':
			print_syntax();
			return EXIT_SUCCESS;
			break;
		case 'v':
			flag_verbose = 1;
			break;
		case 'V':
			print_version();
			return EXIT_SUCCESS;
			break;
		default:
			printf_usage("unknown option '%s'", argv[ps.optind - 1]);
			return EXIT_FAILURE;
			break;
		}
	}

	if (outfile == NULL) {
		printf_usage("too few arguments");
		return EXIT_FAILURE;
	}

	if (flag_decompress) {
		return decompress_file(infile, outfile, flag_verbose);
	}
	else {
		return compress_file(infile, outfile, flag_verbose, level);
	}

	return EXIT_SUCCESS;
}
