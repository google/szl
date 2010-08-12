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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "fmt/fmt.h"

bool failed = false;

/* Consume argument and ignore it */
int Zflag(Fmt::State* f)
{
	if(FMT_ARG(f, int))
		;
	return 1;	/* it's a flag */
}

/* Write 'foo' to the output without consuming any arg */
int zverb(Fmt::State* f)
{
	return fmtstrcpy(f, "foo");
}

/* Verify a return value from smprint is as expected, and free it */
void
verify(char* got, const char* expected)
{
	if(got == NULL){
		failed = true;
		fprintf(stderr, "error: NULL != (%s)\n", expected);
	}else if(strcmp(got, expected) != 0){
		failed = true;
		fprintf(stderr, "error: (%s) != (%s)\n", got, expected);
	}
	free(got);
}

char*
runestoutf(const Rune *s)
{
	char *t, *tt;

	if(s == NULL){
		return NULL;
	}
	t = (char*)malloc(runenlen(s, runestrlen(s)) + 1);
	for(tt = t; *s; s++)
		tt += runetochar(tt, s);
	*tt = 0;
	return t;
}

/* Format to chars and to runes and compare against the expected output. */
void
verifyfmt(Fmt::Formatter* F, const char* expected, const char* fmt, ...)
{
	Rune *rgot;
	char* got;
	va_list args;

	/* printing from a char format string to a char string */
	va_start(args, fmt);
	got = F->vsmprint(fmt, &args);
	va_end(args);
	verify(got, expected);

	/* printing from a char format string to a rune string */
	va_start(args, fmt);
	rgot = F->runevsmprint(fmt, &args);
	va_end(args);
	got = runestoutf(rgot);
	verify(got, expected);
	free(rgot);
}

/* Make sure the string is empty, since it should be returned from a failing smprint */
void
verifyfail(char *s)
{
	if(s != NULL){
		failed = true;
		fprintf(stderr, "error: didn't fail: %s\n", s);
	}
	free(s);
}

Rune lightsmiley = 0x263a;
Rune darksmiley = 0x263b;

/* Test printer that loads unusual decimal point and separator */
char*
mysmprint(const char *fmt, ...)
{
	Fmt::State f;
	va_list args;

	va_start(args, fmt);
	if(Fmt::fmtstrinit(&f) < 0)
		return 0;
	f.decimal = Fmt::smprint("%C", lightsmiley);
	f.thousands = Fmt::smprint("%C", darksmiley);
	f.grouping = "\1\2\3\4";
	f.args = &args;
	if(dofmt(NULL, &f, fmt) < 0)
		return 0;
	va_end(args);
	return Fmt::fmtstrflush(&f);
}

/* Test printer that prints to nothing */
int
nullprint(const char *fmt, ...)
{
	Fmt::State f;
	va_list args;
	int r;

	va_start(args, fmt);
	if(Fmt::fmtnullinit(&f) < 0)
		return -1;
	f.args = &args;
	r = dofmt(NULL, &f, fmt);
	va_end(args);
	return r;
}

void
verifynullprint() {
	int n;

	n = nullprint("hello world");
	if(n != strlen("hello world")){
		failed = true;
		fprintf(stderr, "nullprint returned %d for 'hello world'\n", n);
	}

	/* output long enough to cause a fmtnullflush */
	n = nullprint("01234567890123456789012345678901234567890123456789");
	if (n != 50){
		failed = true;
		fprintf(stderr, "nullprint returned %d for 0-9x5\n", n);
	}
	n = nullprint("%s",
			"01234567890123456789012345678901234567890123456789");
	if (n != 50){
		failed = true;
		fprintf(stderr, "nullprint returned %d for '%%s'\n", n);
	}
	n = nullprint("x%s",
			"01234567890123456789012345678901234567890123456789");
	if (n != 51){
		failed = true;
		fprintf(stderr, "nullprint returned %d for 'x%%s'\n", n);
	}
	n = nullprint("%sx",
			"01234567890123456789012345678901234567890123456789");
	if (n != 51){
		failed = true;
		fprintf(stderr, "nullprint returned %d for '%%sx'\n", n);
	}
	n = nullprint("x%sx",
			"01234567890123456789012345678901234567890123456789");
	if (n != 52){
		failed = true;
		fprintf(stderr, "nullprint returned %d for 'x%%sx'\n", n);
	}
	n = nullprint("x%sx%sx",
			"01234567890123456789012345678901234567890123456789",
			"01234567890123456789012345678901234567890123456789");
	if (n != 103){
		failed = true;
		fprintf(stderr, "nullprint returned %d for 'x%%sx%%sx'\n", n);
	}
}

int main(int argc, char* argv[])
{
	Fmt::Formatter F;

	F.quoteinstall();
	F.installflag('Z', true, Zflag);
	F.installflag(L'\x263a', true, Zflag);
	F.installverb('z', false, zverb);

	verifyfmt(&F, "hello world", "hello world");
	verifyfmt(&F, "x: 87654321", "x: %x", 0x87654321);
	verifyfmt(&F, "d: -2023406815", "d: %d", 0x87654321);
	verifyfmt(&F, "s: hi there", "s: %s", "hi there");
	verifyfmt(&F, "q: 'hi i''m here'", "q: %q", "hi i'm here");
	verifyfmt(&F, "c: !", "c: %c", '!');
	verifyfmt(&F, "g: 3.14159 3.14159e+10 3.14159e-10", "g: %g %g %g", 3.14159, 3.14159e10, 3.14159e-10);
	verifyfmt(&F, "e: 3.141590e+00 3.141590e+10 3.141590e-10", "e: %e %e %e", 3.14159, 3.14159e10, 3.14159e-10);
	verifyfmt(&F, "f: 3.141590 31415900000.000000 0.000000", "f: %f %f %f", 3.14159, 3.14159e10, 3.14159e-10);
	verifyfmt(&F, "smiley: \xe2\x98\xba", "smiley: %C", (Rune)0x263a);
	verifyfmt(&F, "2e+25 2e+25", "%g %.18g", 2e25, 2e25);
	verifyfmt(&F, " 1", "%2.18g", 1.0);
	verifyfmt(&F, "0.785398", "%f", 3.1415927/4);
	verifyfmt(&F, "23", "%d", 23);
	verifyfmt(&F, "23", "%i", 23);
	verifyfmt(&F, "23", "%Zi", 1234, 23);

	/* test $ reorderings */
	verifyfmt(&F, " 1", "%1$*2$d", 1, 2);
	verifyfmt(&F, "2", "%2$*1$d", 1, 2);
	verifyfmt(&F, " 1", "%1$h*2$d", 1, 2);
	verifyfmt(&F, "2", "%2$h*1$d", 1, 2);
	verifyfmt(&F, " 001", "%1$*2$.*3$d", 1, 4, 3);
	verifyfmt(&F, "111 000222 333 444", "%3$d %4$06d %2$d %1$d", 444, 333, 111, 222);
	verifyfmt(&F, "111 000222 333 444", "%3$Zd %4$06d %2$d %1$d", 444, 333, 555, 111, 222);
	verifyfmt(&F, "111               000222 333 444", "%3$d %4$*5$06d %2$d %1$d", 444, 333, 111, 222, 20);
	verifyfmt(&F, "111               000222 333 444", "%3$hd %4$*5$06d %2$d %1$d", 444, 333, (short)111, 222, 20);
	verifyfmt(&F, "111 000222 333 444", "%3$\xe2\x98\xba""d %4$06d %2$d %1$d", 444, 333, 555, 111, 222);

	/* test mixed $ reorderings with verbs that require no arg */
	verifyfmt(&F, "17 13 foo", "%2$d %1$d %z", 13, 17);
	verifyfmt(&F, "foo 17 13", "%z %2$d %1$d", 13, 17);
	verifyfmt(&F, "f 17 13", "%.*3$z %2$d %1$d", 13, 17, 1);
	verifyfmt(&F, "fo", "%.*1$z", 2);

	/* reordering with strings long enough to overflow the fmtnull buffer causing it to flush */
	verifyfmt(&F, "so its flush routine gets called.  That routine used to return 0 indicating failure. "
		"a really really long string so long it overflows the fmtnullinit buffer",
		"%2$s %1$s",
		"a really really long string so long it overflows the fmtnullinit buffer",
		"so its flush routine gets called.  That routine used to return 0 indicating failure.");

	/* test $ reorderings with trailing text */
	verifyfmt(&F, "(31,415,926, 27,182,818)", "(%1$'.1d, %2$'.1d)", 31415926, 27182818);
	verifyfmt(&F, "10 hello world", "%1$d hello world", 10);

	/* test %'d formats */
	verifyfmt(&F, "1 2,222 33,333,333", "%'d %'d %'d", 1, 2222, 33333333);
	verifyfmt(&F, "000,000,000,000,000", "%'019d", 0);
	verifyfmt(&F, "0,000,001 0,002,222 33,333,333", "%'08d %'08d %'08d", 1, 2222, 33333333);
	verifyfmt(&F, "1111:1111 ABCD:1234 11:0000:0011:1001", "%'x %'X %'b", 0x11111111, 0xabcd1234, 12345);
	verifyfmt(&F, "1 222,222,222 3,333,333,333,333", "%'lld %'lld %'lld", 1LL, 222222222LL, 3333333333333LL);
	verifyfmt(&F, "000,000,000,000,001 000,000,222,222,222 003,333,333,333,333", "%'019lld %'019lld %'019lld", 1LL, 222222222LL, 3333333333333LL);
	verifyfmt(&F, "1111:1111:1111 ABCD:1234:5678 110:1011:0010:0011:0101:0100:1001", "%'llx %'llX %'llb", 0x111111111111LL, 0xabcd12345678LL, 112342345LL);

	/* test %'d with custom (utf-8!) separators */
	/* x and b still use : */
	verify(mysmprint("%'d %'d %'d", 1, 2222, 33333333), "1 2\xe2\x98\xbb""22\xe2\x98\xbb""2 33\xe2\x98\xbb""333\xe2\x98\xbb""33\xe2\x98\xbb""3");
	verify(mysmprint("%'x %'X %'b", 0x11111111, 0xabcd1234, 12345), "1111:1111 ABCD:1234 11:0000:0011:1001");
	verify(mysmprint("%'lld %'lld %'lld", 1LL, 222222222LL, 3333333333333LL), "1 222\xe2\x98\xbb""222\xe2\x98\xbb""22\xe2\x98\xbb""2 333\xe2\x98\xbb""3333\xe2\x98\xbb""333\xe2\x98\xbb""33\xe2\x98\xbb""3");
	verify(mysmprint("%'llx %'llX %'llb", 0x111111111111LL, 0xabcd12345678LL, 112342345LL), "1111:1111:1111 ABCD:1234:5678 110:1011:0010:0011:0101:0100:1001");

	/* test embedded \0 */
	verifyfail(F.smprint("abc %h\0d"));
	verifyfail(F.smprint("%d %h\0d", 1, 2));
	verifyfail(F.smprint("abc %1$h\0d"));
	verifyfail(F.smprint("%2$d %1$h\0d", 1, 2));

	verifynullprint();

        puts(failed ? "FAIL" : "PASS");
        return (failed != 0);
}
