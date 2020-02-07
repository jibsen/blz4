//
// blz4 - Example of LZ4 compression with BriefLZ algorithms
//
// Forwards dynamic programming parse using binary trees
//
// Copyright (c) 2020 Joergen Ibsen
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

#ifndef LZ4_BTPARSE_H_INCLUDED
#define LZ4_BTPARSE_H_INCLUDED

static size_t
lz4_btparse_workmem_size(size_t src_size)
{
	return (5 * src_size + 3 + LOOKUP_SIZE) * sizeof(uint32_t);
}

// Forwards dynamic programming parse using binary trees, checking all
// possible matches.
//
// The match search uses a binary tree for each hash entry, which is updated
// dynamically as it is searched by re-rooting the tree at the search string.
//
// This does not result in balanced trees on all inputs, but often works well
// in practice, and has the advantage that we get the matches in order from
// closest and back.
//
// A drawback is the memory requirement of 5 * src_size words, since we cannot
// overlap the arrays in a forwards parse.
//
// This match search method is found in LZMA by Igor Pavlov, libdeflate
// by Eric Biggers, and other libraries.
//
static unsigned long
lz4_pack_btparse(const void *src, void *dst, unsigned long src_size, void *workmem,
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

	uint32_t *const cost = (uint32_t *) workmem;
	uint32_t *const mpos = cost + src_size + 1;
	uint32_t *const mlen = mpos + src_size + 1;
	uint32_t *const nodes = mlen + src_size + 1;
	uint32_t *const lookup = nodes + 2 * src_size;

	// Initialize lookup
	for (unsigned long i = 0; i < LOOKUP_SIZE; ++i) {
		lookup[i] = NO_MATCH_POS;
	}

	// Initialize to all literals with infinite cost
	for (unsigned long i = 0; i <= src_size; ++i) {
		cost[i] = UINT32_MAX;
		mlen[i] = 1;
		mpos[i] = 0;
	}

	cost[0] = 0;

	// Next position where we are going to check matches
	//
	// This is used to skip matching while still updating the trees when
	// we find a match that is accept_len or longer.
	//
	unsigned long next_match_cur = 0;

	// Phase 1: Find lowest cost path arriving at each position
	for (unsigned long cur = 0; cur <= last_match_pos; ++cur) {
		// Check literal
		//
		// For literals, we store the number of literals up to the
		// current position in mpos. This is used to update the cost
		// from the current position with the additional cost of
		// encoding the length of this run of literals in the next
		// match.
		//
		if (mlen[cur] == 1) {
			unsigned long literals_cost = 1 + lz4_literal_cost(mpos[cur] + 1) - lz4_literal_cost(mpos[cur]);

			if (cost[cur + 1] > cost[cur] + literals_cost) {
				cost[cur + 1] = cost[cur] + literals_cost;
				mlen[cur + 1] = 1;
				mpos[cur + 1] = mpos[cur] + 1;
			}
		}
		else {
			if (cost[cur + 1] > cost[cur] + 1) {
				cost[cur + 1] = cost[cur] + 1;
				mlen[cur + 1] = 1;
				mpos[cur + 1] = 1;
			}
		}

		if (cur > next_match_cur) {
			next_match_cur = cur;
		}

		unsigned long max_len = 3;
		unsigned long max_len_pos = NO_MATCH_POS;

		// Look up first match for current position
		//
		// pos is the current root of the tree of strings with this
		// hash. We are going to re-root the tree so cur becomes the
		// new root.
		//
		const unsigned long hash = lz4_hash4_bits(&in[cur], LZ4_HASH_BITS);
		unsigned long pos = lookup[hash];
		lookup[hash] = cur;

		uint32_t *lt_node = &nodes[2 * cur];
		uint32_t *gt_node = &nodes[2 * cur + 1];
		unsigned long lt_len = 0;
		unsigned long gt_len = 0;

		assert(pos == NO_MATCH_POS || pos < cur);

		// If we are checking matches, allow lengths up to end of
		// input, otherwise compare only up to accept_len
		const unsigned long len_limit = cur == next_match_cur ? src_size - cur - 5
		                              : accept_len < src_size - cur - 5 ? accept_len
		                              : src_size - cur - 5;
		unsigned long num_chain = max_depth;

		// Check matches
		for (;;) {
			// If at bottom of tree, mark leaf nodes
			//
			// In case we reached max_depth, this also prunes the
			// subtree we have not searched yet and do not know
			// where belongs.
			//
			if (pos == NO_MATCH_POS || cur - pos > 65535 || num_chain-- == 0) {
				*lt_node = NO_MATCH_POS;
				*gt_node = NO_MATCH_POS;

				break;
			}

			// The string at pos is lexicographically greater than
			// a string that matched in the first lt_len positions,
			// and less than a string that matched in the first
			// gt_len positions, so it must match up to at least
			// the minimum of these.
			unsigned long len = lt_len < gt_len ? lt_len : gt_len;

			// Find match len
			while (len < len_limit && in[pos + len] == in[cur + len]) {
				++len;
			}

			// Update longest match found
			if (cur == next_match_cur && len > max_len) {
				max_len = len;
				max_len_pos = pos;

				if (len >= accept_len) {
					next_match_cur = cur + len;
				}
			}

			// If we reach maximum match length, the string at pos
			// is equal to cur, so we can assign the left and right
			// subtrees.
			//
			// This removes pos from the tree, but we added cur
			// which is equal and closer for future matches.
			//
			if (len >= accept_len || len == len_limit) {
				*lt_node = nodes[2 * pos];
				*gt_node = nodes[2 * pos + 1];

				break;
			}

			// Go to previous match and restructure tree
			//
			// lt_node points to a node that is going to contain
			// elements lexicographically less than cur (the search
			// string).
			//
			// If the string at pos is less than cur, we set that
			// lt_node to pos. We know that all elements in the
			// left subtree are less than pos, and thus less than
			// cur, so we point lt_node at the right subtree of
			// pos and continue our search there.
			//
			// The equivalent applies to gt_node when the string at
			// pos is greater than cur.
			//
			if (in[pos + len] < in[cur + len]) {
				*lt_node = pos;
				lt_node = &nodes[2 * pos + 1];
				assert(*lt_node == NO_MATCH_POS || *lt_node < pos);
				pos = *lt_node;
				lt_len = len;
			}
			else {
				*gt_node = pos;
				gt_node = &nodes[2 * pos];
				assert(*gt_node == NO_MATCH_POS || *gt_node < pos);
				pos = *gt_node;
				gt_len = len;
			}
		}

		// Update costs for longest match found
		//
		// If the match is longer than 18, decreasing the match length
		// by up to 255 will result in saving 1 byte on the match
		// length encoding.
		//
		// On the other hand, the best case is that the following
		// sequence is a match that can be extended to the left to
		// cover the bytes we no longer match, which increases the
		// match length of that match. We can do this at most 254
		// times before its match length encoding goes up 1 byte.
		//
		// So we only have to check the last 255 posssible match
		// lengths.
		//
		// This optimization is from lz4x by Ilya Muravyov.
		//
		if (max_len_pos != NO_MATCH_POS) {
			unsigned long min_len = max_len > (254 + 4) ? max_len - 254 : 4;

			for (unsigned long i = min_len; i <= max_len; ++i) {
				unsigned long match_cost = lz4_match_cost(i);

				assert(match_cost < UINT32_MAX - cost[cur]);

				unsigned long cost_there = cost[cur] + match_cost;

				// If the choice is between a literal and a
				// match with the same cost, choose the match.
				// This is because the match is able to encode
				// any literals preceding it.
				if (cost_there < cost[cur + i]
				 || (mlen[cur + i] == 1 && cost_there == cost[cur + i])) {
					cost[cur + i] = cost_there;
					mpos[cur + i] = max_len_pos;
					mlen[cur + i] = i;
				}
			}
		}
	}

	for (unsigned long cur = last_match_pos + 1; cur < src_size; ++cur) {
		// Check literal
		if (mlen[cur] == 1) {
			unsigned long literals_cost = 1 + lz4_literal_cost(mpos[cur] + 1) - lz4_literal_cost(mpos[cur]);

			if (cost[cur + 1] > cost[cur] + literals_cost) {
				cost[cur + 1] = cost[cur] + literals_cost;
				mlen[cur + 1] = 1;
				mpos[cur + 1] = mpos[cur] + 1;
			}
		}
		else {
			if (cost[cur + 1] > cost[cur] + 1) {
				cost[cur + 1] = cost[cur] + 1;
				mlen[cur + 1] = 1;
				mpos[cur + 1] = 1;
			}
		}
	}

	// Phase 2: Follow lowest cost path backwards gathering tokens
	unsigned long next_token = src_size;

	for (unsigned long cur = src_size; cur > 0; cur -= mlen[cur], --next_token) {
		mlen[next_token] = mlen[cur];
		mpos[next_token] = mpos[cur];
	}

	// Phase 3: Output tokens
	unsigned char *out = (unsigned char *) dst;

	unsigned long cur = 0;

	for (unsigned long i = next_token + 1; i <= src_size; cur += mlen[i++]) {
		unsigned long next_lit = cur;
		unsigned long nlit = 0;

		// Move over literals, counting them
		while (i <= src_size && mlen[i] == 1) {
			++nlit;
			++i;
			++cur;
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
		while (next_lit < cur) {
			*out++ = in[next_lit++];
		}

		// Handle last incomplete sequence
		if (i > src_size) {
			// Write token
			*token_out = nlit << 4;
			break;
		}

		// Output offset
		unsigned long offs = mlen[i] == 1 ? 1 : cur - mpos[i];

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

#endif /* LZ4_BTPARSE_H_INCLUDED */
