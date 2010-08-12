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

#include "utilities/strtotm.h"

namespace sawzall {


// Convert between uint64 and components optionally specifying time zone.
// When "from_string" is specified and the time zone name indicates whether
// DST is in effect (e.g. EST or EDT, but not EST5EDT), that indication is
// honored even if it is wrong.

// "microsec" and "tz_name" can be NULL if those values are not needed
bool SzlTimeToLocalTime(uint64 t, const char* tz,
                        struct tm* tm, int* microsec,
                        char (*tz_name)[kMaxTimeZoneStringLen + 1]);
bool LocalTimeToSzlTime(const struct tm& tm, int microsec, const char* tz,
                        bool from_string, uint64* t);


// Convert uint64 to C string.

bool SzlTime2Str(uint64 szlt, const char* tz,
                 char (*buf)[kMaxTimeStringLen + 1]);


// Convert C string into sawzall time value, with microseconds
bool date2uint64(const char* date, const char* tz, uint64* timep);


}  // end namespace sawzall
