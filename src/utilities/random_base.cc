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

#include <math.h>
#include <sys/time.h>
#include <string>
#include <memory.h>
#include <assert.h>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/sysutils.h"
#include "utilities/port_ieee.h"

#include "utilities/random_base.h"


// Use the mixing function from hash-inl.h
static inline void mix(uint32& a, uint32& b, uint32& c) {
  a -= b; a -= c; a ^= (c>>13);
  b -= c; b -= a; b ^= (a<<8);
  c -= a; c -= b; c ^= (b>>13);
  a -= b; a -= c; a ^= (c>>12);
  b -= c; b -= a; b ^= (a<<16);
  c -= a; c -= b; c ^= (b>>5);
  a -= b; a -= c; a ^= (c>>3);
  b -= c; b -= a; b ^= (a<<10);
  c -= a; c -= b; c ^= (b>>15);
}

// Note:  This is very similar to the HostnamePidTimeSeed() function from
// SzlACMRandom.
static const int kBufferSize = PATH_MAX + 3 * sizeof(uint32);
uint32 RandomBase::WeakSeed32() {
  uint8 buffer[kBufferSize];

  // Initialize the buffer with weak random contents, leaving room
  // for 3 uint32 guard values (0) past the end of the buffer.
  int len = WeakSeed(buffer, PATH_MAX);

  // Ensure that we still have at least 3 * sizeof(uint32) bytes
  // in the buffer after len, and zero those values.
  int remaining = sizeof(buffer) - len;
  CHECK_GT(remaining, 3 * sizeof(uint32));
  memset(buffer + len, 0, 3 * sizeof(uint32));

  uint32 a = Word32At(buffer);
  uint32 b = Word32At(buffer + sizeof(uint32));
  uint32 c = 0;
  for (int i = sizeof(uint32) * 2; i < len; i+= sizeof(uint32) * 3) {
    mix(a, b, c);
    a += Word32At(buffer + i);
    b += Word32At(buffer + i + sizeof(uint32));
    c += Word32At(buffer + i + sizeof(uint32) + sizeof(uint32));
  }
  c += len;
  mix(a, b, c);
  return c;
}

int RandomBase::WeakSeed(uint8* buffer, int bufferlen) {
  int offset = 0;
  char * seed_buffer = reinterpret_cast<char*>(buffer);
  // PID. (Probably only 16 bits)
  if (bufferlen >= offset+2) {
    uint16 pid = getpid();
    memcpy(seed_buffer + offset, &pid, 2);
    offset += 2;
  }

  // CycleClock
  if (bufferlen >= offset + 8) {
    uint64 clock = sawzall::CycleClockNow();
    memcpy(seed_buffer + offset, &clock, 8);
    offset += 8;
  }

  // Time of day.
  if (bufferlen >= offset + 4) {
    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    memcpy(seed_buffer + offset, &start_time.tv_usec, 4);
    offset += 4;
    if (bufferlen >= offset + 4) {
      memcpy(seed_buffer + offset, &start_time.tv_usec, 4);
      offset += 4;
    }
  }

  // Get the hostname.
  if (bufferlen > offset &&
      gethostname(seed_buffer + offset, bufferlen - offset) == 0) {
    offset += strlen(seed_buffer + offset);
  }

  return offset;
}


RandomBase::~RandomBase() {}

// Getting a string of random bytes is a common pattern.
string RandomBase::RandString(int desired_len) {
  CHECK_GE(desired_len, 0);

  string result;
  result.resize(desired_len);
  for (string::iterator it = result.begin(); it != result.end(); ++it) {
    *it = Rand8();
  }
  return result;
}

// precondition: n >= 0.
int32 RandomBase::UnbiasedUniform(int32 n) {
  CHECK_LE(0, n);
  const uint32 range = ~static_cast<uint32>(0);
  if (n == 0) {
    return Rand32() * n;
  } else if (0 == (n & (n - 1))) {
    // N is a power of two, so just mask off the lower bits.
    return Rand32() & (n-1);
  } else {
    // Reject all numbers that skew the distribution towards 0.

    // Rand32's output is uniform in the half-open interval [0, 2^{32}).
    // For any interval [m,n), the number of elements in it is n-m.

    uint32 rem = (range % n) + 1;
    uint32 rnd;

    // rem = ((2^{32}-1) \bmod n) + 1
    // 1 <= rem <= n

    // NB: rem == n is impossible, since n is not a power of 2 (from
    // earlier check).

    do {
      rnd = Rand32();     // rnd uniform over [0, 2^{32})
    } while (rnd < rem);  // reject [0, rem)
    // rnd is uniform over [rem, 2^{32})
    //
    // The number of elements in the half-open interval is
    //
    //  2^{32} - rem = 2^{32} - ((2^{32}-1) \bmod n) - 1
    //               = 2^{32}-1 - ((2^{32}-1) \bmod n)
    //               = n \cdot \lfloor (2^{32}-1)/n \rfloor
    //
    // therefore n evenly divides the number of integers in the
    // interval.
    //
    // The function v \rightarrow v % n takes values from [bias,
    // 2^{32}) to [0, n).  Each integer in the range interval [0, n)
    // will have exactly \lfloor (2^{32}-1)/n \rfloor preimages from
    // the domain interval.
    //
    // Therefore, v % n is uniform over [0, n).  QED.

    return rnd % n;
  }
}

uint64 RandomBase::UnbiasedUniform64(uint64 n) {
  if (n == 0) {
    return Rand64() * n;  // Consume a value anyway.
  } else if (0 == (n & (n - 1))) {
    // n is a power of two, so just mask off the lower bits.
    return Rand64() & (n-1);
  } else {
    const uint64 range = ~static_cast<uint64>(0);
    const uint64 rem = (range % n) + 1;
    uint64 rnd;

    do {
      rnd = Rand64();     // rnd is uniform over [0, 2^{64})
    } while (rnd < rem);  // reject [0, rem)
    // rnd is uniform over [rem, 2^{64}), which contains a multiple of
    // n integers
    return rnd % n;
  }
}

float RandomBase::RandFloat() {
#ifdef OS_CYGWIN
  __ieee_float_shape_type v;
  v.number.sign = 0;
  v.number.fraction0 = Rand8();   // Lower 7 bits
  v.number.fraction1 = Rand16();  // 16 bits
  // Exponent is 8 bits wide, using an excess 127 exponent representation.
  // We have to take care of the implicit 1 in the mantissa.
  v.number.exponent = 127;
  return v.value - static_cast<float>(1.0);
#else
  union ieee754_float v;
  v.ieee.negative = 0;
  v.ieee.mantissa = Rand32();  // lower 23 bits
  // Exponent is 8 bits wide, using an excess 127 exponent representation.
  // We have to take care of the implicit 1 in the mantissa.
  v.ieee.exponent = 127;
  return v.f - static_cast<float>(1.0);
#endif
}

double RandomBase::RandDouble() {
#ifdef OS_CYGWIN
  __ieee_double_shape_type v;
  v.number.sign = 0;
  v.number.fraction0 = Rand32();  // Lower 20 bits
  v.number.fraction1 = Rand32();  // 32 bits
#ifdef __SMALL_BITFIELDS
  // Previous two were actually 4 and 16 respectively; these are the last 32.
  v.number.fraction2 = Rand16();
  v.number.fraction3 = Rand16();
#endif
  // Exponent is 11 bits wide, using an excess 1023 representation.
  v.number.exponent = 1023;
  return v.value - static_cast<double>(1.0);
#else
  union ieee754_double v;
  v.ieee.negative = 0;
  v.ieee.mantissa0 = Rand32();  // lower 20 bits
  v.ieee.mantissa1 = Rand32();  // 32 bits
  // Exponent is 11 bits wide, using an excess 1023 representation.
  v.ieee.exponent = 1023;
  return v.d - static_cast<double>(1.0);
#endif
}

double RandomBase::RandExponential() {
  return -log(RandDouble());
}
