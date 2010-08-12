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
  "The contents herein includes software initially developed by
  Lucent Technologies Inc. and others, and is subject to the terms
  of the Lucent Technologies Inc. Plan 9 Open Source License
  Agreement.  A copy of the Plan 9 Open Source License Agreement is
  available at: http:// plan9.bell-labs.com/plan9dist/download.html
  or by contacting Lucent Technologies at http: // www.lucent.com.
  All software distributed under such Agreement is distributed on
  an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
  implied.  See the Lucent Technologies Inc. Plan 9 Open Source
  License Agreement for the specific language governing all rights,
  obligations and limitations under such Agreement.  Portions of
  the software developed by Lucent Technologies Inc. and others are
  Copyright (c) 2002.  All rights reserved.
  Contributor(s): Google Inc."
*/

// Adjusted from original to play better with google and to
// interpret fractional seconds.

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#include "utilities/strtotm.h"
#include "fmt/runes.h"


char* safestrncpy(char* dst, const char* src, size_t n);


// tokenizing

static char qsep[] = " \t\r\n";

static char* qtoken(char *s, char *sep) {
  char* t = s;  /* s is output string, t is original input string */
  while(*s!='\0' && utfrune(sep, *s)==NULL)
    s++;
  if (*s != '\0') {
    *s = '\0';
    if (s != t)
      s++;
  }
  return s;
}

static int
tokenize(char *s, char **args, int maxargs) {
  int nargs;

  for (nargs=0; nargs<maxargs; nargs++) {
    while(*s!='\0' && utfrune(qsep, *s)!=NULL)
      s++;
    if (*s == '\0')
      break;
    args[nargs] = s;
    s = qtoken(s, qsep);
  }

  return nargs;
}

// start of date processing

const char *const wdayname[7] = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

const char *const monname[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static void time2tm(const char* s, struct tm* tm, int* microsec);
static int dateindex(const char* d, const char *const *tab, int n);

/*
 * parse dates of formats
 * 1) [Wkd[,]] DD Mon YYYY HH:MM:SS zone
 * 2) [Wkd] Mon ( D|DD) HH:MM:SS [zone] YYYY
 * plus anything similar.
 * tm->tm_wday is set from user and may be wrong.
 * return false for a failure (too incomplete to fill out "tm").
 * on success the "tm" value must still be checked by mktime etc.
 */
bool date2tm(const char* date, struct tm* tm, int* microsec,
             char (*explicit_tz)[kMaxTimeZoneStringLen+1]) {
  /*
   * default date is Thu Jan  1 00:00:00 GMT 1970
   */
  tm->tm_wday = 4;
  tm->tm_mday = 1;
  tm->tm_mon = 0;
  tm->tm_hour = 0;
  tm->tm_min = 0;
  tm->tm_sec = 0;
  tm->tm_year = 70;
  *microsec = 0;

  /*
   * we don't know if DST is in effect or not
   */
  tm->tm_isdst = -1;

  char dstr[kMaxTimeStringLen + 1];
  safestrncpy(dstr, date, sizeof(dstr));

  char* flds[7];
  int n = tokenize(dstr, flds, 7);
  if (n < 4 || n > 6)
    return false;

  /*
   * parse the weekday if present. we assume it's first.
   * if it's missing, create an empty field.
   */
  char* s = strchr(flds[0], ',');
  if (s != NULL)
    *s = '\0';
  tm->tm_wday = dateindex(flds[0], wdayname, 7);
  if (tm->tm_wday < 0) {
    /* no weekday present; align the values to start at flds[1] */
    for (int j = n; j >= 1; j--)
      flds[j] = flds[j - 1];
    flds[0] = NULL;
    n++;
  }

  if (n != 6 && n != 5)
    return false;
  
  /*
   * check for the two major formats:
   * Month first or day first
   */
  const char* tz = "";
  const char* year;
  tm->tm_mon = dateindex(flds[1], monname, 12);
  if (tm->tm_mon >= 0) {
    tm->tm_mday = strtoul(flds[2], NULL, 10);
    time2tm(flds[3], tm, microsec);
    /* is there a time zone present? */
    if (n == 5) {
      year = flds[4];
    } else {
      tz = flds[4];
      year = flds[5];
    }
  } else {
    tm->tm_mday = strtoul(flds[1], NULL, 10);
    tm->tm_mon = dateindex(flds[2], monname, 12);
    year = flds[3];
    time2tm(flds[4], tm, microsec);
    if (n == 6)
      tz = flds[5];
  }

  tm->tm_year = strtoul(year, NULL, 10);
  if (strlen(year) > 2)
    tm->tm_year -= 1900;
  else if (strlen(year) < 2)
    return false;

  safestrncpy(*explicit_tz, tz, sizeof(*explicit_tz));

  return true;
}

/*
 * zone  : [A-Za-z][A-Za-z][A-Za-z]  some time zone names
 *  | [A-IK-Z]      military time; rfc1123 says the rfc822 spec is wrong.
 *  | "UT" | "GMT"  universal time
 *  | [+-][0-9][0-9][0-9][0-9]
 * zones is the rfc-822 list of time zone names
 */
static const struct {
  const char* name;
  const char* std_name;
  const char* dst_name;
  const char* olson_name;
  int isdst;
  int  offset;
} zones[] = {
  { "A",        "A",    NULL,   NULL,        0,   -1 * 3600 },
  { "B",        "B",    NULL,   NULL,        0,   -2 * 3600 },
  { "C",        "C",    NULL,   NULL,        0,   -3 * 3600 },
  { "CDT",      "CST",  "CDT",  "CST6CDT",   1,   -5 * 3600 },
  { "CST",      "CST",  "CDT",  "CST6CDT",   0,   -6 * 3600 },
  { "CST6CDT",  "CST",  "CDT",  "CST6CDT",  -1,   -6 * 3600 },
  { "D",        "D",    NULL,   NULL,        0,   -4 * 3600 },
  { "E",        "E",    NULL,   NULL,        0,   -5 * 3600 },
  { "EDT",      "EST",  "EDT",  "EST5EDT",   1,   -4 * 3600 },
  { "EST",      "EST",  "EDT",  "EST5EDT",   0,   -5 * 3600 },
  { "EST5EDT",  "EST",  "EDT",  "EST5EDT",  -1,   -5 * 3600 },
  { "F",        "F",    NULL,   NULL,        0,   -6 * 3600 },
  { "G",        "G",    NULL,   NULL,        0,   -7 * 3600 },
  { "GMT",      "GMT",  NULL,   NULL,        0,    0        },
  { "H",        "H",    NULL,   NULL,        0,   -8 * 3600 },
  { "I",        "I",    NULL,   NULL,        0,   -9 * 3600 },
  { "K",        "K",    NULL,   NULL,        0,  -10 * 3600 },
  { "L",        "L",    NULL,   NULL,        0,  -11 * 3600 },
  { "M",        "M",    NULL,   NULL,        0,  -12 * 3600 },
  { "MDT",      "MST",  "MDT",  "MST7MDT",   1,   -6 * 3600 },
  { "MST",      "MST",  "MDT",  "MST7MDT",   0,   -7 * 3600 },
  { "MST7MDT",  "MST",  "MDT",  "MST7MDT",  -1,   -7 * 3600 },
  { "N",        "N",    NULL,   NULL,        0,   +1 * 3600 },
  { "O",        "O",    NULL,   NULL,        0,   +2 * 3600 },
  { "P",        "P",    NULL,   NULL,        0,   +3 * 3600 },
  { "PDT",      "PST",  "PDT",  "PST8PDT",   1,   -7 * 3600 },
  { "PST",      "PST",  "PDT",  "PST8PDT",   0,   -8 * 3600 },
  { "PST8PDT",  "PST",  "PDT",  "PST8PDT",  -1,   -8 * 3600 },
  { "Q",        "Q",    NULL,   NULL,        0,   +4 * 3600 },
  { "R",        "R",    NULL,   NULL,        0,   +5 * 3600 },
  { "S",        "S",    NULL,   NULL,        0,   +6 * 3600 },
  { "T",        "T",    NULL,   NULL,        0,   +7 * 3600 },
  { "U",        "U",    NULL,   NULL,        0,   +8 * 3600 },
  { "UT",       "GMT",  NULL,   NULL,        0,    0        },
  { "V",        "V",    NULL,   NULL,        0,   +9 * 3600 },
  { "W",        "W",    NULL,   NULL,        0,  +10 * 3600 },
  { "X",        "X",    NULL,   NULL,        0,  +11 * 3600 },
  { "Y",        "Y",    NULL,   NULL,        0,  +12 * 3600 },
  { "Z",        "GMT",  NULL,   NULL,        0,    0        },
  { NULL,       NULL,   NULL,   NULL,        0,    0        }
};

bool zone2tm(const char* tzid, int* offset, int* isdst,
       const char** std_name, const char** dst_name, const char** olson_name) {
  if (*tzid == '+' || *tzid == '-') {
    int i = strtol(tzid, NULL, 10);
    *offset = (i / 100) * 3600 + (i % 100) * 60;
    *isdst = 0;
    *std_name = NULL;
    *dst_name = NULL;
    *olson_name = NULL;
    return true;
  }

  /*
   * look it up in the standard rfc822 table
   */
  for (int i = 0; zones[i].name != NULL; i++) {
    if (strcasecmp(zones[i].name, tzid) == 0) {
      *offset = zones[i].offset;
      *isdst = zones[i].isdst;
      *std_name = zones[i].std_name;
      *dst_name = zones[i].dst_name;
      *olson_name = zones[i].olson_name;
      return true;
    }
  }

  return false;
}

/*
 * hh[:mm[:ss[.microsec]]]
 */
static void time2tm(const char* s, struct tm* tm, int* microsec) {
  char** ptr_s = const_cast<char**>(&s);  // for broken strtoul
  tm->tm_hour = strtoul(s, ptr_s, 10);
  if (*s++ != ':')
    return;
  tm->tm_min = strtoul(s, ptr_s, 10);
  if (*s++ != ':')
    return;
  // can't parse it out easily because .4 != .04; use floating point
  double sec = strtod(s, NULL) + 0.0000005;  /* convert with rounding */
  if (0.0 <= sec && sec < 60.0) {
    tm->tm_sec = static_cast<int>(sec);  /* truncation; seconds */
    sec = sec - tm->tm_sec;
    *microsec = static_cast<int>(sec * 1.e6);
    return;
  }
  /* something funny; use ints */
  tm->tm_sec = strtoul(s, NULL, 100);
}

static int dateindex(const char* d, const char *const *tab, int n) {
  for (int i = 0; i < n; i++)
    if (strcasecmp(d, tab[i]) == 0)
      return i;
  return -1;
}
