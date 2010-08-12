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

#include <string.h>
#include <memory.h>
#include <vector>
#include <string>


extern const uint8 kAsciiPropertyBits[256];

// avoid including runes.h
typedef signed int Rune;  // Code-point values in Unicode 4.0 are 21 bits wide.
int chartorune(Rune* r, const char* s);

inline bool ascii_isspace(unsigned char c) {
  return kAsciiPropertyBits[c] & 0x08;
} 

// Splitting strings into fields
int tokenize(char *s, char **args, int maxargs);  // defined in strtotm.cc

// Split string at commas
void SplitStringAtCommas(const string& str, vector<string>* pieces);

// Like asprintf, but returns a C++ string.
string StringPrintf(const char* format, ...);

// Like StringPrintf, but appends the result.
void StringAppendF(string* dst, const char* format, ...);


// Like strncpy, but guarantees a null terminator and does not pad
char* safestrncpy(char* dst, const char* src, size_t n);

// case-insensitve string comparison, from Plan 9.
int cistrcmp(const char *s1, const char *s2);

inline char* strdup_with_new(const char* str) {
  if (str == NULL)
    return NULL;
  int length = strlen(str);
  return reinterpret_cast<char*>(memcpy(new char[length+1], str, length+1));
}


// For now, long long is 64-bit on all the platforms we care about, so this
// function can simply pass the call to strto[u]ll.
inline int64 strto64(const char *nptr, char **endptr, int base) {
  COMPILE_ASSERT(sizeof(int64) == sizeof(long long),
                 sizeof_int64_is_not_sizeof_long_long);
  return strtoll(nptr, endptr, base);
}

uint64 ParseLeadingHex64Value(const char *str, uint64 deflt);

int FloatToAscii(char (&buf)[64], double x);

int WebSafeBase64Escape(const unsigned char *src, int szsrc, char *dest,
                        int szdest, bool do_padding);

int Base64Escape(const unsigned char *src, int szsrc, char *dest, int szdest);

int CalculateBase64EscapedLen(int input_len, bool do_padding = true);

int Base64Unescape(const char *src, int szsrc, char *dest, int szdest);

int WebSafeBase64Unescape(const char *src, int szsrc, char *dest, int szdest);


class DualString {
 public:
  // Creation
  DualString(char* utf8, int num_utf8, int num_runes);
  ~DualString();

  // Access.  The num_* routines return the number of
  // runes/bytes remaining after the cursor.
  char* utf8() const  { return utf8_ + utf8cursor_; }
  int num_runes() const  { return num_runes_ - runecursor_; }
  int num_utf8() const  { return num_utf8_ - utf8cursor_; }

  // Move along string.  Return the number of bytes moved.
  int Advance(int nrunes_forward);

  // Move along string, when we know both sizes.
  void Advance(int nbytes_forward, int nrunes_forward);

  // Convert byte offsets into rune offsets
  void ConvertPositions(int* runepos, int* utf8pos, int n);

 private:
  int num_runes_;
  int runecursor_;
  int utf8cursor_;
  char* utf8_;
  int num_utf8_;
  int *runepos_;
};


// Support for regular expressions.
// Compiled pattern is returned as an opaque void*, hiding underlying library.
// 'pattern' must be NUL-terminated.
void* CompileRegexp(const char* pattern, const char** errbufp);

// Match pattern against the compiled RE, returning vector of match
// positions. matches[0,1] is match of whole string; match[2,3]
// is match of first parenthesized subexpression, etc.
int DualExecRegexp(void* compiled_pattern, DualString *dual,
                   int matches[], int byte_matches[], int nmatches);

// Like ExecRegexp, but only checks for presence of a match.
int SimpleExecRegexp(void* compiled_pattern, const char* utf8, int nutf8);

// Free compiled regular expression.
void FreeRegexp(void* pattern);


// ----------------------------------------------------------------------------
// Rune & string helpers

int CStrValidUTF8Len(const char* str, bool* is_valid_utf8, int* num_runes);
int StrValidUTF8Len(const char* str, int len, bool* is_valid_utf8, int* num_runes);
int CStr2ValidUTF8(char* dst, const char* src);
int Str2ValidUTF8(char* dst, const char* src, int len);
int Str2RuneStr(Rune* dst, const char* src, int len);
bool IsValidUnicode(int r);
void RuneStr2Str(char* dst, int num_bytes, const Rune* src, int len);
int RuneStr2CStr(char* dst, int clen, const Rune* src, int len);
int RuneStr2CStrWithPos(char* dst, int clen, int* runepos, const Rune* src, int len);
int GetRunePositions(int* runepos, const char* src, int len);

// High-speed version of chartorune; avoids function call if ASCII.
inline int FastCharToRune(Rune* r, const char* p) {
  *r = *reinterpret_cast<const unsigned char*>(p);
  if (*r < 0x80)
    return 1;
  else
    return chartorune(r, p);
}


