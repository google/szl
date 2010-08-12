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
#include <time.h>

#include "unicode/utypes.h"
#include "unicode/timezone.h"
#include "unicode/udat.h"
#include "unicode/ustring.h"

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/strutils.h"
#include "utilities/szlmutex.h"
#include "utilities/timeutils.h"


using icu::TimeZone;
using icu::UnicodeString;


// Convert a "struct tm" interpreted as *GMT* into a time_t.

static long mkgmtime(const struct tm *tm) {
  // Month-to-day offset for non-leap-years.
  static const int month_day[12] =
    {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

  // Most of the calculation is easy; leap years are the main difficulty.
  int month = tm->tm_mon % 12;
  int year = tm->tm_year + tm->tm_mon / 12;
  if (month < 0) {   // Negative values % 12 are still negative.
    month += 12;
    --year;
  }

  // This is the number of Februaries since 1900.
  const int year_for_leap = (month > 1) ? year + 1 : year;

  long rt = tm->tm_sec                             // Seconds
       + 60 * (tm->tm_min                          // Minute = 60 seconds
       + 60 * (tm->tm_hour                         // Hour = 60 minutes
       + 24 * (month_day[month] + tm->tm_mday - 1  // Day = 24 hours
       + 365 * (year - 70)                         // Year = 365 days
       + (year_for_leap - 69) / 4                  // Every 4 years is leap...
       - (year_for_leap - 1) / 100                 // Except centuries...
       + (year_for_leap + 299) / 400)));           // Except 400s.
  return rt < 0 ? -1 : rt;
}


namespace sawzall {

const int kMicrosecPerSec = 1000000;
const int kMicrosecPerMillisec = 1000;
const int kMillisecPerSec = 1000;



// ===========================================================================


// Get ICU TimeZone objects through a class to simplify managing a cache
// and dealing with deletion of non-cached instances.

class CachedTimeZone {
 public:
  explicit CachedTimeZone(const char* id);
  ~CachedTimeZone();

  TimeZone* timezone() { return timezone_; }

  static const char kDefaultOlsonId[];  // name from time zone database
  static const char kDefaultStdId[];    // short name for default standard time
  static const char kDefaultDstId[];    // short name for default DST
  static const int  kDefaultTimeZoneRawOffset;

 private:
  TimeZone* timezone_;
  static TimeZone* default_timezone_;
  static SzlMutex default_timezone_lock_;
};


CachedTimeZone::CachedTimeZone(const char* id) {
  if (strcasecmp(id, kDefaultOlsonId) == 0) {
    SzlMutexLock lock(&default_timezone_lock_);
    if (default_timezone_ == NULL) {
      default_timezone_ = TimeZone::createTimeZone(kDefaultOlsonId);
      assert(default_timezone_ != NULL);
      assert(default_timezone_->getRawOffset() == kDefaultTimeZoneRawOffset);
    }
    timezone_ = default_timezone_;
  } else {
    // TODO: check whether we will ever need non-ASCII identifiers
    // (and if we do, convert our UTF-8 to a UChar string first.)
    timezone_ = TimeZone::createTimeZone(id);
    CHECK(timezone_ != NULL);
    // ICU does not directly say if the id was found, but we can deduce it.
    if (timezone_->getRawOffset() == 0 && strcasecmp(id, "GMT") != 0) {
      // getID() returns "GMT" iff "id" is "GMT" or is not recognized.
      UnicodeString id_string;
      timezone_->getID(id_string);
      if (id_string.length() == 3) {
        const UChar* id = id_string.getBuffer();
        if (id[0] == 'G' && id[1] == 'M' && id[2] == 'T') {
          // the id is "GMT" so the identifier was not found
          delete timezone_;
          timezone_ = NULL;
        }
      }
    }
  }
}


CachedTimeZone::~CachedTimeZone() {
  if (timezone_ != default_timezone_)
    delete timezone_;
}


TimeZone* CachedTimeZone::default_timezone_ = NULL;
SzlMutex CachedTimeZone::default_timezone_lock_;
const char CachedTimeZone::kDefaultOlsonId[] = "PST8PDT";
const char CachedTimeZone::kDefaultStdId[] = "PST";
const char CachedTimeZone::kDefaultDstId[] = "PDT";
const int  CachedTimeZone::kDefaultTimeZoneRawOffset = -8*60*60*kMillisecPerSec;


// TODO: Consider moving SzlTimeToLocalTime and LocalTimeToSzlTime to
// base/time_support.cc or wherever we end up putting the time zone support.

bool SzlTimeToLocalTime(uint64 t, const char* tzid,
                        struct tm* tm, int* microsec,
                        char (*tz_name)[kMaxTimeZoneStringLen + 1]) {
  // TODO Should we support negative values (dates before the epoch)?
  //    If we ever do, take care that the division below rounds towards -INF.
  CHECK(tzid != NULL);
  if (t < 0 || (t / kMicrosecPerSec) > kint32max)
    return false;
  time_t tsec = t / kMicrosecPerSec;
  if (microsec != NULL)
    *microsec = t % kMicrosecPerSec;

  const char* olson_id;
  const char* rfc_std_id = NULL;  // RFC822 id for standard time this time zone
  const char* rfc_dst_id = NULL;  // RFC822 id for DST this time zone
  const char* rfc_olson_id;
  int rfc_isdst;                  // DST-ness RFC822 identifier (ignored)
  int isdst = -1;                 // for result (init only to suppress warning)
  int offset;

  // Check for default and RFC822 identifiers.
  if (tzid[0] == '\0') {
    olson_id = CachedTimeZone::kDefaultOlsonId;
    rfc_std_id = CachedTimeZone::kDefaultStdId;
    rfc_dst_id = CachedTimeZone::kDefaultDstId;
  } else if (zone2tm(tzid, &offset, &rfc_isdst, &rfc_std_id, &rfc_dst_id,
                     &rfc_olson_id)) {
    if (rfc_olson_id == NULL) {
      // [A-IK-Z] or GMT, no DST; just use the offset and the identifier
      olson_id = NULL;  // indicate we already have offset and time zone name
      isdst = 0;
      if (tz_name != NULL) {
        if (rfc_std_id != NULL) {
          safestrncpy(*tz_name, rfc_std_id, sizeof(*tz_name));
        } else if (offset == 0) {
          safestrncpy(*tz_name, "GMT", sizeof(*tz_name));
        } else if (offset < 0) {
          snprintf(*tz_name, sizeof(*tz_name), "GMT-%02d:%02d",
                  (-offset) / 3600, ((-offset) / 60) % 60);
        } else {
          snprintf(*tz_name, sizeof(*tz_name), "GMT+%02d:%02d",
                    offset / 3600, (offset / 60) % 60);
        }
      }
    } else {
      // PST/PDT/PST8PDT/MST/MDT/MST7MDT/CST/CDT/CST6CDT/EST/EDT/EST5EDT.
      // Use ICU and ignore the DST-ness implied by the identifier
      olson_id = rfc_olson_id;
    }
  } else {
    // Must be an Olson time, use ICU.
    olson_id = tzid;
  }

  if (olson_id != NULL) {
    // Get the ICU TimeZone object; cache pacific zone
    CachedTimeZone cached_timezone(olson_id);
    TimeZone* timezone = cached_timezone.timezone();
    if (timezone == NULL)
      return false;
    // (double) ms since the epoch, in UTC
    UDate udate = static_cast<UDate>(tsec) * kMillisecPerSec;
    int32_t raw_offset_ms;
    int32_t dst_offset_ms;
    UErrorCode error_code = U_ZERO_ERROR;
    timezone->getOffset(udate, false, raw_offset_ms, dst_offset_ms, error_code);
    assert(error_code == U_ZERO_ERROR);
    // offset to local time
    offset = (raw_offset_ms + dst_offset_ms) / kMillisecPerSec;
    isdst = (dst_offset_ms != 0);
    if (tz_name != NULL) {
      if (rfc_std_id != NULL) {
        // RFC822 identifier, use our own short name
        safestrncpy(*tz_name, (isdst ? rfc_dst_id : rfc_std_id),
                    sizeof(*tz_name));
      } else {
        // Olson identifier, ask ICU for the short name.
        UnicodeString ustring;
        timezone->getDisplayName((dst_offset_ms != 0),
                                 TimeZone::SHORT,
                                 ustring);
        // convert it to UTF-8
        int32_t length;
        error_code = U_ZERO_ERROR;
        u_strToUTF8(*tz_name, kMaxTimeZoneStringLen, &length,
                    ustring.getBuffer(), ustring.length(), &error_code);
        assert(error_code == U_ZERO_ERROR);
        if (length >= sizeof(*tz_name))
          length = sizeof(*tz_name) - 1;
        (*tz_name)[length] = '\0';
      }
    }
  }

  // Apply the time zone offset and unpack the time value.
  tsec += offset;
  CHECK(gmtime_r(&tsec, tm) != NULL);
  tm->tm_isdst = isdst;  // needed for round trip of ambiguous local time
  return true;
}


bool LocalTimeToSzlTime(const struct tm& tm, int microsec, const char* tzid,
                        bool from_string, uint64* t) {
  int offset;                // time zone offset in seconds
  int isdst = tm.tm_isdst;   // only used for ambiguous hour at end of DST
  const char* olson_id;
  const char* rfc_std_id;    // RFC822 id for standard time this time zone
  const char* rfc_dst_id;    // RFC822 id for DST this time zone
  const char* rfc_olson_id;  // Olson id for this RFC822 id
  int rfc_isdst;             // whether DST according to RFC822 identifier
  assert(tzid != NULL);

  // Convert time to an epoch-based value.  We use mkgmtime() but we cannot
  // represent local time near the ends of the time_t UTC range when local
  // time does not fit in the time_t range.  We handle that by adjusting
  // the time by one year for 1969 and 2038 and then adjusting back, relying
  // on the fact that 1969, 1970, 2037 and 2038 are not leap years.
  struct tm adjusted_tm = tm;
  UDate udate;
  int range_adjust = 0;
  if (adjusted_tm.tm_year == 69) {
    adjusted_tm.tm_year = 70;
    range_adjust = -(60 * 60 * 24 * 365);
  } else if (adjusted_tm.tm_year == 138) {
    adjusted_tm.tm_year = 137;
    range_adjust = 60 * 60 * 24 * 365;
  }
  time_t adjusted_time = mkgmtime(&adjusted_tm);
  if (adjusted_time == static_cast<time_t>(-1))
    return false;
  // (double) ms since the epoch, local time
  udate = (static_cast<UDate>(adjusted_time) + range_adjust) * kMillisecPerSec;

  // Check for default and RFC822 identifiers.
  if (tzid[0] == '\0') {
    olson_id = CachedTimeZone::kDefaultOlsonId;
  } else if (zone2tm(tzid, &offset, &rfc_isdst, &rfc_std_id, &rfc_dst_id,
                     &rfc_olson_id)) {
    if (rfc_olson_id == NULL) {
      // [A-IK-Z] or GMT, no DST; just use the offset
      udate -= offset * kMillisecPerSec;
      *t = static_cast<uint64>(udate) * kMicrosecPerMillisec + microsec;
      return true;
    } else if (rfc_isdst == 0 || rfc_isdst == 1) {
      // PST/PDT/MST/MDT/CST/CDT/EST/EDT.
      if (from_string) {
        // We continue to honor incorrect DST-ness for compatibility.
        udate -= offset * kMillisecPerSec;
        *t = static_cast<uint64>(udate) * kMicrosecPerMillisec + microsec;
        return true;
      } else {
        // Use ICU, but remember that we specified expected DST-ness.
        olson_id = rfc_olson_id;
        isdst = rfc_isdst;
      }
    } else {
      // PST8PDT/MST7MDT/CST6CDT/EST5EDT.
      // Use ICU, but no preference on DST-ness.
      olson_id = rfc_olson_id;
    }
  } else {
    // Must be an Olson time, use ICU.
    // We leave isdst unchanged which works well for some
    // intrinsics (e.g. trunctominute) and could be surprising for others.
    olson_id = tzid;
  }


  CachedTimeZone cached_timezone(olson_id);
  TimeZone* timezone = cached_timezone.timezone();
  if (timezone == NULL)
    return false;
  int32_t raw_offset_ms;
  int32_t dst_offset_ms;
  UErrorCode error_code = U_ZERO_ERROR;
  timezone->getOffset(udate, true, raw_offset_ms, dst_offset_ms, error_code);
  assert(error_code == U_ZERO_ERROR);

  if (dst_offset_ms != 0) {
    // Problem: if the local time is during the DST set-forward period
    // (e.g. [2am,3am) in the spring) the ICU considers it to be a DST
    // time and gives an adjustment that yields a UTC time in [1am,2am) which
    // is a non-DST period.  Most tools treat this bogus hour as non-DST so that
    // [2am,3am) maps to [3am,4am) DST.
    // To fix this, try again using the UTC time indicated by the offset
    // including the dst component.  If we are outside this set-forward period
    // the offsets will be the same as before.  If we are in it, the dst offset
    // will change to zero which is what we want.
    error_code = U_ZERO_ERROR;
    timezone->getOffset(udate - (raw_offset_ms + dst_offset_ms), false,
                        raw_offset_ms, dst_offset_ms, error_code);
    assert(error_code == U_ZERO_ERROR);
  }

  if (dst_offset_ms == 0 && isdst == 1) {
    // Problem: during the DST set-back period (e.g. [1am,2am) in the autumn)
    // the local time is ambiguous; we need to know whether it is still DST
    // (the first time through this hour) or now STD (the second time).
    // ICU assumes STD.  If we were told the time is DST, crank the time
    // back by two hours and see if ICU thinks that is DST; if so, try the
    // original time again but compute the UTC reference time ourselves
    // using the raw offset and the newly found DST offset.
    // (It would be easier to get the raw and DST offsets ahead of time
    // and apply the dst offset as indicated and then ask ICU whether
    // it agrees, but there appears to be no such API.  We cannot assume
    // the DST offset is one hour so we have to ask ICU, and getOffset()
    // did not give us the value on the first call.)
    int32_t alt_dst_offset_ms;
    const int kTwoHoursInMs = 2 * 60 * 60 * kMillisecPerSec;
    error_code = U_ZERO_ERROR;
    timezone->getOffset(udate - kTwoHoursInMs, true, raw_offset_ms,
                        alt_dst_offset_ms, error_code);
    assert(error_code == U_ZERO_ERROR);
    if (alt_dst_offset_ms != 0) {
      // Yes, it was DST recently.  Try again computing UTC assuming DST.
      // Note that ICU may tell us it's still STD time, so we cannot just
      // use the raw and alt_dst offsets to get the final result.
      error_code = U_ZERO_ERROR;
      timezone->getOffset(udate + raw_offset_ms + alt_dst_offset_ms, false,
                          raw_offset_ms, dst_offset_ms, error_code);
      assert(error_code == U_ZERO_ERROR);
    }
  }

  // adjust for offset and convert to uint64
  udate -= (raw_offset_ms + dst_offset_ms);
  if (udate < 0.0 || udate > static_cast<UDate>(kint32max) * kMillisecPerSec)
    return false;
  *t = static_cast<uint64>(udate) * kMicrosecPerMillisec + microsec;
  return true;
}


// convert uint64 to C string
bool SzlTime2Str(uint64 szlt, const char* tz,
                 char (*buf)[kMaxTimeStringLen + 1]) {
  // Convert the the time to parsed form including the time zone name and
  // rewrite the string returned by asctime to put in the time zone.
  struct tm tm;
  char tz_name[kMaxTimeZoneStringLen + 1];
  if (!SzlTimeToLocalTime(szlt, tz, &tm, NULL, &tz_name))
    return false;
  char buf1[kMaxTimeStringLen + 1];
  asctime_r(&tm, buf1);
  // patch in time zone; string has 4 bytes of year on the end.
  // convert "Tue Apr 15 14:49:59 2003\n" to "Tue Apr 15 14:49:59 PDT 2003".
  char* yearp = strrchr(buf1, ' ');
  int len = snprintf(*buf, sizeof(*buf), "%.*s %s%s",
                      yearp-buf1, buf1, tz_name, yearp);
  (*buf)[len - 1] = '\0';  // remove '\n' at the end
  return true;
}


// convert C string into sawzall time value, with microseconds
bool date2uint64(const char* date, const char* tz, uint64* timep) {
  // Parse the string and build the components and the time zone name.
  struct tm tm;
  int microsec;
  char explicit_tz[kMaxTimeZoneStringLen + 1];
  if (!date2tm(date, &tm, &microsec, &explicit_tz))
    return false;

  // use an explicit name if one is given, else use our parameter
  const char* tzid;
  if (explicit_tz[0] != '\0')
    tzid = explicit_tz;
  else
    tzid = tz;

  return LocalTimeToSzlTime(tm, microsec, tzid, true, timep);
}


}  // namespace sawzall
