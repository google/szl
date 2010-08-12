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

/*
 * Modified by Mike Burrows and Rob Pike
 * Copyright 2004 Google, Inc.
 */
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "fmt/fmt.h"
#include "fmt/fmtdef.h"

namespace Fmt {

/*
 * Fill in the internationalization stuff in the State structure.
 * For nil arguments, provide the sensible defaults:
 *	decimal is a period
 *	thousands separator is a comma
 *	thousands are marked every three digits
 */
void
Formatter::fmtlocaleinit(State *f, const char *decimal, const char *thousands, const char *grouping)
{
	if(decimal == nil || decimal[0] == '\0')
		decimal = ".";
	if(thousands == nil)
		thousands = ",";
	if(grouping == nil)
		grouping = "\3";
	f->decimal = decimal;
	f->thousands = thousands;
	f->grouping = grouping;
}

void
fmtlocaleinit(State *f, const char *decimal, const char *thousands, const char *grouping)
{
	Formatter::fmtlocaleinit(f, decimal, thousands, grouping);
}

/*
 * We are about to emit a digit in e.g. %'d.  If that digit would
 * overflow a thousands (e.g.) grouping, tell the caller to emit
 * the thousands separator.  Always advance the digit counter
 * and pointer into the grouping descriptor.
 */
int
__needsep(int *ndig, const char **grouping)
{
	int group;
	
	(*ndig)++;
	group = *(unsigned char*)*grouping;
	/* CHAR_MAX means no further grouping. \0 means we got the empty string */
	if(group == 0xFF || group == 0x7f || group == 0x00)
		return 0;
	if(*ndig > group){
		/* if we're at end of string, continue with this grouping; else advance */
		if((*grouping)[1] != '\0')
			(*grouping)++;
		*ndig = 1;
		return 1;
	}
	return 0;
}

}  /* end namespace Fmt */
