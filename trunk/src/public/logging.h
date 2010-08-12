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

#include <ostream>
#include <stdlib.h>
#include <string.h>

#include "public/commandlineflags.h"  // for DECLARE_int32


DECLARE_int32(v);


namespace sawzall {


#define LOG(severity) sawzall::LogMessage::LogVoid() & \
 sawzall::LogMessage(__FILE__, __LINE__, sawzall::LogMessage::severity).stream()

class LogMessage {
public:
  LogMessage(const char* file, int line, unsigned int severity);
  ~LogMessage();

  ostream& stream() const ;

  // INFO is not logged by default, needs --v=1
#ifndef NDEBUG
  enum { INFO, WARNING, ERROR, FATAL, DFATAL = ERROR };
#else
  enum { INFO, WARNING, ERROR, FATAL, DFATAL = FATAL };
#endif

  // Lower precedence than "<<", makes third operand of "?:" void.
  struct LogVoid { void operator&(const ostream&) { } };

private:
  const unsigned int severity_;
};


#define CHECK(condition) (condition) ? (void) 0 : LOG(FATAL) << \
  "assertion failed: CHECK(" #condition ")"

#define CHECK_EQ(a,b) ((a) == (b)) ? (void) 0 : LOG(FATAL) << \
  "assertion failed: CHECKEQ(" #a "," #b ")"
#define CHECK_NE(a,b) ((a) != (b)) ? (void) 0 : LOG(FATAL) << \
  "assertion failed: CHECKNE(" #a "," #b ")"
#define CHECK_LT(a,b) ((a) <  (b)) ? (void) 0 : LOG(FATAL) << \
  "assertion failed: CHECKLT(" #a "," #b ")"
#define CHECK_LE(a,b) ((a) <= (b)) ? (void) 0 : LOG(FATAL) << \
  "assertion failed: CHECKLE(" #a "," #b ")"
#define CHECK_GT(a,b) ((a) >  (b)) ? (void) 0 : LOG(FATAL) << \
  "assertion failed: CHECKGT(" #a "," #b ")"
#define CHECK_GE(a,b) ((a) >= (b)) ? (void) 0 : LOG(FATAL) << \
  "assertion failed: CHECKGE(" #a "," #b ")"


#ifndef NDEBUG
#ifndef assert
#define assert(condition) (condition) ? (void) 0 : LOG(FATAL) << \
  "assertion failed: assert(" #condition ")"
#endif
#define DCHECK(condition) CHECK(condition)
#define DCHECK_EQ(a,b) CHECK_EQ(a,b)
#define DCHECK_NE(a,b) CHECK_NE(a,b)
#define DCHECK_LT(a,b) CHECK_LT(a,b)
#define DCHECK_LE(a,b) CHECK_LE(a,b)
#define DCHECK_GT(a,b) CHECK_GT(a,b)
#define DCHECK_GE(a,b) CHECK_GE(a,b)
#else
#ifndef assert
#define assert(condition) while (false) CHECK(condition)
#endif
#define DCHECK(condition) while (false) CHECK(condition)
#define DCHECK_EQ(a,b) while (false) CHECK_EQ(a,b)
#define DCHECK_NE(a,b) while (false) CHECK_NE(a,b)
#define DCHECK_LT(a,b) while (false) CHECK_LT(a,b)
#define DCHECK_LE(a,b) while (false) CHECK_LE(a,b)
#define DCHECK_GT(a,b) while (false) CHECK_GT(a,b)
#define DCHECK_GE(a,b) while (false) CHECK_GE(a,b)
#endif


#define VLOG_IS_ON(verboselevel) (FLAGS_v >= (verboselevel))

#define VLOG(verboselevel) !VLOG_IS_ON(verboselevel) ? (void) 0 : LOG(INFO)


template <typename T>
T* CheckNotNull(const char *file, int line, const char *text, T* t) {
  if (t == NULL)
    LogMessage(file, line, LogMessage::FATAL).stream() <<
     "'" << text << "' Must be non NULL";
  return t;
}


#define CHECK_NOTNULL(val) \
  sawzall::CheckNotNull(__FILE__, __LINE__, #val, (val))


}  // namespace sawzall
