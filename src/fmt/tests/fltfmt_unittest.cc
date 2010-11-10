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
#include <math.h>
#include <ctype.h>

#include "fmt/fmt.h"
#include "fmt/nan.h"

/*
 * try all combination of flags and float conversions
 * with some different widths & precisions
 */

#define Njust 2
#define Nplus 3
#define Nalt 2
#define Nzero 2
#define Nspec 5
#define Nwidth 5
#define Nprec 5

int exitcode;

static double fmtvals[] = {
	3.1415925535897932e15,
	3.1415925535897932e14,
	3.1415925535897932e13,
	3.1415925535897932e12,
	3.1415925535897932e11,
	3.1415925535897932e10,
	3.1415925535897932e9,
	3.1415925535897932e8,
	3.1415925535897932e7,
	3.1415925535897932e6,
	3.1415925535897932e5,
	3.1415925535897932e4,
	3.1415925535897932e3,
	3.1415925535897932e2,
	3.1415925535897932e1,
	3.1415925535897932e0,
	3.1415925535897932e-1,
	3.1415925535897932e-2,
	3.1415925535897932e-3,
	3.1415925535897932e-4,
	3.1415925535897932e-5,
	3.1415925535897932e-6,
	3.1415925535897932e-7,
	3.1415925535897932e-8,
	3.1415925535897932e-9,
	3.1415925535897932e-10,
	3.1415925535897932e-11,
	3.1415925535897932e-12,
	3.1415925535897932e-13,
	3.1415925535897932e-14,
	3.1415925535897932e-15,

	1e308,
	5e-324,
};

/*
 * are the numbers close?
 * used to compare long numbers where the last few digits are garbage
 * due to precision problems
 */
static int numclose(char* num1, char* num2) {
	int ndig;
	enum { MAXDIG = 14 };

	ndig = 0;
	while (*num1) {
		if (*num1 >= '0' && *num1 <= '9') {
			ndig++;
			if (ndig > MAXDIG) {
				if (!(*num2 >= '0' && *num2 <= '9')) {
					return 0;
				}
			} else if (*num1 != *num2) {
				return 0;
			}
		} else if (*num1 != *num2) {
			return 0;
		} else if (*num1 == 'e' || *num1 == 'E') {
			ndig = 0;
		}
		num1++;
		num2++;
	}
	if (*num1 || !num2)
		return 0;
	return 1;
}

static void doit(int just, int plus, int alt, int zero, int width, int prec,
		  int spec) {
	char format[256];
	char *p;
	const char *s;
	int i;

	p = format;
	*p++ = '%';
	if (just > 0)
		*p++ = "-"[just - 1];
	if (plus > 0)
		*p++ = "+ "[plus - 1];
	if (alt > 0)
		*p++ = "#"[alt - 1];
	if (zero > 0)
		*p++ = "0"[zero - 1];

	s = "";
	switch (width) {
	case 1: s = "1"; break;
	case 2: s = "5"; break;
	case 3: s = "10"; break;
	case 4: s = "15"; break;
	}
	strcpy(p, s);

	s = "";
	switch (prec) {
	case 1: s = ".0"; break;
	case 2: s = ".2"; break;
	case 3: s = ".5"; break;
	case 4: s = ".15"; break;
	}
	strcat(p, s);

	p = strchr(p, '\0');
	*p++ = "efgEG"[spec];
	*p = '\0';

	for (i = 0; i < sizeof(fmtvals) / sizeof(fmtvals[0]); i++) {
		char ref[256], buf[256];
		snprintf(ref, sizeof ref, format, fmtvals[i]);
		Fmt::snprint(buf, sizeof(buf), format, fmtvals[i]);
		if (strcmp(ref, buf) != 0
		&& !numclose(ref, buf)) {
			fprintf(stderr, "%s: ref='%s' fmt='%s'\n", format, ref, buf);
			exitcode = 1;
		}

		// Check again with output to rune string
		Rune rbuf[256];
		Fmt::runesnprint(rbuf, 256, format, fmtvals[i]);
		Fmt::snprint(buf, sizeof(buf), "%S", rbuf);
		if (strcmp(ref, buf) != 0
		&& !numclose(ref, buf)) {
			fprintf(stderr, "%s: rune ref='%s' fmt='%s'\n", format, ref, buf);
			exitcode = 1;
		}
	}
}

void trynan() {
	static double big = 1e300;
	static double inf = big*big;
	static double nan = inf/inf;  // ieee fp

	if (!Fmt::__isNaN(nan)) {
		fprintf(stderr, "error: !__isNaN()\n");
		exitcode = 1;
	}
	if (!Fmt::__isNaN(sqrt(-1.0))) {
		fprintf(stderr, "error: !__isNaN()\n");
		exitcode = 1;
	}
	if (!Fmt::__isNaN(asin(4.0))) {
		fprintf(stderr, "error: !__isNaN()\n");
		exitcode = 1;
	}
	char buf[256];
	Fmt::snprint(buf, sizeof(buf), "%g", nan);
	if (strcmp(buf, "nan")) {
		fprintf(stderr, "error: 'nan' != '%s'\n", buf);
		exitcode = 1;
	}
	Fmt::snprint(buf, sizeof(buf), "%g", sqrt(-1));
	if (strcmp(buf, "nan")) {
		fprintf(stderr, "error: 'nan' != '%s'\n", buf);
		exitcode = 1;
	}
	Fmt::snprint(buf, sizeof(buf), "%G", nan);
	if (strcmp(buf, "NAN")) {
		fprintf(stderr, "error: 'NAN' != '%s'\n", buf);
		exitcode = 1;
	}
}

void tryinf() {
	static double big = 1e300;
	static double inf = big*big;
	static double ninf = -inf;

	if (!Fmt::__isInf(inf, 1)) {
		fprintf(stderr, "error: !__isInf()\n");
		exitcode = 1;
	}
	if (!Fmt::__isInf(ninf, -1)) {
		fprintf(stderr, "error: !__isInf()\n");
		exitcode = 1;
	}
	if (!Fmt::__isInf(ninf, 0)) {
		fprintf(stderr, "error: !__isInf()\n");
		exitcode = 1;
	}
	char buf[256], ref[256], *tmp;
	const char* refvals[] = { "inf", "+inf", " inf",
				"-inf", "-inf", "-inf" };
	const double dblvals[] = { inf, inf, inf, ninf, ninf, ninf };
	const char* fmts[] = { "%g", "%+g", "% g", "%G", "%+G", "% G"}, *fmt;
	for (int i = 0; i < sizeof(refvals) / sizeof(refvals[0]); i++) {
		strcpy(ref, refvals[i]);
		fmt = fmts[i%3];
		Fmt::snprint(buf, sizeof(buf), fmt, dblvals[i]);
		if (strcmp(buf, ref)) {
			fprintf(stderr, "error: '%s' != '%s'\n", ref, buf);
			exitcode = 1;
		}
		tmp = ref;
		do { *tmp = toupper(*tmp); } while(*++tmp);
		fmt = fmts[3+(i%3)];
		Fmt::snprint(buf, sizeof(buf), fmt, dblvals[i]);
		if (strcmp(buf, ref)) {
			fprintf(stderr, "error: '%s' != '%s'\n", ref, buf);
			exitcode = 1;
		}
	}
}

int main(int argc, char* argv[]) {
	int just, plus, alt, zero, width, prec, spec;

	for (just = 0; just < Njust; just++)
	for (plus = 0; plus < Nplus; plus++)
	for (alt = 0; alt < Nalt; alt++)
	for (zero = 0; zero < Nzero; zero++)
	for (width = 0; width < Nwidth; width++)
	for (prec = 0; prec < Nprec; prec++)
	for (spec = 0; spec < Nspec; spec++)
		doit(just, plus, alt, zero, width, prec, spec);

	trynan();
	tryinf();
	return exitcode;
}
