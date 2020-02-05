//
// blz4 - Example of LZ4 compression with BriefLZ algorithms
//
// Backwards dynamic programming parse with left-extension of matches
//
// Copyright (c) 2018-2020 Joergen Ibsen
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

#ifndef LZ4_LEPARSE_H_INCLUDED
#define LZ4_LEPARSE_H_INCLUDED

static size_t
lz4_leparse_workmem_size(size_t src_size)
{
	return (LOOKUP_SIZE < 2 * src_size ? 3 * src_size : src_size + LOOKUP_SIZE)
	     * sizeof(unsigned long);
}

static unsigned long
lz4_pack_leparse(const void *src, void *dst, unsigned long src_size, void *workmem,
                 const unsigned long max_depth, const unsigned long accept_len)
{
	const unsigned char *const in = (const unsigned char *) src;
	const unsigned long last_match_pos = src_size > 12 ? src_size - 12 : 0;

	// Check for empty input
	if (src_size == 0) {
		unsigned char *out = (unsigned char *) dst;
		*out++ = 0;
		return 1;
	}

	// Check for input without room for match
	if (src_size < 13) {
		unsigned char *out = (unsigned char *) dst;
		*out++ = src_size << 4;
		for (unsigned long i = 0; i < src_size; ++i) {
			*out++ = in[i];
		}
		return 1 + src_size;
	}

	// With a bit of careful ordering we can fit in 3 * src_size words.
	//
	// The idea is that the lookup is only used in the first phase to
	// build the hash chains, so we overlap it with mpos and mlen.
	// Also, since we are using prev from right to left in phase two,
	// and that is the order we fill in cost, we can overlap these.
	//
	// One detail is that we actually use src_size + 1 elements of cost,
	// but we put mpos after it, where we do not need the first element.
	//
	unsigned long *const prev = (unsigned long *) workmem;
	unsigned long *const mpos = prev + src_size;
	unsigned long *const mlen = mpos + src_size;
	unsigned long *const cost = prev;
	unsigned long *const lookup = mpos;

	// Phase 1: Build hash chains
	const int bits = 2 * src_size < LOOKUP_SIZE ? LZ4_HASH_BITS : lz4_log2(src_size);

	// Initialize lookup
	for (unsigned long i = 0; i < (1UL << bits); ++i) {
		lookup[i] = NO_MATCH_POS;
	}

	// Build hash chains in prev
	if (last_match_pos > 0) {
		for (unsigned long i = 0; i <= last_match_pos; ++i) {
			const unsigned long hash = lz4_hash4_bits(&in[i], bits);
			prev[i] = lookup[hash];
			lookup[hash] = i;
		}
	}

	// Initialize last eleven positions as literals
	for (unsigned long i = 1; i < 12; ++i) {
		mlen[src_size - i] = 1;
		mpos[src_size - i] = i;
		cost[src_size - i] = i;
	}
	cost[src_size] = 0;

	// Phase 2: Find lowest cost path from each position to end
	for (unsigned long cur = last_match_pos; cur > 0; --cur) {
		// Since we updated prev to the end in the first phase, we
		// do not need to hash, but can simply look up the previous
		// position directly.
		unsigned long pos = prev[cur];

		assert(pos == NO_MATCH_POS || pos < cur);

		// Start with a literal
		//
		// We store the number of literals from the current position
		// up to the next match in mpos. This is used to update the
		// cost from the current position with the additional cost of
		// encoding the length of this run of literals in the next
		// match.
		//
		if (mlen[cur + 1] == 1) {
			cost[cur] = 1 + cost[cur + 1] - lz4_literal_cost(mpos[cur + 1]) + lz4_literal_cost(mpos[cur + 1] + 1);
			mlen[cur] = 1;
			mpos[cur] = mpos[cur + 1] + 1;
		}
		else {
			cost[cur] = 1 + cost[cur + 1];
			mlen[cur] = 1;
			mpos[cur] = 1;
		}

		unsigned long max_len = 3;

		const unsigned long len_limit = src_size - cur - 5;
		unsigned long num_chain = max_depth;

		// Go through the chain of prev matches
		for (; pos != NO_MATCH_POS && num_chain--; pos = prev[pos]) {
			if (cur - pos > 65535) {
				break;
			}

			unsigned long len = 0;

			// If next byte matches, so this has a chance to be a longer match
			if (max_len < len_limit && in[pos + max_len] == in[cur + max_len]) {
				// Find match len
				while (len < len_limit && in[pos + len] == in[cur + len]) {
					++len;
				}
			}

			// Extend current match if possible
			//
			// Note that we are checking matches in order from the
			// closest and back. This means for a match further
			// away, the encoding of all lengths up to the current
			// max length will always be longer or equal, so we need
			// only consider the extension.
			if (len > max_len) {
				unsigned long min_cost = ULONG_MAX;
				unsigned long min_cost_len = 3;

				// Find lowest cost match length
				for (unsigned long i = max_len + 1; i <= len; ++i) {
					unsigned long match_cost = lz4_match_cost(i);
					assert(match_cost < ULONG_MAX - cost[cur + i]);
					unsigned long cost_here = match_cost + cost[cur + i];

					if (cost_here < min_cost) {
						min_cost = cost_here;
						min_cost_len = i;
					}
				}

				max_len = len;

				// Update cost if cheaper
				if (min_cost < cost[cur]) {
					cost[cur] = min_cost;
					mpos[cur] = pos;
					mlen[cur] = min_cost_len;

					// Left-extend current match if possible
					if (pos > 0 && in[pos - 1] == in[cur - 1]) {
						do {
							--cur;
							--pos;
							++min_cost_len;
							unsigned long match_cost = lz4_match_cost(min_cost_len);
							assert(match_cost < ULONG_MAX - cost[cur + min_cost_len]);
							unsigned long cost_here = match_cost + cost[cur + min_cost_len];
							cost[cur] = cost_here;
							mpos[cur] = pos;
							mlen[cur] = min_cost_len;
						} while (pos > 0 && in[pos - 1] == in[cur - 1]);
						break;
					}
				}
			}

			if (len >= accept_len || len == len_limit) {
				break;
			}
		}
	}

	mpos[0] = 0;
	mlen[0] = 1;

	unsigned char *out = (unsigned char *) dst;

	// Phase 3: Output compressed data, following lowest cost path
	for (unsigned long i = 0; i < src_size; i += mlen[i]) {
		unsigned long next_lit = i;
		unsigned long nlit = 0;

		// Move over literals, counting them
		while (i < src_size && mlen[i] == 1) {
			++nlit;
			++i;
		}

		// Make room for token
		unsigned char *token_out = out++;

		// Output extra literal length bytes
		while (nlit >= 15 + 255) {
			*out++ = 255;
			nlit -= 255;
		}
		if (nlit >= 15) {
			*out++ = nlit - 15;
			nlit = 15;
		}

		// Output literals
		while (next_lit < i) {
			*out++ = in[next_lit++];
		}

		// Handle last incomplete sequence
		if (i == src_size) {
			// Write token
			*token_out = nlit << 4;
			break;
		}

		// Output offset
		unsigned long offs = mlen[i] == 1 ? 1 : i - mpos[i];

		*out++ = offs & 0xFF;
		*out++ = (offs >> 8) & 0xFF;

		// Output extra length bytes
		unsigned long len = mlen[i];

		while (len >= 19 + 255) {
			*out++ = 255;
			len -= 255;
		}
		if (len >= 19) {
			*out++ = len - 19;
			len = 19;
		}

		// Write token
		*token_out = (nlit << 4) | (len - 4);
	}

	// Return compressed size
	return (unsigned long) (out - (unsigned char *) dst);
}

#endif /* LZ4_LEPARSE_H_INCLUDED */
