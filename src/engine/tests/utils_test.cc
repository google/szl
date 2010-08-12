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
#include <time.h>
#include <string>
#include <vector>

#include "fmt/runes.h"

#include "engine/globals.h"
#include "public/commandlineflags.h"
#include "public/logging.h"

#include "utilities/strutils.h"
#include "utilities/random_base.h"
#include "utilities/acmrandom.h"

#include "engine/memory.h"
#include "engine/utils.h"


DEFINE_int32(utf8_test_iter, 1000, "Iterations for UTF-8 timing test");

static string RandomString(SzlACMRandom* rnd, int len, bool ascii) {
  string result;
  while (result.size() < len) {
    Rune r = 1 + (ascii ? rnd->Uniform(127) : rnd->Uniform(255));
    char buf[10];
    int n = runetochar(buf, &r);
    result.append(buf, n);
  }
  return result;
}



namespace sawzall {

// Original version of the StrValidUTF8Len routine, preserved here for
// testing purposes

// Return the length that the bytes stored at src would require
// to be stored as valid UTF-8.  The boolean will report whether
// the input is valid UTF-8 as is.  Note: src must not contain a \0.
static int StrValidUTF8LenOld(const char* src, int len, bool* is_valid_utf8,
                              int* num_runes) {
  *is_valid_utf8 = true;
  const char* end = src + len;
  int valid_len = 0;
  int n = 0;
  while (src < end) {
    Rune r;
    r = *reinterpret_cast<const unsigned char*>(src);
    if (r < Runeself) {
      src++;
      valid_len++;
    } else {
      int in_rune_len;
      if (!fullrune(src, end-src)) {
        // Bad trailing encoding; absorb one byte, emit Runerror
        in_rune_len = 1;
        r = Runeerror;
      } else {
        in_rune_len = FastCharToRune(&r, src);
        // TODO: think about whether runelen() returning a different
        // value than chartorune() on errors is a problem.
      }
      src += in_rune_len;
      int out_rune_len = runelen(r);
      valid_len += out_rune_len;
      if (out_rune_len != in_rune_len)
        *is_valid_utf8 = false;
    }
    n++;
  }
  *num_runes = n;
  return valid_len;
}


void StrValidUTF8Len_Helper(const char* name, vector<string>* random_strings,
                            bool use_new) {
  time_t start = time(NULL);
  for (int len = 0; len < random_strings->size(); len++) {
    string& base = (*random_strings)[len];
    for (int i = 0; i < FLAGS_utf8_test_iter; i++) {
      bool is_valid_utf8;
      int num_runes;
      if (use_new) {
        StrValidUTF8Len(base.data(), base.size(), &is_valid_utf8, &num_runes);
      } else {
        StrValidUTF8LenOld(base.data(), base.size(), &is_valid_utf8, &num_runes);
      }
    }
  }
  printf("Time for %s: %d milliseconds\n", name,
         int((time(NULL) - start) * 1000.0 / FLAGS_utf8_test_iter + 0.5));
}



void CheckStrValidUTF8Len(const string& s) {
  bool is_valid_utf8_a, is_valid_utf8_b;
  int num_runes_a, num_runes_b;
  int result_a, result_b;
  result_a = StrValidUTF8Len(s.data(), s.size(), &is_valid_utf8_a,
                             &num_runes_a);
  result_b = StrValidUTF8LenOld(s.data(), s.size(), &is_valid_utf8_b,
                                &num_runes_b);
  CHECK_EQ(is_valid_utf8_a, is_valid_utf8_b);
  CHECK_EQ(num_runes_a, num_runes_b);
  CHECK_EQ(result_a, result_b);
}

void TestStrValidUTF8Len() {
  CheckStrValidUTF8Len("");
  CheckStrValidUTF8Len("a");
  CheckStrValidUTF8Len("ab");
  SzlACMRandom rnd(301);
  for (int iters = 0; iters < 10000; iters++) {
    int len = rnd.Skewed(10);
    // All ASCII
    CheckStrValidUTF8Len(RandomString(&rnd, len, true));
    // May contain Non-ASCII
    CheckStrValidUTF8Len(RandomString(&rnd, len, false));
    // All ASCII prefix, followed by non-ascii suffix
    int prefix_len = rnd.Skewed(10);
    CheckStrValidUTF8Len(RandomString(&rnd, prefix_len, true) +
                         RandomString(&rnd, len, false));
  }
}

void NullHandling() {
  string str = "string";
  bool is_valid;
  int num_runes;
  char* result = (char*)malloc(9);
  result[8] = '\0';
  str[0] = '\0';
  Str2ValidUTF8(result, str.c_str(), str.length());
  CHECK_EQ(0, strcmp(result, "\uFFFDtring"));
  CHECK_EQ(8, StrValidUTF8Len(str.c_str(), str.length(),
                              &is_valid, &num_runes));
  CHECK(!is_valid);

  str = "string";
  str[5] = '\0';
  Str2ValidUTF8(result, str.c_str(), str.length());
  CHECK_EQ(0, strcmp(result, "strin\uFFFD"));
  CHECK_EQ(8, StrValidUTF8Len(str.c_str(), str.length(),
                              &is_valid, &num_runes));
  CHECK(!is_valid);

  str = "strin\xD0\x00";
  Str2ValidUTF8(result, str.c_str(), str.length());
  CHECK_EQ(0, strcmp(result, "strin\uFFFD"));
  CHECK_EQ(8, StrValidUTF8Len(str.c_str(), str.length(),
                              &is_valid, &num_runes));
  CHECK(!is_valid);
  free(result);
}


}  // namespace sawzall


int main(int argc, char **argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  sawzall::TestStrValidUTF8Len();
  sawzall::NullHandling();

  vector<string> random_ascii_strings;
  vector<string> random_nonascii_strings;
  SzlACMRandom rnd(301);
  for (int i = 0; i < 1024; i++) {
    random_ascii_strings.push_back(RandomString(&rnd, i, true));
    random_nonascii_strings.push_back(RandomString(&rnd, i, false));
  }
  sawzall::StrValidUTF8Len_Helper("StrValidUTF8LenASCIIOld",
                                  &random_ascii_strings, false);
  sawzall::StrValidUTF8Len_Helper("StrValidUTF8LenNonASCIIOld",
                                  &random_nonascii_strings, false);
  sawzall::StrValidUTF8Len_Helper("StrValidUTF8LenASCII",
                                  &random_ascii_strings, true);
  sawzall::StrValidUTF8Len_Helper("StrValidUTF8LenNonASCII",
                                  &random_nonascii_strings, true);

  puts("PASS");
}
