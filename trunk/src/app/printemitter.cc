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

#include <assert.h>

#include "public/porting.h"
#include "utilities/strutils.h"
#include "fmt/fmt.h"

#include "public/emitterinterface.h"
#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/szlemitter.h"

#include "app/printemitter.h"


namespace sawzall { extern Fmt::Formatter F; }


PrintEmitter::PrintEmitter(const char* table_name, Fmt::State* f, bool verbose)
  : table_name_(table_name),
    f_(f),
    verbose_(verbose),
    position_(DONE) {

  // This initial state_ (with nesting_level() == -1) should never be used.  We
  // initialize it only for the sake of initializing everything.
  state_.type  = sawzall::Emitter::EMIT;
  state_.field_count = -1;
}


void PrintEmitter::Begin(sawzall::Emitter::GroupType type, int len) {
  if (nesting_level() >= 0) {
    Prologue();
  }
  // save previous state
  saved_states_.push_back(state_);
  // set new state
  state_.type = type;
  state_.field_count = 0;
  // print table name if needed
  if (position_ == INIT)
    P("emit %s", table_name_);
  // print wrapper if needed
  switch (type) {
    case sawzall::Emitter::EMIT:
      assert(nesting_level() == 0 && position_ < INIT);
      position_ = INIT;
      break;
    case sawzall::Emitter::INDEX:
      assert(nesting_level() == 1 && position_ < IN_INDEX);
      position_ = IN_INDEX;
      break;
    case sawzall::Emitter::ELEMENT:
      assert(nesting_level() == 1 && position_ < IN_ELEMENT);
      P(" <- ");
      position_ = IN_ELEMENT;
      break;
    case sawzall::Emitter::WEIGHT:
      assert(nesting_level() == 1 && position_ < IN_WEIGHT);
      P(" weight ");
      position_ = IN_WEIGHT;
      break;
    case sawzall::Emitter::ARRAY:  // fall through
    case sawzall::Emitter::MAP:  // fall through
    case sawzall::Emitter::TUPLE:
      assert(nesting_level() > 1 &&
             (position_ == IN_INDEX || position_ == IN_ELEMENT
              || position_ == IN_WEIGHT));
      P("{");  // begin of composite
      break;
  }
}


void PrintEmitter::End(sawzall::Emitter::GroupType type, int len) {
  // print wrapper if needed
  assert(type == state_.type);
  switch (state_.type) {
    case sawzall::Emitter::EMIT:
      assert(nesting_level() == 0 && position_ >= AFTER_ELEMENT);
      P(";\n");
      Fmt::fmtfdflush(f_);
      position_ = DONE;
      break;
    case sawzall::Emitter::INDEX:
      assert(nesting_level() == 1 && position_ == IN_INDEX);
      position_ = AFTER_INDEX;
      break;
    case sawzall::Emitter::ELEMENT:
      assert(nesting_level() == 1 && position_ == IN_ELEMENT);
      position_ = AFTER_ELEMENT;
      break;
    case sawzall::Emitter::WEIGHT:
      assert(nesting_level() == 1 && position_ == IN_WEIGHT);
      position_ = AFTER_WEIGHT;
      break;
    case sawzall::Emitter::MAP:
      // If this is an empty map, add ":", to arrive at "{:}" instead of "{}".
      if (len == 0) {
        P(":");
      }
      // fall through
    case sawzall::Emitter::ARRAY:  // fall through
    case sawzall::Emitter::TUPLE:
      assert(nesting_level() > 1 &&
             (position_ == IN_INDEX || position_ == IN_ELEMENT ||
              position_ == IN_WEIGHT));
      P("}");  // end of composite
      break;
  }
  // restore previous state
  state_ = saved_states_.back();
  saved_states_.pop_back();
  if (nesting_level() >= 0) {
    Epilogue();
  }
}


void PrintEmitter::PutBool(bool b) {
  Prologue();
  P("%s", b ? "true" : "false");
  Epilogue();
}


void PrintEmitter::PutBytes(const char* p, int len) {
  Prologue();
  P("bytes({");
  for (int i = 0; i < len; i++) {
    if (i > 0)
      P(", ");
    P("%d", p[i]);
  }
  P("})");
  Epilogue();
}


void PrintEmitter::PutInt(int64 i) {
  Prologue();
  P("%lld", i);
  Epilogue();
}


void PrintEmitter::PutFloat(double f) {
  Prologue();
  char buf[64];
  FloatToAscii(buf, f);  // FloatToAscii guarantees a decimal point.
  P("%s", buf);
  Epilogue();
}


void PrintEmitter::EmitInt(int64 i) {
  P("emit %s <- %lld;\n", table_name_, i);
}


void PrintEmitter::EmitFloat(double f) {
  P("emit %s <- ", table_name_);
  char buf[64];
  FloatToAscii(buf, f);  // FloatToAscii guarantees a decimal point.
  P("%s;\n", buf);
}


void PrintEmitter::PutFingerprint(uint64 fp) {
  Prologue();
  P("0x%.16llxP", fp);  // see engine/globals.h for canonical format string
  Epilogue();
}


void PrintEmitter::PutString(const char* s, int len) {
  Prologue();
  P((verbose_ ? "%.*q" : "%.*s"), len, s);  // quote strings in verbose mode
  Epilogue();
}


void PrintEmitter::PutTime(uint64 t) {
  Prologue();
  P("%lluT", t);  // see engine/globals.h for canonical format string
  Epilogue();
}


int PrintEmitter::nesting_level() const {
  return saved_states_.size() - 1;
}


void PrintEmitter::P(const char* fmt, ...) {
  if (verbose_ || position_ == IN_ELEMENT) {
    va_list args;
    va_start(args, fmt);
    sawzall::F.fmtvprint(f_, fmt, &args);
    va_end(args);
  }
}


void PrintEmitter::Prologue() {
  assert(nesting_level() >= 0);
  // print index wrapper if needed
  if (position_ == PrintEmitter::IN_INDEX && nesting_level() == 1)
    P("[");
  // print field separator if needed
  switch (state_.type) {
    case sawzall::Emitter::ARRAY:  // fall through
    case sawzall::Emitter::MAP:  // fall through
    case sawzall::Emitter::TUPLE:
      if (state_.field_count > 0) {
        if (state_.type == sawzall::Emitter::MAP &&
            (state_.field_count & 1) != 0)
          P(": ");  // key: value separator for maps
        else
          P(", ");  // field separator
      }
      break;
    default:
      // nothing to do
      break;
  }
}


void PrintEmitter::Epilogue() {
  assert(nesting_level() >= 0);
  // print index wrapper if needed
  if (position_ == PrintEmitter::IN_INDEX && nesting_level() == 1)
    P("]");
  // field done
  state_.field_count++;
}
