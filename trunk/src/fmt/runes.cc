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

#include <stdarg.h>
#include <string.h>
#include "fmt/runes.h"

#include "public/porting.h"

typedef uint8 uchar;
#define nelem(x) (sizeof(x)/sizeof((x)[0]))

enum
{
	Bit1	= 7,
	Bitx	= 6,
	Bit2	= 5,
	Bit3	= 4,
	Bit4	= 3,
	Bit5	= 2, 

	T1	= ((1<<(Bit1+1))-1) ^ 0xFF,	/* 0000 0000 */
	Tx	= ((1<<(Bitx+1))-1) ^ 0xFF,	/* 1000 0000 */
	T2	= ((1<<(Bit2+1))-1) ^ 0xFF,	/* 1100 0000 */
	T3	= ((1<<(Bit3+1))-1) ^ 0xFF,	/* 1110 0000 */
	T4	= ((1<<(Bit4+1))-1) ^ 0xFF,	/* 1111 0000 */
	T5	= ((1<<(Bit5+1))-1) ^ 0xFF,	/* 1111 1000 */

	Rune1	= (1<<(Bit1+0*Bitx))-1,		/* 0000 0000 0111 1111 */
	Rune2	= (1<<(Bit2+1*Bitx))-1,		/* 0000 0111 1111 1111 */
	Rune3	= (1<<(Bit3+2*Bitx))-1,		/* 1111 1111 1111 1111 */
	Rune4	= (1<<(Bit4+3*Bitx))-1,
                                        /* 0001 1111 1111 1111 1111 1111 */

	Maskx	= (1<<Bitx)-1,			/* 0011 1111 */
	Testx	= Maskx ^ 0xFF,			/* 1100 0000 */

	Bad	= Runeerror,
};


// ===== from rune.c =====

/*
 * This is the older "unsafe" version, which works fine on 
 * null-terminated strings.
 */
int
chartorune(Rune *rune, const char *str)
{
	int c, c1, c2, c3;
	long l;

	/*
	 * one character sequence
	 *	00000-0007F => T1
	 */
	c = *(uchar*)str;
	if(c < Tx) {
		*rune = c;
		return 1;
	}

	/*
	 * two character sequence
	 *	0080-07FF => T2 Tx
	 */
	c1 = *(uchar*)(str+1) ^ Tx;
	if(c1 & Testx)
		goto bad;
	if(c < T3) {
		if(c < T2)
			goto bad;
		l = ((c << Bitx) | c1) & Rune2;
		if(l <= Rune1)
			goto bad;
		*rune = l;
		return 2;
	}

	/*
	 * three character sequence
	 *	0800-FFFF => T3 Tx Tx
	 */
	c2 = *(uchar*)(str+2) ^ Tx;
	if(c2 & Testx)
		goto bad;
	if(c < T4) {
		l = ((((c << Bitx) | c1) << Bitx) | c2) & Rune3;
		if(l <= Rune2)
			goto bad;
		*rune = l;
		return 3;
	}

	/*
	 * four character sequence (21-bit value)
	 *	10000-1FFFFF => T4 Tx Tx Tx
	 */
	c3 = *(uchar*)(str+3) ^ Tx;
	if (c3 & Testx)
		goto bad;
	if (c < T5) {
		l = ((((((c << Bitx) | c1) << Bitx) | c2) << Bitx) | c3) & Rune4;
		if (l <= Rune3)
			goto bad;
		*rune = l;
		return 4;
	}

	/*
	 * Support for 5-byte or longer UTF-8 would go here, but
	 * since we don't have that, we'll just fall through to bad.
	 */

	/*
	 * bad decoding
	 */
bad:
	*rune = Bad;
	return 1;
}

    
int
runetochar(char *str, const Rune *rune)
{
	/* Runes are signed, so convert to unsigned for range check. */
	unsigned long c;

	/*
	 * one character sequence
	 *	00000-0007F => 00-7F
	 */
	c = *rune;
	if(c <= Rune1) {
		str[0] = c;
		return 1;
	}

	/*
	 * two character sequence
	 *	0080-07FF => T2 Tx
	 */
	if(c <= Rune2) {
		str[0] = T2 | (c >> 1*Bitx);
		str[1] = Tx | (c & Maskx);
		return 2;
	}

	/*
	 * If the Rune is out of range, convert it to the error rune.
	 * Do this test here because the error rune encodes to three bytes.
	 * Doing it earlier would duplicate work, since an out of range
	 * Rune wouldn't have fit in one or two bytes.
	 */
	if (c > Runemax)
		c = Runeerror;

	/*
	 * three character sequence
	 *	0800-FFFF => T3 Tx Tx
	 */
	if (c <= Rune3) {
		str[0] = T3 |  (c >> 2*Bitx);
		str[1] = Tx | ((c >> 1*Bitx) & Maskx);
		str[2] = Tx |  (c & Maskx);
		return 3;
	}

	/*
	 * four character sequence (21-bit value)
	 *     10000-1FFFFF => T4 Tx Tx Tx
	 */
	str[0] = T4 | (c >> 3*Bitx);
	str[1] = Tx | ((c >> 2*Bitx) & Maskx);
	str[2] = Tx | ((c >> 1*Bitx) & Maskx);
	str[3] = Tx | (c & Maskx);
	return 4;
}

int
runelen(Rune rune)
{
	char str[10];

	return runetochar(str, &rune);
}

int
runenlen(const Rune *r, int nrune)
{
	int nb, c;

	nb = 0;
	while(nrune--) {
		c = *r++;
		if (c <= Rune1)
			nb++;
		else if (c <= Rune2)
			nb += 2;
		else if (c <= Rune3)
			nb += 3;
		else /* assert(c <= Rune4) */ 
			nb += 4;
	}
	return nb;
}

int
fullrune(const char *str, int n)
{
	if (n > 0) {
		int c = *(uchar*)str;
		if (c < Tx)
			return 1;
		if (n > 1) {
			if (c < T3)
				return 1;
			if (n > 2) {
				if (c < T4 || n > 3)
					return 1;
			}
		}
	}
	return 0;
}


// ===== from utflen.c =====

int
utflen(const char *s)
{
	int c;
	long n;
	Rune rune;

	n = 0;
	for(;;) {
		c = *(uchar*)s;
		if(c < Runeself) {
			if(c == 0)
				return n;
			s++;
		} else
			s += chartorune(&rune, s);
		n++;
	}
	return 0;
}


// ===== from utfnlen.c =====

int
utfnlen(const char *s, long m)
{
	int c;
	long n;
	Rune rune;
	const char *es;

	es = s + m;
	for(n = 0; s < es; n++) {
		c = *(uchar*)s;
		if(c < Runeself){
			if(c == '\0')
				break;
			s++;
			continue;
		}
		if(!fullrune(s, es-s))
			break;
		s += chartorune(&rune, s);
	}
	return n;
}


// ===== from utfrune.c =====

const
char*
utfrune(const char *s, Rune c)
{
	long c1;
	Rune r;
	int n;

	if(c < Runesync)		/* not part of utf sequence */
		return strchr(s, c);

	for(;;) {
		c1 = *(uchar*)s;
		if(c1 < Runeself) {	/* one byte rune */
			if(c1 == 0)
				return 0;
			if(c1 == c)
				return s;
			s++;
			continue;
		}
		n = chartorune(&r, s);
		if(r == c)
			return s;
		s += n;
	}
	return 0;
}


// ===== from runestrlen.c  and runestrchr.c =====

int
runestrlen(const Rune *s) {
  const Rune *t = s;

  while (*t++)
    ;
  return t-1-s;
}


// ===== from runetype.c =====

static
Rune*
rbsearch(Rune c, Rune *t, int n, int ne)
{
	Rune *p;
	int m;

	while(n > 1) {
		m = n >> 1;
		p = t + m*ne;
		if(c >= p[0]) {
			t = p;
			n = n-m;
		} else
			n = m;
	}
	if(n && c >= t[0])
		return t;
	return 0;
}


// ===== from runetypebody-5.0.0.c =====
// which was generated automatically by mkrunetype.c from UnicodeData-5.0.0.txt

static Rune __toupperr[] = {
	0x0061, 0x007a, 1048544,
	0x00e0, 0x00f6, 1048544,
	0x00f8, 0x00fe, 1048544,
	0x0256, 0x0257, 1048371,
	0x028a, 0x028b, 1048359,
	0x037b, 0x037d, 1048706,
	0x03ad, 0x03af, 1048539,
	0x03b1, 0x03c1, 1048544,
	0x03c3, 0x03cb, 1048544,
	0x03cd, 0x03ce, 1048513,
	0x0430, 0x044f, 1048544,
	0x0450, 0x045f, 1048496,
	0x0561, 0x0586, 1048528,
	0x1f00, 0x1f07, 1048584,
	0x1f10, 0x1f15, 1048584,
	0x1f20, 0x1f27, 1048584,
	0x1f30, 0x1f37, 1048584,
	0x1f40, 0x1f45, 1048584,
	0x1f60, 0x1f67, 1048584,
	0x1f70, 0x1f71, 1048650,
	0x1f72, 0x1f75, 1048662,
	0x1f76, 0x1f77, 1048676,
	0x1f78, 0x1f79, 1048704,
	0x1f7a, 0x1f7b, 1048688,
	0x1f7c, 0x1f7d, 1048702,
	0x1f80, 0x1f87, 1048584,
	0x1f90, 0x1f97, 1048584,
	0x1fa0, 0x1fa7, 1048584,
	0x1fb0, 0x1fb1, 1048584,
	0x1fd0, 0x1fd1, 1048584,
	0x1fe0, 0x1fe1, 1048584,
	0x2170, 0x217f, 1048560,
	0x24d0, 0x24e9, 1048550,
	0x2c30, 0x2c5e, 1048528,
	0x2d00, 0x2d25, 1041312,
	0xff41, 0xff5a, 1048544,
	0x10428, 0x1044f, 1048536,
};

static Rune __toupperp[] = {
	0x0101, 0x012f, 1048575,
	0x0133, 0x0137, 1048575,
	0x013a, 0x0148, 1048575,
	0x014b, 0x0177, 1048575,
	0x017a, 0x017e, 1048575,
	0x0183, 0x0185, 1048575,
	0x01a1, 0x01a5, 1048575,
	0x01b4, 0x01b6, 1048575,
	0x01ce, 0x01dc, 1048575,
	0x01df, 0x01ef, 1048575,
	0x01f9, 0x021f, 1048575,
	0x0223, 0x0233, 1048575,
	0x0247, 0x024f, 1048575,
	0x03d9, 0x03ef, 1048575,
	0x0461, 0x0481, 1048575,
	0x048b, 0x04bf, 1048575,
	0x04c2, 0x04ce, 1048575,
	0x04d1, 0x0513, 1048575,
	0x1e01, 0x1e95, 1048575,
	0x1ea1, 0x1ef9, 1048575,
	0x1f51, 0x1f57, 1048584,
	0x2c68, 0x2c6c, 1048575,
	0x2c81, 0x2ce3, 1048575,
};

static Rune __touppers[] = {
	0x00b5, 1049319,
	0x00ff, 1048697,
	0x0131, 1048344,
	0x017f, 1048276,
	0x0180, 1048771,
	0x0188, 1048575,
	0x018c, 1048575,
	0x0192, 1048575,
	0x0195, 1048673,
	0x0199, 1048575,
	0x019a, 1048739,
	0x019e, 1048706,
	0x01a8, 1048575,
	0x01ad, 1048575,
	0x01b0, 1048575,
	0x01b9, 1048575,
	0x01bd, 1048575,
	0x01bf, 1048632,
	0x01c5, 1048575,
	0x01c6, 1048574,
	0x01c8, 1048575,
	0x01c9, 1048574,
	0x01cb, 1048575,
	0x01cc, 1048574,
	0x01dd, 1048497,
	0x01f2, 1048575,
	0x01f3, 1048574,
	0x01f5, 1048575,
	0x023c, 1048575,
	0x0242, 1048575,
	0x0253, 1048366,
	0x0254, 1048370,
	0x0259, 1048374,
	0x025b, 1048373,
	0x0260, 1048371,
	0x0263, 1048369,
	0x0268, 1048367,
	0x0269, 1048365,
	0x026b, 1059319,
	0x026f, 1048365,
	0x0272, 1048363,
	0x0275, 1048362,
	0x027d, 1059303,
	0x0280, 1048358,
	0x0283, 1048358,
	0x0288, 1048358,
	0x0289, 1048507,
	0x028c, 1048505,
	0x0292, 1048357,
	0x0345, 1048660,
	0x03ac, 1048538,
	0x03c2, 1048545,
	0x03cc, 1048512,
	0x03d0, 1048514,
	0x03d1, 1048519,
	0x03d5, 1048529,
	0x03d6, 1048522,
	0x03f0, 1048490,
	0x03f1, 1048496,
	0x03f2, 1048583,
	0x03f5, 1048480,
	0x03f8, 1048575,
	0x03fb, 1048575,
	0x04cf, 1048561,
	0x1d7d, 1052390,
	0x1e9b, 1048517,
	0x1fb3, 1048585,
	0x1fbe, 1041371,
	0x1fc3, 1048585,
	0x1fe5, 1048583,
	0x1ff3, 1048585,
	0x214e, 1048548,
	0x2184, 1048575,
	0x2c61, 1048575,
	0x2c65, 1037781,
	0x2c66, 1037784,
	0x2c76, 1048575,
};

Rune
toupperrune(Rune c)
{
	Rune *p;

	p = rbsearch(c, __toupperr, nelem(__toupperr)/3, 3);
	if(p && c >= p[0] && c <= p[1])
		return c + p[2] - 1048576;
	p = rbsearch(c, __toupperp, nelem(__toupperp)/3, 3);
	if(p && c >= p[0] && c <= p[1] && !((c - p[0]) & 1))
		return c + p[2] - 1048576;
	p = rbsearch(c, __touppers, nelem(__touppers)/2, 2);
	if(p && c == p[0])
		return c + p[1] - 1048576;
	return c;
}

static Rune __tolowerr[] = {
	0x0041, 0x005a, 1048608,
	0x00c0, 0x00d6, 1048608,
	0x00d8, 0x00de, 1048608,
	0x0189, 0x018a, 1048781,
	0x01b1, 0x01b2, 1048793,
	0x0388, 0x038a, 1048613,
	0x038e, 0x038f, 1048639,
	0x0391, 0x03a1, 1048608,
	0x03a3, 0x03ab, 1048608,
	0x03fd, 0x03ff, 1048446,
	0x0400, 0x040f, 1048656,
	0x0410, 0x042f, 1048608,
	0x0531, 0x0556, 1048624,
	0x10a0, 0x10c5, 1055840,
	0x1f08, 0x1f0f, 1048568,
	0x1f18, 0x1f1d, 1048568,
	0x1f28, 0x1f2f, 1048568,
	0x1f38, 0x1f3f, 1048568,
	0x1f48, 0x1f4d, 1048568,
	0x1f68, 0x1f6f, 1048568,
	0x1f88, 0x1f8f, 1048568,
	0x1f98, 0x1f9f, 1048568,
	0x1fa8, 0x1faf, 1048568,
	0x1fb8, 0x1fb9, 1048568,
	0x1fba, 0x1fbb, 1048502,
	0x1fc8, 0x1fcb, 1048490,
	0x1fd8, 0x1fd9, 1048568,
	0x1fda, 0x1fdb, 1048476,
	0x1fe8, 0x1fe9, 1048568,
	0x1fea, 0x1feb, 1048464,
	0x1ff8, 0x1ff9, 1048448,
	0x1ffa, 0x1ffb, 1048450,
	0x2160, 0x216f, 1048592,
	0x24b6, 0x24cf, 1048602,
	0x2c00, 0x2c2e, 1048624,
	0xff21, 0xff3a, 1048608,
	0x10400, 0x10427, 1048616,
};

static Rune __tolowerp[] = {
	0x0100, 0x012e, 1048577,
	0x0132, 0x0136, 1048577,
	0x0139, 0x0147, 1048577,
	0x014a, 0x0176, 1048577,
	0x017b, 0x017d, 1048577,
	0x01a2, 0x01a4, 1048577,
	0x01b3, 0x01b5, 1048577,
	0x01cd, 0x01db, 1048577,
	0x01de, 0x01ee, 1048577,
	0x01f8, 0x021e, 1048577,
	0x0222, 0x0232, 1048577,
	0x0248, 0x024e, 1048577,
	0x03d8, 0x03ee, 1048577,
	0x0460, 0x0480, 1048577,
	0x048a, 0x04be, 1048577,
	0x04c3, 0x04cd, 1048577,
	0x04d0, 0x0512, 1048577,
	0x1e00, 0x1e94, 1048577,
	0x1ea0, 0x1ef8, 1048577,
	0x1f59, 0x1f5f, 1048568,
	0x2c67, 0x2c6b, 1048577,
	0x2c80, 0x2ce2, 1048577,
};

static Rune __tolowers[] = {
	0x0130, 1048377,
	0x0178, 1048455,
	0x0179, 1048577,
	0x0181, 1048786,
	0x0182, 1048577,
	0x0184, 1048577,
	0x0186, 1048782,
	0x0187, 1048577,
	0x018b, 1048577,
	0x018e, 1048655,
	0x018f, 1048778,
	0x0190, 1048779,
	0x0191, 1048577,
	0x0193, 1048781,
	0x0194, 1048783,
	0x0196, 1048787,
	0x0197, 1048785,
	0x0198, 1048577,
	0x019c, 1048787,
	0x019d, 1048789,
	0x019f, 1048790,
	0x01a0, 1048577,
	0x01a6, 1048794,
	0x01a7, 1048577,
	0x01a9, 1048794,
	0x01ac, 1048577,
	0x01ae, 1048794,
	0x01af, 1048577,
	0x01b7, 1048795,
	0x01b8, 1048577,
	0x01bc, 1048577,
	0x01c4, 1048578,
	0x01c5, 1048577,
	0x01c7, 1048578,
	0x01c8, 1048577,
	0x01ca, 1048578,
	0x01cb, 1048577,
	0x01f1, 1048578,
	0x01f2, 1048577,
	0x01f4, 1048577,
	0x01f6, 1048479,
	0x01f7, 1048520,
	0x0220, 1048446,
	0x023a, 1059371,
	0x023b, 1048577,
	0x023d, 1048413,
	0x023e, 1059368,
	0x0241, 1048577,
	0x0243, 1048381,
	0x0244, 1048645,
	0x0245, 1048647,
	0x0246, 1048577,
	0x0386, 1048614,
	0x038c, 1048640,
	0x03f4, 1048516,
	0x03f7, 1048577,
	0x03f9, 1048569,
	0x03fa, 1048577,
	0x04c0, 1048591,
	0x04c1, 1048577,
	0x1fbc, 1048567,
	0x1fcc, 1048567,
	0x1fec, 1048569,
	0x1ffc, 1048567,
	0x2126, 1041059,
	0x212a, 1040193,
	0x212b, 1040314,
	0x2132, 1048604,
	0x2183, 1048577,
	0x2c60, 1048577,
	0x2c62, 1037833,
	0x2c63, 1044762,
	0x2c64, 1037849,
	0x2c75, 1048577,
};

Rune
tolowerrune(Rune c)
{
	Rune *p;

	p = rbsearch(c, __tolowerr, nelem(__tolowerr)/3, 3);
	if(p && c >= p[0] && c <= p[1])
		return c + p[2] - 1048576;
	p = rbsearch(c, __tolowerp, nelem(__tolowerp)/3, 3);
	if(p && c >= p[0] && c <= p[1] && !((c - p[0]) & 1))
		return c + p[2] - 1048576;
	p = rbsearch(c, __tolowers, nelem(__tolowers)/2, 2);
	if(p && c == p[0])
		return c + p[1] - 1048576;
	return c;
}

