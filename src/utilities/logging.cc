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

#include <string.h>

#include <iostream>
#include <fstream>

#include "public/porting.h"
#include "public/commandlineflags.h"
#include "public/logging.h"


DEFINE_int32(v, 0, "Show all VLOG(m) messages for m <= this.");

DEFINE_string(logfile, "", "Direct log output messages to this file.");

namespace sawzall {


static const char* basename(const char* filename) {
  const char* last_slash = strrchr(filename, '/');
  return (last_slash == NULL) ? filename : last_slash+1;
}

LogMessage::LogMessage(const char* file, int line, unsigned int severity)
  : severity_(severity) {
  const char* severity_text;
  if (severity == INFO)
    severity_text = "INFO";
  else if (severity == WARNING)
    severity_text = "WARNING";
  else if (severity == ERROR)
    severity_text = "ERROR";
  else if (severity == FATAL)
    severity_text = "FATAL";
  else
    severity_text = "FATAL [Internal error: invalid severity]";
  stream() << "[" << severity_text << " " <<
               basename(file) << ":" << line << "] ";
}

LogMessage::~LogMessage() {
  stream() << endl;
  if (severity_ >= FATAL)
    abort();
}

// TODO: set the log output from a command line flag
ostream& LogMessage::stream() const {
  static ofstream log_stream;
  if (log_stream.is_open())
    return log_stream;
  if (!FLAGS_logfile.empty() && !log_stream.fail()) {
    log_stream.open(FLAGS_logfile.c_str(), ios_base::out|ios_base::trunc);
    if (!log_stream.fail())
      return log_stream;
    else
      fprintf(stderr, "Unable to open file \"%s\"\n", FLAGS_logfile.c_str());
  }
  return cerr;
}


}  // namespace sawzall
