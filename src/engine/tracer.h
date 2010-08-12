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

namespace sawzall {

// A little tracing facility. Iff the fmt string for the constructor
// starts with a '(', the destructor will print a closing ')'. Uses
// are of the form:
//
// Trace t(&tracer, "(Expression");    // block-structured trace
// Trace(&tracer, "name = %s", name);  // single line
//
// where tracer is a particular named Tracer*:
//
// Tracer tracer("myname");            // a named tracer
//
// It's ok to leave tracing calls in the code, they are calls to
// empty routines. (Ideally we would want them to disappear completely,
// for instance by making the Tracer() calls inlinable empty functions.
// However, then the compiler complains with warnings about unused
// variables at the call site - sigh. The other alternative is to
// use macros at all call sites, which we like to avoid.)

class Tracer {
 public:
  // To enable this tracer, the --trace flag must contain name.
  Tracer(const char* name);

 private:
  const char* name_;
  int level_;

  friend class Trace;
};


class Trace {
 public:
  Trace(Tracer* tracer, const char* fmt, ...);
  ~Trace();

 private:
  Tracer* tracer_;
  char quote_;
};

}  // namespace sawzall
