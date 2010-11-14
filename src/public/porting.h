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

#ifndef PUBLIC_PORTING_H_
#define PUBLIC_PORTING_H_

#include <stdint.h>
#include <stdlib.h>

// The current version, represented as a single integer to make comparison
// easier:  major * 10^6 + minor * 10^3 + micro
#define GOOGLE_SZL_VERSION 1000000

// Definitions that might have to be changed when porting this code.

#if SZL_BYTE_ORDER == 0
#error Byte order is unknown or ambiguous
#endif

using namespace std;

typedef unsigned int uint;

typedef int8_t  int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;


// other

typedef signed char schar;

typedef long sword_t;
typedef unsigned long uword_t;

typedef uint64 Fprint;

#ifndef COMPILE_ASSERT
template <bool> struct CompileAssert { };
#define COMPILE_ASSERT(expr, msg_as_identifier) \
    typedef CompileAssert<(bool(expr))> msg_as_identifier[bool(expr) ? 1 : -1]
#endif


const int32  kint32min  = (int32)  0x80000000;
const int32  kint32max  = (int32)  0x7FFFFFFF;
const uint32 kuint32max = (uint32) 0xFFFFFFFFU;
const int64  kint64min  = (int64)  0x8000000000000000LL;
const int64  kint64max  = (int64)  0x7FFFFFFFFFFFFFFFLL;
const uint64 kuint64max = (uint64) 0xFFFFFFFFFFFFFFFFULL;


#define GG_LONGLONG(x) x##LL
#define GG_ULONGLONG(x) x##ULL
#define GG_LL_FORMAT "ll"  // As in "%lld". Note that "q" is poor form also.
#define GG_LL_FORMAT_W L"ll"


inline int MinInt(int a, int b) { return (a < b) ? a : b; }
inline int MaxInt(int a, int b) { return (a > b) ? a : b; }

#ifndef PATH_MAX
#define PATH_MAX 2048
#endif



#define OFFSETOF_MEMBER(t, f)         \
  (reinterpret_cast<char*>(           \
     &reinterpret_cast<t*>(16)->f) -  \
   reinterpret_cast<char*>(16))

#define ARRAYSIZE(a) (sizeof(a) / sizeof(*(a))) 

namespace {
  template<typename T> struct PtrTest { static const bool IsPtr = false; };
  template<typename T> struct PtrTest<T*> { static const bool IsPtr = true; };
}

template<typename To, typename From>
inline To implicit_cast(const From &f) {
  return f;
}

template<typename To, typename From>
To pun_cast(const From* u) {
  // "To" must be a pointer type:
  COMPILE_ASSERT(PtrTest<To>::IsPtr,
                 pun_cast_used_with_non_pointer_template_argument);
  return reinterpret_cast<To>(u);
}

template<typename To, typename From>
inline To down_cast(From* f) {
  // Compile-time check that "To" is a subclass of "From".
  if (false)
    implicit_cast<From*, To>(0);

  // In debug builds only, verify that the cast is valid.
  assert(f == NULL || dynamic_cast<To>(f) != NULL);

  return static_cast<To>(f);
}


#endif  // PUBLIC_PORTING_H_
