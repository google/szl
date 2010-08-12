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
 * The authors of this software are Rob Pike and Ken Thompson.
 *              Copyright (c) 2002 by Lucent Technologies.
 * Permission to use, copy, modify, and distribute this software for any
 * purpose without fee is hereby granted, provided that this entire notice
 * is included in all copies of any software which is or includes a copy
 * or modification of this software and in all copies of the supporting
 * documentation for such software.
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTY.  IN PARTICULAR, NEITHER THE AUTHORS NOR LUCENT TECHNOLOGIES MAKE ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE MERCHANTABILITY
 * OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR PURPOSE.
 */
 
namespace Fmt {

/*
 * Internal definitions and routines for the fmt library
 */

#define uchar _fmtuchar
#define ushort _fmtushort
#define uint _fmtuint
#define ulong _fmtulong
#define vlong _fmtvlong
#define uvlong _fmtuvlong

#define USED(x) if(x);else

typedef unsigned char		uchar;
typedef unsigned short		ushort;
typedef unsigned int		uint;
typedef unsigned long		ulong;

#ifndef NOVLONGS
typedef unsigned long long	uvlong;
typedef long long		vlong;
#endif

#define nil		0	/* cannot be ((void*)0) because used for function pointers */

/*
 * It's va_copy in POSIX but __va_copy in gcc-2.95 and hard to
 * configure conditionally.  We define this here to make it easy to
 * change as required.
 */
#define VA_COPY	__va_copy

typedef struct Quoteinfo Quoteinfo;
struct Quoteinfo
{
	int	quoted;		/* if set, string must be quoted */
	int	nrunesin;	/* number of input runes that can be accepted */
	int	nbytesin;	/* number of input bytes that can be accepted */
	int	nrunesout;	/* number of runes that will be generated */
	int	nbytesout;	/* number of bytes that will be generated */
};

void	*__fmtflush(Fmt::State*, void*, int);
int	__floatfmt(Fmt::State*, double);
int	__fmtpad(Fmt::State*, int);
int	__rfmtpad(Fmt::State*, int);
int	__fmtFdFlush(Fmt::State*);

int	__efgfmt(Fmt::State*);
int	__charfmt(Fmt::State*);
int	__runefmt(Fmt::State*);
int	__runesfmt(Fmt::State*);
int	__countfmt(Fmt::State*);
int	__flagfmt(Fmt::State*);
int	__percentfmt(Fmt::State*);
int	__ifmt(Fmt::State*);
int	__strfmt(Fmt::State*);
int	__badfmt(Fmt::State*);
int	__fmtcpy(Fmt::State*, const void*, int, int);
int	__fmtrcpy(Fmt::State*, const void*, int n);
int	__errfmt(Fmt::State *f);
int	__needsep(int*, const char**);

double	__fmtpow10(int);

const void*	dispatch(Formatter*, State*, const void*, int, va_list*, int);
Convfmt* fmtfmt(Formatter*, int c);

#define FMTCHAR(f, t, s, c)\
	do{\
	if(t + 1 > (char*)s){\
		t = (char*)__fmtflush(f, t, 1);\
		if(t != nil)\
			s = (char*)f->stop;\
		else\
			return -1;\
	}\
	*t++ = c;\
	}while(0)

#define FMTRCHAR(f, t, s, c)\
	do{\
	if(t + 1 > (Rune*)s){\
		t = (Rune*)__fmtflush(f, t, sizeof(Rune));\
		if(t != nil)\
			s = (Rune*)f->stop;\
		else\
			return -1;\
	}\
	*t++ = c;\
	}while(0)

#define FMTRUNE(f, t, s, r)\
	do{\
	Rune _rune;\
	int _runelen;\
	if(t + UTFmax > (char*)s && t + (_runelen = runelen(r)) > (char*)s){\
		t = (char*)__fmtflush(f, t, _runelen);\
		if(t != nil)\
			s = (char*)f->stop;\
		else\
			return -1;\
	}\
	if(r < Runeself)\
		*t++ = r;\
	else{\
		_rune = r;\
		t += runetochar(t, &_rune);\
	}\
	}while(0)

}  /* end namespace Fmt */
