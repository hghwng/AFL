#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"

u32 MAP_SIZE = 1 << 16;  // 64K by default.
static u16 count_class_lookup16[65536];
u8* trace_bits; /* SHM with instrumentation bitmap  */

static const u8 simplify_lookup[256] = {
    [0] = 1, [1 ... 255] = 128
};

static const u8 count_class_lookup8[256] = {[0] = 0,
                                            [1] = 1,
                                            [2] = 2,
                                            [3] = 4,
                                            [4 ... 7] = 8,
                                            [8 ... 15] = 16,
                                            [16 ... 31] = 32,
                                            [32 ... 127] = 64,
                                            [128 ... 255] = 128

};

#include "coverage-64.h"

void init_count_class16(void) {
  u32 b1, b2;

  for (b1 = 0; b1 < 256; b1++)
    for (b2 = 0; b2 < 256; b2++)
      count_class_lookup16[(b1 << 8) + b2] =
          (count_class_lookup8[b1] << 8) | count_class_lookup8[b2];
}

static inline u8 has_new_bits(u8* virgin_map) {
#ifdef WORD_SIZE_64

  u64* current = (u64*)trace_bits;
  u64* virgin = (u64*)virgin_map;

  u32 i = (MAP_SIZE >> 3);

#else

  u32* current = (u32*)trace_bits;
  u32* virgin = (u32*)virgin_map;

  u32 i = (MAP_SIZE >> 2);

#endif /* ^WORD_SIZE_64 */

  u8 ret = 0;

  while (i--) {
    if (unlikely(*current)) discover_word(&ret, current, virgin);

    current++;
    virgin++;
  }

  return ret;
}

static int slow = 0;

static inline u8 has_new_bits_unclassified(u8* virgin_map) {
  /* Handle the hot path first: no new coverage */
  u8* end = trace_bits + MAP_SIZE;

  if (!skim((u64*)virgin_map, (u64*)trace_bits, (u64*)end)) return 0;
  ++slow;

  classify_counts(trace_bits);
  return has_new_bits(virgin_map);
}

u8* read_trace(int i) {
  char path[200];
  sprintf(path, "./test/feedback/%d", i++);
  FILE* f = fopen(path, "rb");
  if (f == 0) return 0;

  u8* p = aligned_alloc(32, MAP_SIZE);
  fread(p, MAP_SIZE, 1, f);
  fclose(f);
  return p;
}

int main() {
  srand(0);
  init_count_class16();
  trace_bits = aligned_alloc(64, MAP_SIZE);
  u8* virgin_bits = aligned_alloc(64, MAP_SIZE);

  // num, repeat, hash
  u64 params[3] = {30, 25614, 0xf4619044d279bc23}; // Realistic, 1s
  /* u64 params[3] = {2000, 2500, 0xd2aa814a64d4c444};  // All items, 3.5s */

  u8* ptrs[2000] = {0};
  for (int i = 0; i < params[0]; ++i) {
    ptrs[i] = read_trace(i);
    if (ptrs[i] == 0) break;
  }

  u64 hash = 0;
  memset(virgin_bits, 255, MAP_SIZE);
  for (int i = 0; ptrs[i]; ++i) {
    memcpy(trace_bits, ptrs[i], MAP_SIZE);
    trace_bits[0] = 0;
    for (int round = 0; round < params[1]; ++round) {
#ifdef SLOW
      classify_counts(trace_bits);
      u64 v = has_new_bits(virgin_bits);
#else
      u64 v = has_new_bits_unclassified(virgin_bits);
#endif
      hash = hash * 3 + v;
    }
  }
  printf("hash %llx (%s), slow = %d\n", hash, params[2] == hash ? "same" : "mismatch", slow);
}
