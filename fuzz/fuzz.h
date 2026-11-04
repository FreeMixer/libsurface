// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* What every libFuzzer harness in fuzz/ shares: a cursor over the input, the input-size
 * refusal, the numbers the harnesses invent, and the exact-size copy that lets the sanitizer
 * see a read one byte past a string.
 *
 * The cursor is TOTAL. Reading past the end of the input yields zeros and an empty slice,
 * never a failure, so every input decodes to some sequence of calls and the end of the input
 * ends the sequence. Nothing here reads a clock, the environment, a file or the network, and
 * nothing allocates except fuzz_dup. */
#ifndef SURFACE_FUZZ_H
#define SURFACE_FUZZ_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "spec.h"

/* The OSS Verify driver refuses an input file this large; the harnesses refuse it too, so
 * the domain they are proven over is the domain they are run over. */
#define FUZZ_INPUT_MAX ((size_t)64 << 20)

/* A property check: a function the sabotage rule can name (fuzz/sabotage.sh finds the
 * innermost check_ function of an abort's stack), so it is never inlined away. */
#define FUZZ_CHECK __attribute__((noinline)) static void

struct fuzz_in {
	const uint8_t *p;
	size_t n;
};

static inline struct fuzz_in fuzz_in(const uint8_t *data, size_t size)
{
	struct fuzz_in in = { data, size };
	return in;
}

static inline uint8_t fuzz_u8(struct fuzz_in *in)
{
	if (in->n == 0)
		return 0;
	in->n--;
	return *in->p++;
}

static inline uint16_t fuzz_u16(struct fuzz_in *in)
{
	uint16_t lo = fuzz_u8(in);
	return (uint16_t)(lo | (uint16_t)fuzz_u8(in) << 8);
}

static inline uint32_t fuzz_u32(struct fuzz_in *in)
{
	uint32_t lo = fuzz_u16(in);
	return lo | (uint32_t)fuzz_u16(in) << 16;
}

/* Whether the input is used up: the end of the input ends the program it is. */
static inline int fuzz_done(const struct fuzz_in *in)
{
	return in->n == 0;
}

/* A heap block of exactly `n` bytes (one for n = 0). A string handed to the library lives in
 * its own allocation, so a read past its end lands in a redzone the sanitizer watches instead
 * of in the next record of the same input. */
static inline uint8_t *fuzz_alloc(size_t n)
{
	uint8_t *b = malloc(n ? n : 1);
	/* not a property: the harness's own allocation failed */
	if (!b)
		abort();
	return b;
}

static inline uint8_t *fuzz_dup(const uint8_t *p, size_t n)
{
	uint8_t *b = fuzz_alloc(n);
	if (n)
		memcpy(b, p, n);
	return b;
}

/* A NUL-terminated string of up to `max` bytes the input holds (a length byte, then that
 * many bytes), in an allocation of exactly its size. The caller frees it. */
static inline char *fuzz_str(struct fuzz_in *in, size_t max)
{
	size_t want = fuzz_u8(in);
	size_t n = want < max ? want : max;
	if (n > in->n)
		n = in->n;
	char *s = (char *)fuzz_alloc(n + 1);
	if (n)
		memcpy(s, in->p, n);
	s[n] = 0;
	in->p += n;
	in->n -= n;
	return s;
}

/* A number for the library's double parameters. The first byte picks a value that is the
 * edge of some law (zero, one, minus one, a half, NaN, the infinities, huge and tiny
 * magnitudes) or a fraction n/d, which lands a level exactly on a rounding edge. */
static inline double fuzz_double(struct fuzz_in *in)
{
	static const double edge[] = {
		0.0, 1.0, -1.0, 0.5, -0.5, 0.25, 0.75, 2.0, -2.0, 1e300, -1e300, 1e-300,
		0.999999, 1.000001, -0.999999, -1.000001,
	};
	uint8_t b = fuzz_u8(in);
	if (b < 16)
		return edge[b];
	if (b == 16)
		return __builtin_nan("");
	if (b == 17)
		return __builtin_inf();
	if (b == 18)
		return -__builtin_inf();
	if (b < 128) {
		unsigned n = fuzz_u8(in), d = 1u + fuzz_u8(in);
		double v = (double)n / d;
		return b & 1 ? -v : v;
	}
	return (double)(int16_t)fuzz_u16(in) / 8192.0;
}

/* The time the caller's clock reads: a start the input chooses, then steps the input
 * chooses. Never goes back, and never nears the wrap of 64 bits. */
static inline uint64_t fuzz_step(struct fuzz_in *in, uint64_t now)
{
	uint8_t b = fuzz_u8(in);
	uint64_t unit = b & 3 ? (b & 3) == 1 ? 1000ull : (b & 3) == 2 ? 1000000ull : 100000ull : 1ull;
	return now + (uint64_t)(b >> 2) * unit;
}

#endif /* SURFACE_FUZZ_H */
