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

#include "fmt/fmt.h"

int nothing(Fmt::State *s){
	return 0;
}

int nada(Fmt::State *s){
	return 0;
}

int
main(int argc, char *argv[])
{
	Fmt::quoteinstall();
	Fmt::print("hello world\n");
	Fmt::print("x: %x\n", 0x87654321);
	Fmt::print("u: %u\n", 0x87654321);
	Fmt::print("d: %d\n", 0x87654321);
	Fmt::print("s: %s\n", "hi there");
	Fmt::print("q: %q\n", "hi i'm here");
	Fmt::print("c: %c\n", '!');
	Fmt::print("g: %g %g %g\n", 3.14159, 3.14159e10, 3.14159e-10);
	Fmt::print("e: %e %e %e\n", 3.14159, 3.14159e10, 3.14159e-10);
	Fmt::print("f: %f %f %f\n", 3.14159, 3.14159e10, 3.14159e-10);
	Fmt::print("smiley: %C\n", (Rune)0x263a);
	Fmt::print("%g %.18g\n", 2e25, 2e25);
	Fmt::print("%2.18g\n", 1.0);
	Fmt::print("%f\n", 3.1415927/4);
	Fmt::print("%d\n", 23);
	Fmt::print("%i\n", 23);

	/* test %4$d formats */
	Fmt::print("%3$d %4$06d %2$d %1$d\n", 444, 333, 111, 222);
	Fmt::print("%3$d %4$06d %2$d %1$d\n", 444, 333, 111, 222);
	Fmt::print("%3$d %4$*5$06d %2$d %1$d\n", 444, 333, 111, 222, 20);
	Fmt::print("%3$hd %4$*5$06d %2$d %1$d\n", 444, 333, (short)111, 222, 20);
	Fmt::print("%3$lld %4$*5$06d %2$d %1$d\n", 444, 333, 111LL, 222, 20);

	/* test %'d formats */
	Fmt::print("%'d %'d %'d\n", 1, 2222, 33333333);
	Fmt::print("%'019d\n", 0);
	Fmt::print("%08d %08d %08d\n", 1, 2222, 33333333);
	Fmt::print("%'08d %'08d %'08d\n", 1, 2222, 33333333);
	Fmt::print("%'x %'X %'b\n", 0x11111111, 0xabcd1234, 12345);
	Fmt::print("%'lld %'lld %'lld\n", 1LL, 222222222LL, 3333333333333LL);
	Fmt::print("%019lld %019lld %019lld\n", 1LL, 222222222LL, 3333333333333LL);
	Fmt::print("%'019lld %'019lld %'019lld\n", 1LL, 222222222LL, 3333333333333LL);
	Fmt::print("%'020lld %'020lld %'020lld\n", 1LL, 222222222LL, 3333333333333LL);
	Fmt::print("%'llx %'llX %'llb\n", 0x111111111111LL, 0xabcd12345678LL, 112342345LL);

	/* test warning about multiple installation */
	Fmt::Formatter fmt;
	/* collide with pre-defined */
	fmt.print("should see warning about %%d\n");
	fmt.installverb('d', false, nothing);
	/* collide with custom */
	fmt.print("should see no warning about %%N\n");
	fmt.installverb('N', false, nothing);
	fmt.installverb('N', false, nothing); /* not a problem; redefining to same value. */
	fmt.print("should see one warning about %%N\n");
	fmt.installverb('N', false, nada);
	fmt.print("should see warning about %%alpha\n");
	fmt.installverb(0x3B1, false, nothing);
	fmt.installverb(0x3B1, false, nada);
	return 0;
}
