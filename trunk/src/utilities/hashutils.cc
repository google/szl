// Copyright 2010 Google Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//      http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>

#include <string>
#include <utility>

#include "public/hash_map.h"

#include "public/porting.h"
#include "public/logging.h"
#include "public/hashutils.h"


//============================================================================
// MD5 hash

void MD5Digest(const void* data, size_t length,
               uint8 (*digest)[MD5_DIGEST_LENGTH]) {
  MD5_CTX md;
  MD5_Init(&md);
  MD5_Update(&md, data, length);
  MD5_Final(*digest, &md);
}


//============================================================================
// Hash implementation


const uint32 kPrimes32[16] ={
  65537, 65539, 65543, 65551, 65557, 65563, 65579, 65581,
  65587, 65599, 65609, 65617, 65629, 65633, 65647, 65651,
};


const uint64 kPrimes64[] ={
  GG_ULONGLONG(4294967311), GG_ULONGLONG(4294967357),
  GG_ULONGLONG(4294967371), GG_ULONGLONG(4294967377),
  GG_ULONGLONG(4294967387), GG_ULONGLONG(4294967389),
  GG_ULONGLONG(4294967459), GG_ULONGLONG(4294967477),
  GG_ULONGLONG(4294967497), GG_ULONGLONG(4294967513),
  GG_ULONGLONG(4294967539), GG_ULONGLONG(4294967543),
  GG_ULONGLONG(4294967549), GG_ULONGLONG(4294967561),
  GG_ULONGLONG(4294967563), GG_ULONGLONG(4294967569)
};


uint32 Hash32StringWithSeed(const char *s, uint32 len, uint32 seed) {
  uint32 n = seed;
  size_t prime1 = 0, prime2 = 8;  // Indices into kPrimes32
  union {
    uint16 n;
    char bytes[sizeof(uint16)];
  } chunk;
  for (const char *i = s, *const end = s + len; i != end; ) {
    chunk.bytes[0] = *i++;
    chunk.bytes[1] = i == end ? 0 : *i++;
    n = n * kPrimes32[prime1++] ^ chunk.n * kPrimes32[prime2++];
    prime1 &= 0x0F;
    prime2 &= 0x0F;
  }
  return n;
}


uint64 Hash64StringWithSeed(const char *s, uint32 len, uint64 seed) {
  uint64 n = seed;
  size_t prime1 = 0, prime2 = 8;  // Indices into kPrimes64
  union {
    uint32 n;
    char bytes[sizeof(uint32)];
  } chunk;
  for (const char *i = s, *const end = s + len; i != end; ) {
    chunk.bytes[0] = *i++;
    chunk.bytes[1] = i == end ? 0 : *i++;
    chunk.bytes[2] = i == end ? 0 : *i++;
    chunk.bytes[3] = i == end ? 0 : *i++;
    n = n * kPrimes64[prime1++] ^ chunk.n * kPrimes64[prime2++];
    prime1 &= 0x0F;
    prime2 &= 0x0F;
  }
  return n;
}


uint32 Hash32NumWithSeed(uint32 num, uint32 seed) {
  // Same (except byte order) as if called Hash32StringWithSeed.
  uint32 n = seed;
  n = n * kPrimes32[0] ^ uint32(num << 16) * kPrimes32[8];
  n = n * kPrimes32[1] ^ uint32(num >> 16) * kPrimes32[9];
  return n;
}


uint64 Hash64NumWithSeed(uint64 num, uint64 seed) {
  // Same (except byte order) as if called Hash64StringWithSeed.
  uint64 n = seed;
  n = n * kPrimes64[0] ^ uint64(num << 32) * kPrimes64[8];
  n = n * kPrimes64[1] ^ uint64(num >> 32) * kPrimes64[9];
  return n;
}

uint64 FingerprintString(const string& s) {
  return Hash64StringWithSeed(s.data(), s.size(), kHashSeed64);
}


uint64 FingerprintString(const char *s, uint32 len) {
  return Hash64StringWithSeed(s, len, kHashSeed64);
}


// This function hashes pointer sized items and returns a 32b hash, conveniently 
// hiding the fact that pointers may be 32b or 64b.
uint32 Hash32PointerWithSeed(const void* p, uint32 seed) {
  // Assume optimization.
  if (sizeof(void*) == sizeof(uint32)) {
    uint32 num = reinterpret_cast<uintptr_t>(p);
    return Hash32NumWithSeed(num, seed);
  } else {
    uint32 num1 = reinterpret_cast<uintptr_t>(p);
    uint32 num2 = reinterpret_cast<uint64>(p) >> 32;
    return Hash32NumWithSeed(num1, Hash32NumWithSeed(num2, seed));
  }
}
