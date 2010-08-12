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

#include <string.h>   // for strchr()

#include "fmt/fmt.h"
#include "utilities/quotefmt.h"


namespace sawzall {

// Implementation routines for various characters and strings.
// Doesn't need access to Sawzall's global Formatter, F, which is
// good because it's also used by milldump --format.

// Format one character; return whether it was printed in hex
static bool CharFmt(Fmt::State* f, int c, bool prev_was_hex, int quote) {
  switch (c) {
    case '\\':
      Fmt::fmtprint(f, "\\\\");
      return false;
    case '\a':
      Fmt::fmtprint(f, "\\a");
      return false;
    case '\b':
     Fmt::fmtprint(f, "\\b");
      return false;
    case '\f':
      Fmt::fmtprint(f, "\\f");
      return false;
    case '\n':
      Fmt::fmtprint(f, "\\n");
      return false;
    case '\r':
      Fmt::fmtprint(f, "\\r");
      return false;
    case '\t':
      Fmt::fmtprint(f, "\\t");
      return false;
    case '\v':
      Fmt::fmtprint(f, "\\v");
      return false;
  }

  // is it the terminating quote character?
  if (c == quote) {
    Fmt::fmtprint(f, "\\%c", quote);
    return false;
  }

  // ordinary char, but might need to print it \xNNNN
  bool hex = false;

  // would flow into previous escape?
  if (prev_was_hex && strchr("0123456789abcdefABCDEF", c) != NULL)
    hex = true;

  // non-printing?
  if (('\0' <= c && c < ' ') || (0x7F <= c && c <= 0xA0))
    hex = true;

  // unicode with sharp set?
  if (c >= 0x100 && (f->flags & Fmt::FmtSharp))
    hex = true;

  if (hex)
    Fmt::fmtprint(f, "\\x%x", c);
  else
    Fmt::fmtrune(f, c);

  return hex;
}


// Double-quoted string, general routine, known not to be NULL
static int DQStrFmt(Fmt::State* f, bool runes, char *s, Rune *r, int len) {
  int prec = 1000000;   // a large value.
  if(f->flags&Fmt::FmtPrec)
    prec = f->prec;
  Fmt::fmtrune(f, '"');
  int c = 'X';  // initial non-0 value
  bool hex = false;
  // interpret prec as the number of source characters to print
  while (len > 0 && --prec >= 0) {
    if (runes) {
      c = *r++;
      --len;
    } else {
      Rune t;
      int w;
      w = chartorune(&t, s);
      s += w;
      len -= w;
      c = t;
    }
    if (len >= 0)
      hex = CharFmt(f, c, hex, '"');
  }
  Fmt::fmtrune(f, '"');
  return 0;
}


// Double-quoted UTF-8 string
int DQUTF8StrFmt(Fmt::State* f) {
  char* s = FMT_ARG(f, char*);
  if (s == NULL)
    return Fmt::fmtprint(f, "<nil>");
  /* if precision is specified, make sure we don't wander off the end */
  int len;
  if (f->flags & Fmt::FmtPrec) {
    for (len = 0; len < f->prec && s[len] != '\0'; len++)
      ;
  } else {
    len = strlen(s);
  }
  return DQStrFmt(f, false, s, NULL, len);
}


// Double-quoted UTF-8 string, length-terminated, perhaps with \0
int ZDQUTF8StrFmt(Fmt::State* f) {
  char* s = FMT_ARG(f, char*);
  int len = FMT_ARG(f, int);
  if (s == NULL)
    return Fmt::fmtprint(f, "<nil>");
  return DQStrFmt(f, false, s, NULL, len);
}


// Double-quoted Rune string
int DQRuneStrFmt(Fmt::State* f) {
  Rune* r = FMT_ARG(f, Rune*);
  if (r == NULL)
    return Fmt::fmtprint(f, "<nil>");
  /* if precision is specified, make sure we don't wander off the end */
  int len;
  if (f->flags & Fmt::FmtPrec) {
    for (len = 0; len < f->prec && r[len] != '\0'; len++)
      ;
  } else {
    len = runestrlen(r);
  }
  return DQStrFmt(f, true, NULL, r, len);
}


// Double-quoted Rune string, length-terminated, perhaps with \0 
int ZDQRuneStrFmt(Fmt::State* f) {
  Rune* r = FMT_ARG(f, Rune*);
  int len = FMT_ARG(f, int);
  if (r == NULL)
    return Fmt::fmtprint(f, "<nil>");
  return DQStrFmt(f, true, NULL, r, len);
}


// Single-quoted Unicode character
int SQRuneFmt(Fmt::State* f) {
  int c = FMT_ARG(f, int);
  Fmt::fmtrune(f, '\'');
  CharFmt(f, c, false, '\'');
  Fmt::fmtrune(f, '\'');
  return 0;
}

}  // namespace sawzall
