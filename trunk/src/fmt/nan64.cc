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
 * 64-bit IEEE not-a-number routines.
 * This is big/little-endian portable assuming that 
 * the 64-bit doubles and 64-bit integers have the
 * same byte ordering.
 */

#include "nan.h"

namespace Fmt {

typedef unsigned long long uvlong;
typedef unsigned long ulong;

static uvlong uvnan    = 0x7FF0000000000001LL;
static uvlong uvinf    = 0x7FF0000000000000LL;
static uvlong uvneginf = 0xFFF0000000000000LL;

double
__NaN(void)
{
	return *(double*)&uvnan;
}

int
__isNaN(double d)
{
	uvlong x = *(uvlong*)&d;
	// IEEE 754: NaN if exponent is 2047 and mantissa not 0.
	return ((x & uvinf) == uvinf) && (x & ~uvneginf);
}

double
__Inf(int sign)
{
	if(sign < 0)
		return *(double*)&uvinf;
	else
		return *(double*)&uvneginf;
}

int
__isInf(double d, int sign)
{
	uvlong x;

	x = *(uvlong*)&d;
	if(sign == 0)
		return x==uvinf || x==uvneginf;
	else if(sign > 0)
		return x==uvinf;
	else
		return x==uvneginf;
}

}  /* end namespace Fmt */
