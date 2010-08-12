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
#include <vector>

#include "public/hash_map.h"

#include "engine/globals.h"
#include "public/logging.h"

#include "utilities/strutils.h"
#include "utilities/random_base.h"
#include "utilities/acmrandom.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/frame.h"
#include "engine/proc.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/engine.h"


namespace sawzall {


// Helper, defined later.
static void SplitCSVLine(char* line, vector<char*>* cols);


// returns a value x such that 0.0 < x < 1.0
static const char rand_doc[] =
  "Return a random floating point number x in the range 0.0 < x < 1.0.";

static void rand(Proc* proc, Val**& sp) {
  Engine::push_szl_float(sp, proc, proc->rand()->RndFloat());
}


// returns a value x such that 0 <= x < n
static const char nrand_doc[] =
  "Return a random integer x in the range 0 <= x < n. Returns undef "
  "if n is negative or zero";

static const char* nrand(Proc* proc, Val**& sp) {
  szl_int n = Engine::pop_szl_int(sp);
  if (n <= 0)
    return proc->PrintError("nrand() argument %lld <= 0; must be positive", n);
  Engine::push_szl_int(sp, proc, proc->rand()->Next64() % n);
  return NULL;
}


// tobase64(input: bytes, websafe: bool): bytes
// convert the input string to a base64 representation
static const char *tobase64_doc =
"The function tobase64 takes an input bytes array and returns a "
"bytes array containing its base64 encoding.  The boolean flag, "
"if set, invokes the web-safe encoding that uses '-' instead of '+' "
"and '_' instead of '/', and does not pad the ouput with =.";

static void tobase64(Proc *proc, Val**& sp) {
  BytesVal *input = Engine::pop_bytes(sp);
  bool web_safe = Engine::pop_szl_bool(sp);
  // calculate new buffer size and allocate it
  const int len = CalculateBase64EscapedLen(input->length());
  BytesVal* output = Factory::NewBytes(proc, len);
  // convert data into newly created bytes array
  int out_len;
  if (web_safe) {
    out_len = WebSafeBase64Escape(input->u_base(),
                                  input->length(),
                                  output->base(),
                                  output->length(),
                                  false);
  } else {
    out_len = Base64Escape(input->u_base(),
                           input->length(),
                           output->base(),
                           output->length());
  }
  // final length may differ, so make a slice
  assert(out_len <= len);
  BytesVal* slice = SymbolTable::bytes_form()->NewSlice(proc, output, 0, out_len);
  input->dec_ref();
  Engine::push(sp, slice);
}


// frombase64(input: bytes, websafe: bool): bytes
// convert the input string from a base64 representation
static const char *frombase64_doc =
"The function frombase64 takes an input bytes array and returns a "
"bytes array containing its base64 decoding.  The boolean flag, if "
"set, invokes the web-safe decoding that uses '-' instead of '+' "
"and '_' instead of '/'.";

static const char *frombase64(Proc *proc, Val**& sp) {
  BytesVal *input = Engine::pop_bytes(sp);
  bool web_safe = Engine::pop_szl_bool(sp);
  // assume new buffer size <= old size and allocate it
  const int len = input->length();
  BytesVal* output = Factory::NewBytes(proc, len);
  // convert data into newly created bytes array
  int out_len;
  if (web_safe) {
    out_len = WebSafeBase64Unescape(input->base(),
                                    input->length(),
                                    output->base(),
                                    output->length());
  } else {
    out_len = Base64Unescape(input->base(),
                             input->length(),
                             output->base(),
                             output->length());
  }
  // Decrement input's refcount whether or not the conversion succeeded.
  input->dec_ref();
  if (out_len < 0) {
    // If the conversion failed, output is garbage, so decrement its refcount.
    output->dec_ref();
    return proc->PrintError("Failed to decode base64 string '%.*s'",
                            input->length(),
                            static_cast<char *>(input->base()));
  }
  assert(out_len <= len);
  // final length may differ, so make a slice
  BytesVal* slice = SymbolTable::bytes_form()->NewSlice(proc, output, 0, out_len);
  Engine::push(sp, slice);
  return NULL;
}


// Copy the data from an ArrayDesc into a newly allocated
// char[], and zero-terminate it.
// Return the new array, and store the length
// in the location given by len unless it is NULL.
static char* SaveCharArray(BytesVal* arr) {
  int32 n = arr->length();
  char* line = new char[n+1];  // explicitly deallocated by clients of SaveCharArray
  memmove(line, arr->base(), n);
  line[n] = '\0';
  return line;
}


static const char splitcsvline_doc[] =
  "The function splitcsvline takes a line of UTF-8 bytes and splits it "
  "at commas, ignoring leading and trailing white space and using '\"' for "
  "quoting. It returns the array of fields produced.";

static void splitcsvline(Proc* proc, Val**& sp) {
  BytesVal* aline = Engine::pop_bytes(sp);

  vector<char*> values;
  char* line = SaveCharArray(aline);
  SplitCSVLine(line, &values);

  ArrayVal* strs = Factory::NewBytesArray(proc, values.size());
  for (int i = 0; i < values.size(); i++)
    strs->at(i) = Factory::NewBytesC(proc, values[i]);

  delete[] line;
  aline->dec_ref();
  Engine::push(sp, strs);
}

// Try to save a pointer to the start of field n (1 indexed).
// Return true iff we were able to.
static bool SaveField(int n, char* line, vector<char*> *fields, vector<char*> *results) {
  if (n < 0) {
    return false;
  } else if (n == 0) {
    // It would be consistent with matchstrs to use field 0 to
    // refer to the entire line, but by the time we get here
    // the original has been destroyed by SplitCSVLine
    // (quotes removed, contents shifted) so we can't easily do it.
    // I'll leave this as a placeholder, in case we want to
    // make it work someday.
    //  results->push_back(line);
    return false;
  } else if (n > fields->size()) {
    // There are two possible behaviors here...
    if (false) {
      // in this mode an out of bounds field makes the result undefined.
      return false;
    } else {
      // in this mode an out of bounds field returns an empty string.
      static char empty[] = "";
      results->push_back(empty);
      return true;
    }
  } else {
    results->push_back((*fields)[n-1]);
    return true;
  }
  return false;  // not reached
}


static const char splitcsv_doc[] =
  "The function splitcsv takes an array of UTF-8 bytes "
  "containing lines of text, such as that produced by "
  "the load() builtin. It splits each line using "
  "the same method as splitcsvline, and then selects "
  "the fields indicated by the second argument "
  "(numbered starting at 1). "
  "The return value is a flat array of the collected fields.";

static const char* splitcsv(Proc* proc, Val**& sp) {
  BytesVal* astr = Engine::pop_bytes(sp);
  ArrayVal* aflds = Engine::pop_array(sp);

  // Make a copy so we can nul terminate it,
  // and allow SplitCSVLine to destroy it.

  char* str = SaveCharArray(astr);
  int len = astr->length();
  char* end = str + len;

  // Walk the csv string, and split out the fields of each
  // line.  We do that instead of handling it all at once
  // (which SplitCSVLine can do) so that we can make sure
  // the right number of fields are present.

  vector<char*> values;
  char* p = str;
  while (p < end) {
    vector<char*> fields;
    char* q = strchr(p, '\n');
    if (q != NULL)
      *q = '\0';
    SplitCSVLine(p, &fields);
    for (int i = 0; i < aflds->length(); i++) {
      int field = aflds->at(i)->as_int()->val();
      if (!SaveField(field, p, &fields, &values))
        return proc->PrintError("splitcsv: field %d > %d max",
                           field, static_cast<int>(fields.size()));
    }
    p = (q == NULL) ? end : q+1;
  }

  ArrayVal* strs = Factory::NewBytesArray(proc, values.size());
  for (int i = 0; i < values.size(); i++)
    strs->at(i) = Factory::NewBytesC(proc, values[i]);

  delete[] str;
  astr->dec_ref();
  aflds->dec_ref();
  Engine::push(sp, strs);
  return NULL;
}


// Helper to split a string of CSV values.
// Modifies the input string by inserting nulls to terminate the values
// and removing whitespace and quote escaping.
static void SplitCSVLine(char* line, vector<char*>* cols) {
  char* end_of_line = line + strlen(line);
  char* end;
  char* start;

  for (; line < end_of_line; line++) {
    // Skip leading whitespace
    while (ascii_isspace(*line))
      ++line;

    if (*line == '"') {     // Quoted value...
      start = ++line;
      end = start;
      for (; *line; line++) {
        if (*line == '"') {
          line++;
          if (*line != '"')  // [""] is an escaped ["]
            break;           // but just ["] is end of value
        }
        *end++ = *line;
      }
      // All characters after the closing quote and before the comma
      // are ignored.
      line = strchr(line, ',');
      if (line == NULL)
        line = end_of_line;
    } else {
      start = line;
      line = strchr(line, ',');
      if (line == NULL)
        line = end_of_line;
      // Skip all trailing whitespace
      for (end = line; end > start && ascii_isspace(end[-1]); --end)
        ;
    }
    const bool need_another_column =
      (*line == ',') && (line == end_of_line - 1);
    *end = '\0';
    cols->push_back(start);
    // If line was something like [paul,] (comma is the last character
    // and is not proceeded by whitespace or quote) then we are about
    // to eliminate the last column (which is empty). This would be
    // incorrect.
    if (need_another_column)
      cols->push_back(end);

    assert(*line == '\0' || *line == ',');
  }
}


static void Initialize() {
  // make sure the SymbolTable is initialized
  assert(SymbolTable::is_initialized());
  Proc* proc = Proc::initial_proc();

  // shortcuts for predefined types
  Type* bytes_type = SymbolTable::bytes_type();
  Type* int_type = SymbolTable::int_type();
  Type* bool_type = SymbolTable::bool_type();
  Type* float_type = SymbolTable::float_type();
  Type* array_of_bytes_type = SymbolTable::array_of_bytes_type();
  Type* array_of_int_type = SymbolTable::array_of_int_type();

#define DEF(name, type, attribute) \
  SymbolTable::RegisterIntrinsic(#name, type, name, name##_doc, attribute)

  // signature: (): float
  DEF(rand,
      FunctionType::New(proc)->res(float_type), Intrinsic::kNormal);

  // signature: (int): int
  DEF(nrand,
      FunctionType::New(proc)->par("n", int_type)->res(int_type),
      Intrinsic::kNormal);

  // signature: (bytes, bool): bytes
  DEF(tobase64,
      FunctionType::New(proc)->par("input", bytes_type)->
        par("websafe", bool_type)->res(bytes_type),
      Intrinsic::kCanFold);

  // signature: (bytes, bool): bytes
  DEF(frombase64,
      FunctionType::New(proc)->par("input", bytes_type)->
        par("websafe", bool_type)->res(bytes_type),
      Intrinsic::kCanFold);

  // signature: (bytes): array of bytes
  DEF(splitcsvline,
      FunctionType::New(proc)->par("csv", bytes_type)->
        res(array_of_bytes_type),
      Intrinsic::kNormal);

  // signature: (bytes, int, int): array of bytes
  DEF(splitcsv,
      FunctionType::New(proc)->par("csv", bytes_type)->
        par("fields", array_of_int_type)->
        res(array_of_bytes_type),
      Intrinsic::kNormal);

#undef DEF
}

}  // namespace sawzall


// Note: This must happen outside the namespace
// to avoid linker/name mangling problems.
REGISTER_MODULE_INITIALIZER(
  GoogleIntrinsic,
  { REQUIRE_MODULE_INITIALIZED(Sawzall);
    sawzall::Initialize();
  }
);
