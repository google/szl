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
//

#ifndef _STRTOTM_H_
#define _STRTOTM_H_


// Longest tz in early 2006 was "America/Argentina/ComodRivadavia" => 32 chars.
// Allow for twice that.  TODO: use the actual limit if/when established.
const int kMaxTimeZoneStringLen = 64;

// "Tue Jul  1 22:27:26 PDT 2003" is 29 including \0  =>  26 + len(tz name)
const int kMaxTimeStringLen = 26 + kMaxTimeZoneStringLen;


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
             char (*explicit_tz)[kMaxTimeZoneStringLen+1]);


bool zone2tm(const char* tzid, int* offset, int* isdst,
       const char** std_name, const char** dst_name, const char** olson_name);

#endif  // _STRTOTM_H_
