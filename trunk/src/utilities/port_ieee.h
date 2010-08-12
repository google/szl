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

#include <config.h>
#if defined(OS_CYGWIN)
#include <ieeefp.h>
#elif defined(OS_MACOSX)
#include <machine/endian.h>
#else
#include <ieee754.h>
#endif

#ifdef OS_MACOSX  // Mac OS X lacks an ieee754 header.

#ifndef __FLOAT_WORD_ORDER
#define __FLOAT_WORD_ORDER __DARWIN_BYTE_ORDER
#endif

union ieee754_float {
  float f;

  // IEEE 754 single-precision format
  struct {
#if __DARWIN_BYTE_ORDER == __DARWIN_BIG_ENDIAN
    unsigned int negative:1;
    unsigned int exponent:8;
    unsigned int mantissa:23;
#endif  // big endian
#if __DARWIN_BYTE_ORDER == __DARWIN_LITTLE_ENDIAN
    unsigned int mantissa:23;
    unsigned int exponent:8;
    unsigned int negative:1;
#endif  // little endian
  } ieee;

  struct {
#if __DARWIN_BYTE_ORDER == __DARWIN_BIG_ENDIAN
    unsigned int negative:1;
    unsigned int exponent:8;
    unsigned int quiet_nan:1;
    unsigned int mantissa:22;
#endif  // big endian
#if __DARWIN_BYTE_ORDER == __DARWIN_LITTLE_ENDIAN
    unsigned int mantissa:22;
    unsigned int quiet_nan:1;
    unsigned int exponent:8;
    unsigned int negative:1;
#endif  // little endian
  } ieee_nan;
};

union ieee754_double {
  double d;

  // IEEE 754 double-precision format
  struct {
#if __DARWIN_BYTE_ORDER == __DARWIN_BIG_ENDIAN
    unsigned int negative:1;
    unsigned int exponent:11;
    unsigned int mantissa0:20;
    unsigned int mantissa1:32;
#endif  // big endian
#if __DARWIN_BYTE_ORDER == __DARWIN_LITTLE_ENDIAN
# if  __FLOAT_WORD_ORDER == __BIG_ENDIAN
    unsigned int mantissa0:20;
    unsigned int exponent:11;
    unsigned int negative:1;
    unsigned int mantissa1:32;
# else
    unsigned int mantissa1:32;
    unsigned int mantissa0:20;
    unsigned int exponent:11;
    unsigned int negative:1;
# endif
#endif  // little endian
  } ieee;

  struct {
#if __DARWIN_BYTE_ORDER == __DARWIN_BIG_ENDIAN
    unsigned int negative:1;
    unsigned int exponent:11;
    unsigned int quiet_nan:1;
    unsigned int mantissa0:19;
    unsigned int mantissa1:32;
#else
# if  __FLOAT_WORD_ORDER == __DARWIN_BIG_ENDIAN
    unsigned int mantissa0:19;
    unsigned int quiet_nan:1;
    unsigned int exponent:11;
    unsigned int negative:1;
    unsigned int mantissa1:32;
# else
    unsigned int mantissa1:32;
    unsigned int mantissa0:19;
    unsigned int quiet_nan:1;
    unsigned int exponent:11;
    unsigned int negative:1;
# endif
#endif
  } ieee_nan;
};

#endif  // OS_MACOSX
