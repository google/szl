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

/*
 * These routines were written by Rob Pike and Ken Thompson
 * and first appeared in Plan 9.
 * Only a subset appears here.
*/


#ifndef _RUNESH_
#define _RUNESH_ 1

#include <stdint.h>

typedef signed int Rune;  /* Code-point values in Unicode 4.0 are 21 bits wide.*/

enum {
  UTFmax	= 4,		/* maximum bytes per rune */
  Runesync	= 0x80,		/* cannot represent part of a UTF sequence (<) */
  Runeself	= 0x80,		/* rune and UTF sequences are the same (<) */
  Runeerror	= 0xFFFD,	/* decoding error in UTF */
  Runemax	= 0x10FFFF,	/* maximum rune value */
};


// runetochar copies (encodes) one rune, pointed to by r, to at most
// UTFmax bytes starting at s and returns the number of bytes generated.

int runetochar(char* s, const Rune* r);


// chartorune copies (decodes) at most UTFmax bytes starting at s to
// one rune, pointed to by r, and returns the number of bytes consumed.
// If the input is not exactly in UTF format, chartorune will set *r
// to Runeerror and return 1.
//
// Note: There is no special case for a "null-terminated" string. A
// string whose first byte has the value 0 is the UTF8 encoding of the
// Unicode value 0 (i.e., ASCII NULL). A byte value of 0 is illegal
// anywhere else in a UTF sequence.

int chartorune(Rune* r, const char* s);


// runelen returns the number of bytes required to convert r into UTF.

int runelen(Rune r);


// runenlen returns the number of bytes required to convert the n
// runes pointed to by r into UTF.

int runenlen(const Rune* r, int n);


// fullrune returns 1 if the string s of length n is long enough to be
// decoded by chartorune, and 0 otherwise. This does not guarantee
// that the string contains a legal UTF encoding. This routine is used
// by programs that obtain input one byte at a time and need to know
// when a full rune has arrived.

int fullrune(const char* s, int n);


// The following routines are analogous to the corresponding string
// routines with "utf" substituted for "str", and "rune" substituted
// for "chr".

// utflen returns the number of runes that are represented by the UTF
// string s. (cf. strlen)

int utflen(const char* s);


// utfnlen returns the number of complete runes that are represented
// by the first n bytes of the UTF string s. If the last few bytes of
// the string contain an incompletely coded rune, utfnlen will not
// count them; in this way, it differs from utflen, which includes
// every byte of the string. (cf. strnlen)

int utfnlen(const char* s, long n);


// utfrune returns a pointer to the first occurrence of rune r in the
// UTF string s, or 0 if r does not occur in the string.  The NULL
// byte terminating a string is considered to be part of the string s.
// (cf. strchr)

const char* utfrune(const char* s, Rune r);


// runestrlen is the rune-string analogue of strlen; result is bytes, not runes

int runestrlen(const Rune* s);


// Unicode defines some characters as letters and
// specifies three cases: upper, lower, and title.  Mappings between
// upper and lower case are defined, although they are not exhaustive: some
// upper case letters have no lower case mapping, and so on.
// These routines are based on Unicode version 3.0.0.
//
// toupperrune, tolowerrune, and totitlerune are the Unicode case
// mappings. These routines return the character unchanged if it has
// no defined mapping.

Rune toupperrune(Rune r);
Rune tolowerrune(Rune r);


#endif  // _RUNESH_
