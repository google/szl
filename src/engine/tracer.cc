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

#include "engine/globals.h"
#include "public/commandlineflags.h"

#include "engine/tracer.h"


DEFINE_string(trace, "", "list of tracers enabled");


namespace sawzall {

static const int nblanks = 2;  // no. of blanks per indentation level


Tracer::Tracer(const char* name) {
  name_ = name;
  level_ = -1;  // disable tracing
  if (FLAGS_trace.find(name) != string::npos)
    level_ = 0;  // enable tracing
}


Trace::Trace(Tracer* tracer, const char* fmt, ...) {
#ifndef NDEBUG
  tracer_ = tracer;
  if (tracer_->level_ >= 0) {
    F.print("%*s", tracer_->level_ * nblanks, "");
    quote_ = fmt[0];
    if (quote_ == '(') {
      F.print("(");
      fmt++;
    }
    va_list args;
    va_start(args, fmt);
    char* msg = F.vsmprint(fmt, &args);
    va_end(args);
    F.print("%s: %s\n", tracer_->name_, msg);
    free(msg);
    tracer_->level_++;
  }
#endif
}


Trace::~Trace() {
#ifndef NDEBUG
  if (tracer_->level_ > 0) {
    tracer_->level_--;
    if (quote_ == '(')
      F.print("%*s)\n", tracer_->level_ * nblanks, "");
  }
#endif
}

}  // namespace sawzall
