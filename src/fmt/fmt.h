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

#ifndef _FMTH_
#define _FMTH_ 1

#include <stdarg.h>

#ifndef _UTFH_
#include "fmt/runes.h"
#endif

/*
 * The Fmt class is mostly a name space to capture the C-like interface
 * to the usual print, sprint, etc. routines.  Most programs can use it
 * without worrying about the Formatter class.
 *
 * The Formatter class allows the installation of custom print verbs.
 * The namesake routines in Fmt are implemented by calls to the
 * methods in Formatter, using a statically defined, read-only Formatter.
 */


/* This namespace provides lexical compatibility with the old class Fmt */
namespace Fmt {

class Formatter;

struct State{
	unsigned char	runes;	/* output buffer is runes or chars? */
	void	*start;			/* of buffer */
	void	*to;			/* current place in the buffer */
	void	*stop;			/* end of the buffer; overwritten if flush fails */
	int	(*flush)(State *);	/* called when to == stop */
	void	*farg;			/* to make flush a closure */
	int	fintarg;		/* holds ints used by flush routines */
	int	nfmt;			/* num chars formatted so far */
	va_list	*args;			/* args passed to verb */
	int	r;			/* % format Rune */
	int	width;
	int	prec;
	unsigned long	flags;
	Formatter	*formatter;
	const char *decimal;		/* representation of decimal point; cannot be "" */
	/* for %'d */
	const char *thousands;		/* separator for thousands */

	/* Each char is an integer indicating #digits before next separator. Values:
	 *	\xFF: no more grouping (or \x7F; defined to be CHAR_MAX in POSIX)
	 *	\x00: repeat previous indefinitely
	 *	\x**: count that many
	 */
	const char *grouping;		/* descriptor of separator placement */
};

enum{
	FmtWidth	= 1,
	FmtLeft		= FmtWidth << 1,
	FmtPrec		= FmtLeft << 1,
	FmtSharp	= FmtPrec << 1,
	FmtSpace	= FmtSharp << 1,
	FmtSign		= FmtSpace << 1,
	FmtApost	= FmtSign << 1,
	FmtZero		= FmtApost << 1,
	FmtUnsigned	= FmtZero << 1,
	FmtShort	= FmtUnsigned << 1,
	FmtLong		= FmtShort << 1,
	FmtVLong	= FmtLong << 1,
	FmtByte		= FmtVLong << 1,
	FmtLDouble	= FmtByte << 1,

	FmtFlag		= FmtLDouble << 1
};

enum
{
	Maxfmt = 64
};

typedef int (*Fmts)(State*);

struct Convfmt
{
	int		c;
	bool		isflag;		/* is the installed fmt a flag or a verb? */
	bool		consumesarg;	/* does it consume a vararg? */
	volatile Fmts	fmt;		/* for spin lock in fmtfmt; avoids race due to write order */
};

class Formatter {
public:
	Formatter();
	~Formatter();

	int	print(const char*, ...);
	char*	seprint(char*, char*, const char*, ...);
	char*	vseprint(char*, char*, const char*, va_list*);
	int	snprint(char*, int, const char*, ...);
	int	vsnprint(char*, int, const char*, va_list*);
	char*	smprint(const char*, ...);
	char*	vsmprint(const char*, va_list*);
	int	sprint(char*, const char*, ...);
	int	fprint(int, const char*, ...);
	int	vfprint(int, const char*, va_list*);

	int	runesprint(Rune*, const char*, ...);
	int	runesnprint(Rune*, int, const char*, ...);
	int	runevsnprint(Rune*, int, const char*, va_list*);
	Rune*	runeseprint(Rune*, Rune*, const char*, ...);
	Rune*	runevseprint(Rune*, Rune*, const char*, va_list*);
	Rune*	runesmprint(const char*, ...);
	Rune*	runevsmprint(const char*, va_list*);

	static void	fmtlocaleinit(State*, const char*, const char*, const char*);

	int	fmtnullinit(State*);
	int	fmtfdinit(State*, int, char*, int);
	int	fmtstrinit(State*);
	int	runefmtstrinit(State*);

	static int	fmtfdflush(State*);
	static char*	fmtstrflush(State*);
	static Rune*	runefmtstrflush(State*);

	void	quoteinstall(void);

	int	installverb(int c, bool consumesarg, int (*f)(State*));
	int	installflag(int c, bool consumesarg, int (*f)(State*));

	int	fmtprint(State*, const char*, ...);
	int	fmtvprint(State*, const char*, va_list*);

	int	nfmt;
	Convfmt	fmt[Maxfmt];
};

extern Formatter _stdfmt;

int	print(const char*, ...);
char*	seprint(char*, char*, const char*, ...);
char*	vseprint(char*, char*, const char*, va_list*);
int	snprint(char*, int, const char*, ...);
int	vsnprint(char*, int, const char*, va_list*);
char*	smprint(const char*, ...);
char*	vsmprint(const char*, va_list*);
int	sprint(char*, const char*, ...);
int	fprint(int, const char*, ...);
int	vfprint(int, const char*, va_list*);

int	runesprint(Rune*, const char*, ...);
int	runesnprint(Rune*, int, const char*, ...);
int	runevsnprint(Rune*, int, const char*, va_list*);
Rune*	runeseprint(Rune*, Rune*, const char*, ...);
Rune*	runevseprint(Rune*, Rune*, const char*, va_list*);
Rune*	runesmprint(const char*, ...);
Rune*	runevsmprint(const char*, va_list*);

int	fmtnullinit(State*);
int	fmtfdinit(State*, int, char*, int);
int	fmtfdflush(State*);
int	fmtstrinit(State*);
void	fmtlocaleinit(State*, const char*, const char*, const char*);
char*	fmtstrflush(State*);
Rune*	runefmtstrflush(State*);
int	runefmtstrinit(State*);

int	quotestrfmt(State *f);
int	quoterunestrfmt(State *f);
void	quoteinstall(void);
extern int	(*fmtdoquote)(int);


/*
 * dofmt -- format to a buffer
 * the number of characters formatted is returned,
 * or -1 if there was an error.
 * if the buffer is ever filled, flush is called.
 * it should reset the buffer and return whether formatting should continue.
 */
int	dofmt(Formatter*, State*, const char*);
int	dorfmt(Formatter*, State*, const Rune*);
int	fmtprint(State*, const char*, ...);
int	fmtvprint(State*, const char*, va_list*);
int	fmtrune(State*, int);
int	fmtstrcpy(State*, const char*);
int	fmtrunestrcpy(State *f, const Rune *s);

double	fmtstrtod(const char *, char **);
double	fmtcharstod(int(*)(void*), void*);

};

#define	FMT_ARG(f, type) va_arg(*(f)->args, type)

#endif
