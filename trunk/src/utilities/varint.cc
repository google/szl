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

#include <stdlib.h>
#include <assert.h>

#include "public/porting.h"
#include "public/varint.h"


const int kMaxVarint32Bytes = 5;
const int kMaxVarintBytes = 10;


namespace sawzall {


char* EncodeUnsignedVarint32(char* sptr, uint32 v) {
  // Operate on characters as unsigneds
  unsigned char* ptr = reinterpret_cast<unsigned char*>(sptr);
  static const int B = 128;
  if (v < (1<<7)) {
    *(ptr++) = v;
  } else if (v < (1<<14)) {
    *(ptr++) = v | B;
    *(ptr++) = v>>7;
  } else if (v < (1<<21)) {
    *(ptr++) = v | B;
    *(ptr++) = (v>>7) | B;
    *(ptr++) = v>>14;
  } else if (v < (1<<28)) {
    *(ptr++) = v | B;
    *(ptr++) = (v>>7) | B;
    *(ptr++) = (v>>14) | B;
    *(ptr++) = v>>21;
  } else {
    *(ptr++) = v | B;
    *(ptr++) = (v>>7) | B;
    *(ptr++) = (v>>14) | B;
    *(ptr++) = (v>>21) | B;
    *(ptr++) = v>>28;
  }
  return reinterpret_cast<char*>(ptr);
}


char* EncodeUnsignedVarint64(char* sptr, uint64 v) {
  if (v < (1u << 28)) {
    return EncodeUnsignedVarint32(sptr, v);
  } else {
    // Operate on characters as unsigneds
    unsigned char* ptr = reinterpret_cast<unsigned char*>(sptr);
    static const int B = 128;
    uint32 v32 = v;
    *(ptr++) = v32 | B;
    *(ptr++) = (v32 >> 7) | B;
    *(ptr++) = (v32 >> 14) | B;
    *(ptr++) = (v32 >> 21) | B;
    if (v < (1ull << 35)) {
      *(ptr++) = (v   >> 28);
      return reinterpret_cast<char*>(ptr);
    } else {
      *(ptr++) = (v   >> 28) | B;
      return EncodeUnsignedVarint32(reinterpret_cast<char*>(ptr), v >> 35);
    }
  }
}


const char* DecodeUnsignedVarint32(const char* p, uint32* OUTPUT) {
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(p);
  uint32 result = *ptr++;
  if (result >= 128) {
    result &= 127;
    uint32 byte;
    byte = *(ptr++); result |= (byte & 127) <<  7; if (byte < 128) goto done;
    byte = *(ptr++); result |= (byte & 127) << 14; if (byte < 128) goto done;
    byte = *(ptr++); result |= (byte & 127) << 21; if (byte < 128) goto done;
    byte = *(ptr++); result |= (byte & 127) << 28; if (byte < 128) goto done;
    return NULL;       // Value is too long to be a varint32
  }
 done:
  *OUTPUT = result;
  return reinterpret_cast<const char*>(ptr);
}


const char* DecodeUnsignedVarint64(const char* p, uint64* OUTPUT) {
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(p);
  // Fast path: need to accumulate data in upto three result fragments
  //    res1    bits 0..27
  //    res2    bits 28..55
  //    res3    bits 56..63

  uint32 byte;
  uint32 res1;
  uint32 res2 = 0;
  uint32 res3 = 0;
  byte = *(ptr++); res1 = byte & 127;          if (byte < 128) goto done1;
  byte = *(ptr++); res1 |= (byte & 127) <<  7; if (byte < 128) goto done1;
  byte = *(ptr++); res1 |= (byte & 127) << 14; if (byte < 128) goto done1;
  byte = *(ptr++); res1 |= (byte & 127) << 21; if (byte < 128) goto done1;

  byte = *(ptr++); res2 = byte & 127;          if (byte < 128) goto done2;
  byte = *(ptr++); res2 |= (byte & 127) <<  7; if (byte < 128) goto done2;
  byte = *(ptr++); res2 |= (byte & 127) << 14; if (byte < 128) goto done2;
  byte = *(ptr++); res2 |= (byte & 127) << 21; if (byte < 128) goto done2;

  byte = *(ptr++); res3 = byte & 127;          if (byte < 128) goto done3;
  byte = *(ptr++); res3 |= (byte & 127) <<  7; if (byte < 128) goto done3;

  return NULL;       // Value is too long to be a varint64

 done1:
  assert(res2 == 0);
  assert(res3 == 0);
  *OUTPUT = res1;
  return reinterpret_cast<const char*>(ptr);

done2:
  assert(res3 == 0);
  *OUTPUT = res1 | (uint64(res2) << 28);
  return reinterpret_cast<const char*>(ptr);

done3:
  *OUTPUT = res1 | (uint64(res2) << 28) | (uint64(res3) << 56);
  return reinterpret_cast<const char*>(ptr);
}


}  // namespace sawzall
