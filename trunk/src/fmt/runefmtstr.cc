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

static int
runeFmtStrFlush(Fmt::State *f)
{
	Rune *s;
	int n;

	n = f->fintarg;
	n *= 2;
	f->fintarg = n;
	s = (Rune*)f->start;
	f->start = realloc(s, sizeof(Rune)*n);
	if(f->start == nil){
		f->start = s;
		return 0;
	}
	f->to = (Rune*)f->start + ((Rune*)f->to - s);
	f->stop = (Rune*)f->start + n - 1;
	return 1;
}

int
Formatter::runefmtstrinit(State *f)
{
	int n;

	f->runes = 1;
	n = 256;
	f->start = malloc(sizeof(Rune)*n);
	if(f->start == nil)
		return -1;
	f->to = f->start;
	f->stop = (Rune*)f->start + n - 1;
	f->flush = runeFmtStrFlush;
	f->farg = 0;
	f->fintarg = n;
	f->nfmt = 0;
	f->flags = 0;
	f->formatter = this;
	fmtlocaleinit(f, nil, nil, nil);
	return 0;
}

int
runefmtstrinit(State *f)
{
	return _stdfmt.runefmtstrinit(f);
}

Rune*
Formatter::runefmtstrflush(State *f)
{
	*(Rune*)f->to = '\0';
	f->to = f->start;
	return (Rune*)f->start;
}

Rune*
runefmtstrflush(State *f)
{
	return Formatter::runefmtstrflush(f);
}

}  /* end namespace Fmt */
