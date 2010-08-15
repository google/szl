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

// Copied from the internals of the protocol compiler.
// These ought to be made available to plugins.

#include <stdio.h>
#include <string>
#include <limits>

using namespace std;

#include "public/porting.h"

namespace google {
namespace protobuf {
namespace compiler {
namespace szl {


namespace {

template<int I,int J> struct CTL10Helper : CTL10Helper<I/10,J+1> { };
template<int J> struct CTL10Helper<0,J> { static const int value = J; };
template<int I> struct CompileTimeLog10 : CTL10Helper<I,0> { };

template<typename T>
inline string CommonItoa(T t, const char* fmt) {
  char buffer[CompileTimeLog10<numeric_limits<T>::digits10>::value + 3];
  sprintf(buffer, fmt, t);
  return buffer;
}

template<typename T>
inline string CommonFDtoa(T t, const char* fmt) {
  const int kMantDig = numeric_limits<T>::digits10;
  const int kExpDig = numeric_limits<T>::max_exponent10;
  char buffer[kMantDig + CompileTimeLog10<kExpDig>::value + 7];
  T check = 0.0;
  sprintf(buffer, fmt, kMantDig, t);
  sscanf(buffer, fmt, sizeof(buffer), &check);
  if (check != t) {
    sprintf(buffer, fmt, kMantDig+1, t);
    sscanf(buffer, fmt, sizeof(buffer), &check);
    if (check != t)
      sprintf(buffer, fmt, kMantDig+2, t);
  }
  return buffer;
}

}


// The obvious conversions.

string SimpleItoa(int i)                { return CommonItoa(i, "%d"); }
string SimpleItoa(unsigned int i)       { return CommonItoa(i, "%u"); }
string SimpleItoa(long i)               { return CommonItoa(i, "%ld"); }
string SimpleItoa(unsigned long i)      { return CommonItoa(i, "%lu"); }
string SimpleItoa(long long i)          { return CommonItoa(i, "%lld"); }
string SimpleItoa(unsigned long long i) { return CommonItoa(i, "%llu"); }

string SimpleFtoa(float f)  { return CommonFDtoa(f, "%.*g"); }
string SimpleDtoa(double d) { return CommonFDtoa(d, "%.*lg"); }


// ----------------------------------------------------------------------
// CEscape()
//    Copies 'src' to result, escaping dangerous characters using
//    C-style escape sequences.
//    'src' and 'dest' should not overlap. The 'Hex' version
//    hexadecimal rather than octal sequences.
//
//    Currently only \n, \r, \t, ", ', \ and !isprint() chars are escaped.
// ----------------------------------------------------------------------

string CEscape(const string& source) {
  const char* src = source.data();
  int src_len = source.size();
  char* dest = new char[src_len * 4];  // Maximum possible expansion
  const char* src_end = src + src_len;
  int used = 0;

  for (; src < src_end; src++) {
    switch (*src) {
      case '\n': dest[used++] = '\\'; dest[used++] = 'n';  break;
      case '\r': dest[used++] = '\\'; dest[used++] = 'r';  break;
      case '\t': dest[used++] = '\\'; dest[used++] = 't';  break;
      case '\"': dest[used++] = '\\'; dest[used++] = '\"'; break;
      case '\'': dest[used++] = '\\'; dest[used++] = '\''; break;
      case '\\': dest[used++] = '\\'; dest[used++] = '\\'; break;
      default:
        if (!isprint(*src)) {
          sprintf(dest + used, "\\%03o", *reinterpret_cast<const uint8*>(src));
          used += 4;
        } else {
          dest[used++] = *src; break;
        }
    }
  }

  string result(dest, used);
  delete [] dest;
  return result;  // relies on NRVO to avoid an extra copy
}


}  // namespace szl
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
