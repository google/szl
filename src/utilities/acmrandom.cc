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

#include <string>
#include <climits>
#include <ctime>
#include <string.h>
#include <unistd.h>
#include <errno.h>


#include "public/porting.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "utilities/sysutils.h"
#include "utilities/random_base.h"
#include "utilities/acmrandom.h"


SzlACMRandom::~SzlACMRandom() { }


// Robert Jenkins' mix function.
static uint32 mix(uint32 a, uint32 b, uint32 c) {
  a=a-b;  a=a-c;  a=a^(c >> 13);
  b=b-c;  b=b-a;  b=b^(a << 8); 
  c=c-a;  c=c-b;  c=c^(b >> 13);
  a=a-b;  a=a-c;  a=a^(c >> 12);
  b=b-c;  b=b-a;  b=b^(a << 16);
  c=c-a;  c=c-b;  c=c^(b >> 5);
  a=a-b;  a=a-c;  a=a^(c >> 3);
  b=b-c;  b=b-a;  b=b^(a << 10);
  c=c-a;  c=c-b;  c=c^(b >> 15);
  return c;
}


/* static */
int32 SzlACMRandom::HostnamePidTimeSeed() {
  char name[PATH_MAX + 20];      // need 12 bytes for 3 'empty' uint32's
  assert(sizeof(name) - PATH_MAX > sizeof(uint32) * 3);

  CHECK(gethostname(name, PATH_MAX) == 0)
    << ": gethostname: " << strerror(errno);
  const int namelen = strlen(name);
  for (int i = 0; i < sizeof(uint32) * 3; ++i)
    name[namelen + i] = '\0';   // so we mix 0's once we get to end-of-string

  uint32 a = getpid();
  uint32 b = static_cast<uint32>(sawzall::CycleClockNow());
  uint32 c = 0;
  const uint8* p = reinterpret_cast<const uint8*>(name);
  for (int i = 0; i < namelen; i += sizeof(uint32) * 3) {
    a += Word32At(p + i);
    b += Word32At(p + i + sizeof(uint32));
    c += Word32At(p + i + sizeof(uint32) + sizeof(uint32));
    mix(a,b,c);
  }
  c += namelen;                      // one final mix
  c = mix(a,b,c);
  return static_cast<int32>(c);      // I guess the seed can be negative
}

int64 SzlACMRandom::Next64() {
  // Given: 1 <= Next() <= M-1, where M = 2^31-1
  //
  // Hence: 1 <= (Next() - 1) * (M-1) + Next()
  //          <= (M-2) * (M-1) + (M-1)
  //          =  (M-1) * (M-1)
  //          <  2^62
  //
  // Hence, all results are in the range [1, (2^31-2)^2] and all numbers in
  // the range are equally probable.  The results will never overflow an
  // int64.  Neither will the intermedaite results.
  return (int64(Next()) - 1) * (M-1) + Next();
}

uint8  SzlACMRandom::Rand8() {
  return static_cast<uint8>((Next() >> 1) & 0x000000ff);
}

uint16 SzlACMRandom::Rand16() {
  return static_cast<uint16>((Next() >> 1) & 0x0000ffff);
}

// Our range here is [0, 2^31 - 3]
uint32 SzlACMRandom::Rand32() {
  return static_cast<uint32>(Next() - 1);
}

// Our range here is [0, (2^31-2)^2-1]
uint64 SzlACMRandom::Rand64() {
  return static_cast<uint64>(Next64() - 1);
}


int32 SzlACMRandom::UnbiasedUniform(int32 n) {
  const uint32 range = M - 2;
  CHECK_LE(n, range);

  if (n == 0) {
    return Next() * 0;
  } else {
    uint32 rem = range % n;
    uint32 rnd;
    do {
      rnd = Next();
    } while (rnd <= rem);
    return rnd % n;
  }
}
