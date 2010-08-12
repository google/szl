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
#include "fmt/fmt.h"
#include "fmt/fmtdef.h"

namespace Fmt {

/*
 * public routine for final flush of a formatting buffer
 * to a file descriptor; returns total char count.
 */
int
Formatter::fmtfdflush(State *f)
{
	if(__fmtFdFlush(f) <= 0)
		return -1;
	return f->nfmt;
}

int
fmtfdflush(State *f)
{
	return Formatter::fmtfdflush(f);
}

/*
 * initialize an output buffer for buffered printing
 */
int
Formatter::fmtfdinit(State *f, int fd, char *buf, int size)
{
	f->runes = 0;
	f->start = buf;
	f->to = buf;
	f->stop = buf + size;
	f->flush = __fmtFdFlush;
	f->farg = 0;
	f->fintarg = fd;
	f->flags = 0;
	f->nfmt = 0;
	f->formatter = this;
	fmtlocaleinit(f, nil, nil, nil);
	return 0;
}

int
fmtfdinit(State *f, int fd, char *buf, int size)
{
	return _stdfmt.fmtfdinit(f, fd, buf, size);
}

}  /* end namespace Fmt */
