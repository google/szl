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
#include <stdlib.h>
#include "fmt/fmt.h"
#include "fmt/fmtdef.h"

namespace Fmt {

/* format the output into f->to and return the number of characters fmted  */
int
dofmt(Formatter* formatter, State *f, const char *fmt)
{
	Rune rune, *rt, *rs;
	int r;
	char *t, *s;
	int n, nfmt;

	nfmt = f->nfmt;
	if (formatter == NULL)
		formatter = &_stdfmt;
	for(;;){
		if(f->runes){
			rt = (Rune*)f->to;
			rs = (Rune*)f->stop;
			while((r = *(uchar*)fmt) && r != '%'){
				if(r < Runeself)
					fmt++;
				else{
					fmt += chartorune(&rune, fmt);
					r = rune;
				}
				FMTRCHAR(f, rt, rs, r);
			}
			fmt++;
			f->nfmt += rt - (Rune *)f->to;
			f->to = rt;
			if(!r){
				return f->nfmt - nfmt;
			}
		}else{
			t = (char*)f->to;
			s = (char*)f->stop;
			while((r = *(uchar*)fmt) && r != '%'){
				if(r < Runeself){
					FMTCHAR(f, t, s, r);
					fmt++;
				}else{
					n = chartorune(&rune, fmt);
					if(t + n > s){
						t = (char*)__fmtflush(f, t, n);
						if(t != nil)
							s = (char*)f->stop;
						else{
							return -1;
						}
					}
					while(n--)
						*t++ = *fmt++;
				}
			}
			fmt++;
			f->nfmt += t - (char *)f->to;
			f->to = t;
			if(!r){
				return f->nfmt - nfmt;
			}
		}

		/*
		 * If reordering args as in '%1$d', dispatch will finish
		 * the formatting and return "" or nil.
		 */
		fmt = (char*)dispatch(formatter, f, fmt, 0, nil, 0);
		if(fmt == nil){
			return -1;
		}
	}
	return 0;	/* not reached */
}

/* Shared buffer for null */
static Rune nullbuf[32];

/* Null flush routine for fmtnullinit */
static int
__fmtNullFlush(State *f)
{
	f->to = f->start;
	return 1;
}

/* Set up f to absorb stuff without using resources */
int
Formatter::fmtnullinit(State *f)
{
	memset(f, 0, sizeof(State));  /* make sure flags, etc. are all zero */
	f->runes = 1;
	f->start = nullbuf;
	f->to = nullbuf;
	f->stop = nullbuf + sizeof nullbuf / sizeof nullbuf[0];
	f->flush = __fmtNullFlush;
	f->farg = 0;
	f->flags = 0;
	f->nfmt = 0;
	f->formatter = this;
	fmtlocaleinit(f, nil, nil, nil);
	return 0;
}

/* Set up f to absorb stuff without using resources */
int
fmtnullinit(State *f)
{
	return _stdfmt.fmtnullinit(f);
}

void *
__fmtflush(State *f, void *t, int len)
{
	if(f->runes)
		f->nfmt += (Rune*)t - (Rune*)f->to;
	else
		f->nfmt += (char*)t - (char *)f->to;
	f->to = t;
	if(f->flush == 0 || (*f->flush)(f) == 0 || (char*)f->to + len > (char*)f->stop){
		f->stop = f->to;
		return nil;
	}
	return f->to;
}

/*
 * put a formatted block of memory sz bytes long of n runes into the output buffer,
 * left/right justified in a field of at least f->width characters (if FmtWidth is set)
 */
int
__fmtpad(State *f, int n)
{
	char *t, *s;
	int i;

	t = (char*)f->to;
	s = (char*)f->stop;
	for(i = 0; i < n; i++)
		FMTCHAR(f, t, s, ' ');
	f->nfmt += t - (char *)f->to;
	f->to = t;
	return 0;
}

int
__rfmtpad(State *f, int n)
{
	Rune *t, *s;
	int i;

	t = (Rune*)f->to;
	s = (Rune*)f->stop;
	for(i = 0; i < n; i++)
		FMTRCHAR(f, t, s, ' ');
	f->nfmt += t - (Rune *)f->to;
	f->to = t;
	return 0;
}

int
__fmtcpy(State *f, const void *vm, int n, int sz)
{
	Rune *rt, *rs, r;
	char *t, *s, *m, *me;
	ulong fl;
	int nc, w;

	m = (char*)vm;
	me = m + sz;
	fl = f->flags;
	w = 0;
	if (fl & FmtWidth)
		w = f->width;
	if((fl & FmtPrec) && n > f->prec)
		n = f->prec;
	if(f->runes){
		if(!(fl & FmtLeft) && __rfmtpad(f, w - n) < 0)
			return -1;
		rt = (Rune*)f->to;
		rs = (Rune*)f->stop;
		for(nc = n; nc > 0; nc--){
			r = *(uchar*)m;
			if(r < Runeself)
				m++;
			else if((me - m) >= UTFmax || fullrune(m, me-m))
				m += chartorune(&r, m);
			else
				break;
			FMTRCHAR(f, rt, rs, r);
		}
		f->nfmt += rt - (Rune *)f->to;
		f->to = rt;
		if(fl & FmtLeft && __rfmtpad(f, w - n) < 0)
			return -1;
	}else{
		if(!(fl & FmtLeft) && __fmtpad(f, w - n) < 0)
			return -1;
		t = (char*)f->to;
		s = (char*)f->stop;
		for(nc = n; nc > 0; nc--){
			r = *(uchar*)m;
			if(r < Runeself)
				m++;
			else if((me - m) >= UTFmax || fullrune(m, me-m))
				m += chartorune(&r, m);
			else
				break;
			FMTRUNE(f, t, s, r);
		}
		f->nfmt += t - (char *)f->to;
		f->to = t;
		if(fl & FmtLeft && __fmtpad(f, w - n) < 0)
			return -1;
	}
	return 0;
}

int
__fmtrcpy(State *f, const void *vm, int n)
{
	Rune r, *m, *me, *rt, *rs;
	char *t, *s;
	ulong fl;
	int w;

	m = (Rune*)vm;
	fl = f->flags;
	w = 0;
	if (fl & FmtWidth)
		w = f->width;
	if((fl & FmtPrec) && n > f->prec)
		n = f->prec;
	if(f->runes){
		if(!(fl & FmtLeft) && __rfmtpad(f, w - n) < 0)
			return -1;
		rt = (Rune*)f->to;
		rs = (Rune*)f->stop;
		for(me = m + n; m < me; m++)
			FMTRCHAR(f, rt, rs, *m);
		f->nfmt += rt - (Rune *)f->to;
		f->to = rt;
		if(fl & FmtLeft && __rfmtpad(f, w - n) < 0)
			return -1;
	}else{
		if(!(fl & FmtLeft) && __fmtpad(f, w - n) < 0)
			return -1;
		t = (char*)f->to;
		s = (char*)f->stop;
		for(me = m + n; m < me; m++){
			r = *m;
			FMTRUNE(f, t, s, r);
		}
		f->nfmt += t - (char *)f->to;
		f->to = t;
		if(fl & FmtLeft && __fmtpad(f, w - n) < 0)
			return -1;
	}
	return 0;
}

/* fmt out one character */
int
__charfmt(State *f)
{
	char x[1];

	x[0] = FMT_ARG(f, int);
	f->prec = 1;
	return __fmtcpy(f, (const char*)x, 1, 1);
}

/* fmt out one rune */
int
__runefmt(State *f)
{
	Rune x[1];

	x[0] = FMT_ARG(f, int);
	return __fmtrcpy(f, (const void*)x, 1);
}

/* public helper routine: fmt out a null terminated string already in hand */
int
fmtstrcpy(State *f, const char *s)
{
	int p, i;
	if(!s)
		return __fmtcpy(f, "<nil>", 5, 5);
	/* if precision is specified, make sure we don't wander off the end */
	if(f->flags & FmtPrec){
		p = f->prec;
		for(i = 0; i < p; i++)
			if(s[i] == 0)
				break;
		return __fmtcpy(f, s, utfnlen(s, i), i);	/* BUG?: won't print a partial rune at end */
	}

	return __fmtcpy(f, s, utflen(s), strlen(s));
}

/* fmt out a null terminated utf string */
int
__strfmt(State *f)
{
	char *s;

	s = FMT_ARG(f, char*);
	return fmtstrcpy(f, s);
}

/* public helper routine: fmt out a null terminated rune string already in hand */
int
fmtrunestrcpy(State *f, const Rune *s)
{
	const Rune *e;
	int n, p;

	if(!s)
		return __fmtcpy(f, "<nil>", 5, 5);
	/* if precision is specified, make sure we don't wander off the end */
	if(f->flags & FmtPrec){
		p = f->prec;
		for(n = 0; n < p; n++)
			if(s[n] == 0)
				break;
	}else{
		for(e = s; *e; e++)
			;
		n = e - s;
	}
	return __fmtrcpy(f, s, n);
}

/* fmt out a null terminated rune string */
int
__runesfmt(State *f)
{
	Rune *s;

	s = FMT_ARG(f, Rune*);
	return fmtrunestrcpy(f, s);
}

/* fmt a % */
int
__percentfmt(State *f)
{
	Rune x[1];

	x[0] = f->r;
	f->prec = 1;
	return __fmtrcpy(f, (const void*)x, 1);
}

/* fmt an integer */
int
__ifmt(State *f)
{
	char buf[140], *p;	/* big enough for 64 bits of binary with a 3-byte sep every 4 digits */
	const char *conv;
	uvlong vu;
	ulong u;
	int neg, base, i, n, fl, w, isv, ndig, len, excess, bytelen;
	const char *grouping;
	const char *thousands;

	neg = 0;
	fl = f->flags;
	isv = 0;
	vu = 0;
	u = 0;
	/*
	 * Unsigned verbs
	 */
	switch(f->r){
	case 'o':
	case 'u':
	case 'x':
	case 'X':
		fl |= FmtUnsigned;
		break;
	}
	if(f->r == 'p'){
		u = (ulong)FMT_ARG(f, void*);
		f->r = 'x';
		fl |= FmtUnsigned;
	}else if(fl & FmtVLong){
		isv = 1;
		if(fl & FmtUnsigned)
			vu = FMT_ARG(f, uvlong);
		else
			vu = FMT_ARG(f, vlong);
	}else if(fl & FmtLong){
		if(fl & FmtUnsigned)
			u = FMT_ARG(f, ulong);
		else
			u = FMT_ARG(f, long);
	}else if(fl & FmtByte){
		if(fl & FmtUnsigned)
			u = (uchar)FMT_ARG(f, int);
		else
			u = (char)FMT_ARG(f, int);
	}else if(fl & FmtShort){
		if(fl & FmtUnsigned)
			u = (ushort)FMT_ARG(f, int);
		else
			u = (short)FMT_ARG(f, int);
	}else{
		if(fl & FmtUnsigned)
			u = FMT_ARG(f, uint);
		else
			u = FMT_ARG(f, int);
	}
	conv = "0123456789abcdef";
	grouping = "\4";	/* for hex, octal etc. (undefined by spec but nice) */
	thousands = f->thousands;
	switch(f->r){
	case 'd':
	case 'i':
	case 'u':
		base = 10;
		grouping = f->grouping;
		break;
	case 'X':
		conv = "0123456789ABCDEF";
		/* fall through */
	case 'x':
		base = 16;
		thousands = ":";
		break;
	case 'b':
		base = 2;
		thousands = ":";
		break;
	case 'o':
		base = 8;
		break;
	default:
		return -1;
	}
	if(!(fl & FmtUnsigned)){
		if(isv && (vlong)vu < 0){
			vu = -(vlong)vu;
			neg = 1;
		}else if(!isv && (long)u < 0){
			u = -(long)u;
			neg = 1;
		}
	}else{
		fl &= ~(FmtSign|FmtSpace);	/* no + for unsigned conversions */
	}
	p = buf + sizeof buf - 1;
	n = 0;	/* in runes */
	excess = 0;	/* number of bytes > number of runes */
	ndig = 0;
	len = utflen(thousands);
	bytelen = strlen(thousands);
	if(isv){
		while(vu){
			i = vu % base;
			vu /= base;
			if((fl & FmtApost) && __needsep(&ndig, &grouping)){
				n += len;
				excess += bytelen - len;
				p -= bytelen;
				memmove(p+1, thousands, bytelen);
			}
			*p-- = conv[i];
			n++;
		}
	}else{
		while(u){
			i = u % base;
			u /= base;
			if((fl & FmtApost) && __needsep(&ndig, &grouping)){
				n += len;
				excess += bytelen - len;
				p -= bytelen;
				memmove(p+1, thousands, bytelen);
			}
			*p-- = conv[i];
			n++;
		}
	}
	if(n == 0){
		if(!(fl & FmtPrec) || f->prec != 0){
			*p-- = '0';
			n = 1;
			if(fl & FmtApost)
				__needsep(&ndig, &grouping);
		}
		fl &= ~FmtSharp;
	}
	for(w = f->prec; n < w && p > buf+3; n++){
		if((fl & FmtApost) && __needsep(&ndig, &grouping)){
			n += len;
			excess += bytelen - len;
			p -= bytelen;
			memmove(p+1, thousands, bytelen);
		}
		*p-- = '0';
	}
	if(neg || (fl & (FmtSign|FmtSpace)))
		n++;
	if(fl & FmtSharp){
		if(base == 16)
			n += 2;
		else if(base == 8){
			if(p[1] == '0')
				fl &= ~FmtSharp;
			else
				n++;
		}
	}
	if((fl & FmtZero) && !(fl & (FmtLeft|FmtPrec))){
		w = 0;
		if (fl & FmtWidth)
			w = f->width;
		for(; n < w && p > buf+3; n++){
			if((fl & FmtApost) && __needsep(&ndig, &grouping)){
				n += len;
				excess += bytelen - len;
				p -= bytelen;
				memmove(p+1, thousands, bytelen);
			}
			*p-- = '0';
		}
		f->flags &= ~FmtWidth;
	}
	if(fl & FmtSharp){
		if(base == 16)
			*p-- = f->r;
		if(base == 16 || base == 8)
			*p-- = '0';
	}
	if(neg)
		*p-- = '-';
	else if(fl & FmtSign)
		*p-- = '+';
	else if(fl & FmtSpace)
		*p-- = ' ';
	f->flags &= ~FmtPrec;

	return __fmtcpy(f, p + 1, n, n + excess);
}

int
__countfmt(State *f)
{
	void *p;
	ulong fl;

	fl = f->flags;
	p = FMT_ARG(f, void*);
	if(fl & FmtVLong){
		*(vlong*)p = f->nfmt;
	}else if(fl & FmtLong){
		*(long*)p = f->nfmt;
	}else if(fl & FmtByte){
		*(char*)p = f->nfmt;
	}else if(fl & FmtShort){
		*(short*)p = f->nfmt;
	}else{
		*(int*)p = f->nfmt;
	}
	return 0;
}

int
__flagfmt(State *f)
{
	switch(f->r){
	case '-':
		f->flags |= FmtLeft;
		break;
	case '+':
		f->flags |= FmtSign;
		break;
	case '#':
		f->flags |= FmtSharp;
		break;
	case '\'':
		f->flags |= FmtApost;
		break;
	case ' ':
		f->flags |= FmtSpace;
		break;
	case 'u':
		f->flags |= FmtUnsigned;
		break;
	case 'h':
		if(f->flags & FmtShort)
			f->flags |= FmtByte;
		f->flags |= FmtShort;
		break;
	case 'L':
		f->flags |= FmtLDouble;
		break;
	case 'l':
		if(f->flags & FmtLong)
			f->flags |= FmtVLong;
		f->flags |= FmtLong;
		break;
	}
	return 1;
}

/* default error format */
int
__badfmt(State *f)
{
	char x[3];

	x[0] = '%';
	x[1] = f->r;
	x[2] = '%';
	f->prec = 3;
	__fmtcpy(f, (const void*)x, 3, 3);
	return 0;
}

}  /* end namespace Fmt */
