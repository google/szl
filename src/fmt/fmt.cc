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
#include <unistd.h>
#include "fmt/fmt.h"
#include "fmt/fmtdef.h"

namespace Fmt {

Formatter _stdfmt;

Formatter::Formatter() {
	this->nfmt = 0;
	memset(this->fmt, 0, sizeof this->fmt);
}

Formatter::~Formatter() {
}

static Convfmt knownfmt[] = {
	{ ' ',	 true, false, __flagfmt		},
	{ '#',	 true, false, __flagfmt		},
	{ '%',	false, false, __percentfmt	},
	{ '\'',	 true, false, __flagfmt		},
	{ '+',	 true, false, __flagfmt		},
	{ '-',	 true, false, __flagfmt		},
	{ 'C',	false,  true, __runefmt		},	/* Plan 9 addition */
	{ 'E',	false,  true, __efgfmt		},
	{ 'F',	false,  true, __efgfmt		},	/* ANSI only */
	{ 'G',	false,  true, __efgfmt		},
	{ 'L',	 true, false, __flagfmt		},	/* ANSI only */
	{ 'S',	false,  true, __runesfmt	},	/* Plan 9 addition */
	{ 'X',	false,  true, __ifmt		},
	{ 'b',	false,  true, __ifmt		},	/* Plan 9 addition */
	{ 'c',	false,  true, __charfmt		},
	{ 'd',	false,  true, __ifmt		},
	{ 'e',	false,  true, __efgfmt		},
	{ 'f',	false,  true, __efgfmt		},
	{ 'g',	false,  true, __efgfmt		},
	{ 'h',	 true, false, __flagfmt		},
	{ 'i',	false,  true, __ifmt		},	/* ANSI only */
	{ 'l',	 true, false, __flagfmt		},
	{ 'n',	false,  true, __countfmt	},
	{ 'o',	false,  true, __ifmt		},
	{ 'p',	false,  true, __ifmt		},
	{ 'r',	false, false, __errfmt		},
	{ 's',	false,  true, __strfmt		},
	{ 'u',	false,  true, __ifmt		},	/* in Plan 9, __flagfmt */
	{ 'x',	false,  true, __ifmt		},
	{ 0,	false, false, nil		},
};

static Convfmt badconvfmt = { 0, false, false, __badfmt };

static int
doinstallfmt(Formatter* formatter, int c, bool isflag, bool consumesarg, Fmts f)
{
	Convfmt *p, *ep;

	if(c<=0 || c>=65536)
		return -1;
	if(!f)
		f = __badfmt;

	ep = &formatter->fmt[formatter->nfmt];
	for(p=formatter->fmt; p<ep; p++)
		if(p->c == c)
			break;

	if(p == &formatter->fmt[Maxfmt])
		return -1;

	if(p->fmt && p->fmt != f){
		/* note: this test only catches an explicit install; if someone
		 * overrides %d and then someone else expects %d to be the
		 * default, the second someone will be surprised.  to be fixed.
		 */
		/* print without calling install */
		int n;
		Rune r;
		char m1[] = "Fmt: Warning: verb %";
		char m2[UTFmax+1];
		r = c;
		n = runetochar(m2, &r);
		m2[n] = '\0';
		char m3[] =  " redefined\n";
		write(2, m1, strlen(m1));
		write(2, m2, strlen(m2));
		write(2, m3, strlen(m3));
	}
	p->fmt = f;
	p->isflag = isflag;
	p->consumesarg = consumesarg;
	if(p == ep){ /* installing a new format character */
		formatter->nfmt++;
		p->c = c;
	}

	return 0;
}

int
Formatter::installverb(int c, bool consumesarg, Fmts f)
{
	int ret;

	ret = doinstallfmt(this, c, false, consumesarg, f);
	return ret;
}

int
Formatter::installflag(int c, bool consumesarg, Fmts f)
{
	int ret;

	ret = doinstallfmt(this, c, true, consumesarg, f);
	return ret;
}

Convfmt*
fmtfmt(Formatter* formatter, int c)
{
	Convfmt *p, *ep;

	ep = &formatter->fmt[formatter->nfmt];
	for(p=formatter->fmt; p<ep; p++)
		if(p->c == c)
			return p;

	/* is this a predefined format char? */
	for(p=knownfmt; p->c; p++)
		if(p->c == c)
			return p;

	return &badconvfmt;
}

static const void*
nextrune(const void *fmt, int isrunes, Rune *runep) {
	if(isrunes){
		*runep = *(Rune*)fmt;
		fmt = (Rune*)fmt + 1;
	}else
		fmt = (char*)fmt + chartorune(runep, (char*)fmt);
	return fmt;
}

// *runep already holds next rune; always sets it to 0 if no number
static const void*
nextnumber(const void *fmt, int isrunes, Rune *runep, int *intp){
	int i;
	
	i = 0;
	while('0' <= *runep && *runep <= '9'){
		i = i * 10 + *runep - '0';
		fmt = nextrune(fmt, isrunes, runep);
	}
	*intp = i;
	return fmt;
}

/*
 * always sets *dollarp to 0 if no $ present.
 * *runep has not yet been read or consumed.
 * upon return, *runep contains the next rune.
 */
static const void*
getdollar(const void *fmt, int isrunes, Rune *runep, int *dollarp){
	int oldrune;

	fmt = nextrune(fmt, isrunes, runep);
	const void *oldfmt = fmt;
	oldrune = *runep;
	fmt = nextnumber(fmt, isrunes, runep, dollarp);
	if(*runep != '$'){
		*dollarp = 0;
		*runep = oldrune;
		return oldfmt;
	}
	return nextrune(fmt, isrunes, runep);
}

static const void**
addverb(const void **verbs, int *nverbs,  int argno, const void *fmt)
{
	const void** oldverbs;
	int n;

	if(verbs == nil || argno >= *nverbs){
		n = 2 * (argno + 1);
		oldverbs = verbs;
		verbs = (const void**)realloc(verbs, n * sizeof verbs[0]);
		if(verbs == nil){
			free(oldverbs);
			return nil;
		}
		memset(verbs + *nverbs, 0, (n - *nverbs) * sizeof verbs[0]);
		*nverbs = n;
	}
	/*
	 * Needed only to consume arguments.  If there are two verbs for
	 * a given argument, they need to consume the same number of
	 * words or the user deserves whatever happens.
	 */
	if(verbs[argno] == nil)
		verbs[argno] = fmt;
	return verbs;
}

/* dofmt for a format that includes %1$d -style stuff */
static int
doreorderfmt(Formatter* formatter, State *f, const void *fmt, int isrunes, va_list* args, int nargs)
{
	Rune *rt, *rs;
	int r;
	char *t, *s;

	for(;;){
		if(f->runes){
			rt = (Rune*)f->to;
			rs = (Rune*)f->stop;
			for(;;){
				fmt = nextrune(fmt, isrunes, &r);
				if(r == '\0'){
					f->nfmt += rt - (Rune *)f->to;
					f->to = rt;
					return 1;
				}
				if(r == '%'){
					break;
				}
				FMTRCHAR(f, rt, rs, r);
			}
			f->nfmt += rt - (Rune *)f->to;
			f->to = rt;
		}else{
			t = (char*)f->to;
			s = (char*)f->stop;
			for(;;){
				fmt = nextrune(fmt, isrunes, &r);
				if(r == '\0'){
					f->nfmt += t - (char *)f->to;
					f->to = t;
					return 1;
				}
				if(r == '%'){
					break;
				}
				if(r < Runeself){
					FMTCHAR(f, t, s, r);
				}else{
					FMTRUNE(f, t, s, r);
				}
			}
			f->nfmt += t - (char *)f->to;
			f->to = t;
		}

		fmt = dispatch(formatter, f, fmt, isrunes, args, nargs);
		if(fmt == nil){
			return -1;
		}
	}

	return 1;
}

/*
 * Call va_end on the first nargs elements of args and free args
 */
void cleanupargs(va_list* args, int nargs) {
	int a;

	for(a = 1; a < nargs; a++){
		va_end(args[a]);
	}
	free(args);
}

/*
 * 'Fmt' has some %1$d -style formats; it points right after the '%' for the
 * first one.  Find all arguments and their locations, then produce the
 * formatted output.  Returns the empty string or nil if an error is found.
 */
static Rune emptyrunes[1];
static const void*
reorderfmt(Formatter* formatter, State* ff, const void *fmt, int isrunes)
{
	State nullf;
	va_list* args;
	Rune r;
	const void *origfmt, *argstart, **verbs;
	int a, i, allocedv, argno, maxverb, maxflag, nargs, dollar, star;

	/*
	 * backup over the preceding %
	 */
	if(isrunes)
		fmt = (Rune*)fmt - 1;
	else
		fmt = (char*)fmt - 1;

	/*
	 * Build a vector of verbs in argument order.
	 *
	 * Ignore any width or precision flags.  Later we'll assume all missing
	 * args are such flags.
	 */
	origfmt = fmt;
	verbs = nil;
	allocedv = 0;
	maxverb = 0;
	for (;;) {
		fmt = nextrune(fmt, isrunes, &r);
		if(r == '\0')
			break;
		if(r != '%')
			continue;

		/*
		 * look for potential number$
		 */
		argstart = fmt;
		fmt = nextrune(fmt, isrunes, &r);
		fmt = nextnumber(fmt, isrunes, &r, &argno);
		if(r != '$' || argno == 0){
			/*
			 * not valid $ verb syntax, assume the verb
			 * doesn't consume an arg
			 */
			continue;
		}

		verbs = addverb(verbs, &allocedv, argno, argstart);
		if(verbs == nil){
			return nil;
		}
		if(argno >= maxverb)
			maxverb = argno + 1;
	}

	/*
	 * Allocate enough args for a width and precision flag for every verb
	 */
	nargs = 3 * maxverb;	/* verb + width + precision -> 3 args per verb */
	if(nargs < 2)
		nargs = 2;
	args = (va_list*)malloc(nargs * sizeof(va_list));
	if(args == nil){
		free(verbs);
		return nil;
	}

	/*
	 * Run over each of the formatting verbs in argument order
	 * and record their va_list positions.
	 */
	formatter->fmtnullinit(&nullf);
	nullf.args = ff->args;
	maxflag = maxverb;
	if(maxflag < 2)
		maxflag = 2;
	for(a = 1; a < maxverb; ) {
		VA_COPY(args[a], *nullf.args);
		fmt = verbs[a];
		a++;
		if(fmt == nil){		/* a width or precision flag */
			va_arg(*nullf.args, int);
			continue;
		}

		fmt = getdollar(fmt, isrunes, &r, &dollar);
		nullf.flags = 0;
		nullf.width = nullf.prec = 0;
		for(;; fmt = nextrune(fmt, isrunes, &r)){
		 HaveNextRune:
			// at this point, r is always the next character
			switch(r){
			case '\0':
				cleanupargs(args, a);
				free(verbs);
				return nil;
			case '.':
				nullf.flags |= Fmt::FmtWidth|Fmt::FmtPrec;
				continue;
			case '0':
				if(!(nullf.flags & Fmt::FmtWidth)){
					nullf.flags |= Fmt::FmtZero;
					fmt = nextrune(fmt, isrunes, &r);
					continue;
				}
				/* fall through */
			case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				fmt = nextnumber(fmt, isrunes, &r, &i);
			numflag:
				if(nullf.flags & Fmt::FmtWidth){
					nullf.flags |= Fmt::FmtPrec;
					nullf.prec = i;
				}else{
					nullf.flags |= Fmt::FmtWidth;
					nullf.width = i;
				}
				goto HaveNextRune;
			case '*':
				fmt = getdollar(fmt, isrunes, &r, &star);
				if(star >= maxflag){
					maxflag = star + 1;
				}

				/* can safely make up any value. */
				i = 17;
				goto numflag;
			}
			nullf.r = r;
			Convfmt *convfmt = fmtfmt(formatter, r);
			if((*convfmt->fmt)(&nullf) < 0){
				cleanupargs(args, a);
				free(verbs);
				return nil;
			}
			if(!convfmt->isflag){
				break;
			}
			/* a flag; $ arg number is implicit, keep going. */
		}
	}
	free(verbs);

	/*
	 * Can't have more flags than allowed for above unless there is an
	 * error in the format string.
	 */
	if(maxflag > nargs){
		cleanupargs(args, maxverb);
		return nil;
	}

	/*
	 * Fill in the arg location of any flags at the end.
	 */
	for(; a < maxflag; a++){
		VA_COPY(args[a], *nullf.args);
		va_arg(*nullf.args, int);
	}

	ff->args = nil;
	r = doreorderfmt(formatter, ff, origfmt, isrunes, args, maxflag);
	ff->args = nullf.args;

	cleanupargs(args, maxflag);
	if(r < 0)
		return nil;
	if(isrunes)
		return emptyrunes;
	return "";
}

const void*
dispatch(Formatter* formatter, State *f, const void *fmt, int isrunes, va_list* args, int nargs)
{
        va_list arg, stararg;
	const void* origfmt;
	Rune r;
	int dollar, star;

	f->flags = 0;
	f->width = f->prec = 0;

	origfmt = fmt;
	fmt = getdollar(fmt, isrunes, &r, &dollar);

	/*
	 * If using %1$d -style formats, make a copy of the argument.
	 * Need to make a copy since the argument can be used multiple times
	 * in the format string.
	 */
	if(dollar){
		/* May be the first %1$d -style arg seen. */
		if(args == nil){
			return reorderfmt(formatter, f, origfmt, isrunes);
		}
		if(dollar >= nargs){
			return nil;
		}
		VA_COPY(arg, args[dollar]);
		f->args = &arg;
	}
	for(;; fmt = nextrune(fmt, isrunes, &r)){
                int i = 0;
	 HaveNextRune:
		/* at this point, r is always the next character */
		switch(r){
		case '\0':
			fmt = nil;
			goto breakout;
		case '.':
			f->flags |= Fmt::FmtWidth|Fmt::FmtPrec;
			continue;
		case '0':
			if(!(f->flags & Fmt::FmtWidth)){
				f->flags |= Fmt::FmtZero;
				continue;
			}
			/* fall through */
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			fmt = nextnumber(fmt, isrunes, &r, &i);
		numflag:
			if(f->flags & Fmt::FmtWidth){
				f->flags |= Fmt::FmtPrec;
				f->prec = i;
			}else{
				f->flags |= Fmt::FmtWidth;
				f->width = i;
			}
			goto HaveNextRune;
		case '*':
			fmt = getdollar(fmt, isrunes, &r, &star);
			if(args == nil){
				if(star != 0){
					return reorderfmt(formatter, f, origfmt, isrunes);
				}
				i = FMT_ARG(f, int);
			}else if(star <= 0){
				fmt = nil;		/* illegal */
				goto breakout;
			}else{
				/*
				 * reorderfmt can miss numbered '*' arguments
				 * for verbs that don't consume args, so we may
				 * have to advance past the end of the known
				 * argument positions.
				 */
				int a = star < nargs ? star : nargs - 1;
				VA_COPY(stararg, args[a]);
				for(; a <= star; a++)
					i = va_arg(stararg, int);
				va_end(stararg);
			}
			if(i < 0){
				/*
				 * negative precision =>
				 * ignore the precision.
				 */
				if(f->flags & Fmt::FmtPrec){
					f->flags &= ~Fmt::FmtPrec;
					f->prec = 0;
					goto HaveNextRune;
				}
				i = -i;
				f->flags |= Fmt::FmtLeft;
			}
			goto numflag;
		}

		f->r = r;
		Convfmt *convfmt = fmtfmt(formatter, r);

		/*
		 * If the formatter consumes an argument there must be one
		 * specified when using %1$d -style formats.
		 */
		if(args != nil && !dollar && convfmt->consumesarg){
			fmt = nil;
			break;
		}
		if((*convfmt->fmt)(f) < 0){
			/* Formatter returned an error */
			fmt = nil;
			break;
		}
		if(!convfmt->isflag){
			break;
		}
		/* else it's a flag; keep going */
	}
 breakout:
	if(args != nil && dollar){
		va_end(arg);
		f->args = nil;
	}
	return fmt;
}

}  // end namespace Fmt
