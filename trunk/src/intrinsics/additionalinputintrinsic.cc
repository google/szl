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

// Support for additional inputs to szl provided by the calling process;
// values can also be set from Sawzall code and setting can be locked from
// sawzall code, which may be useful in security prologues.
//
// Code running a szl Proc class can provide additional byte strings
// as input to Sawzall beyond that which is made available as the input proto.
// To manipulate additional inputs, use the AddInput, ClearInputs, and GetInput
// functions in Proc.  These inputs are mapped from key strings and are set as
// strings in C++ code, but are made available as bytes in Sawzall via the
// getadditionalinput(key: string) intrinsic.
//
// Example C++ Usage:
//
// sawzall::Process proc;
// MyProtoInput input;
// MyProtoInput alternate;
// string message1, message2;
//
// input.AppendToString(&message1);
// alternate.AppendToString(&message2);
// proc.proc()->AddInput("alternate_record", message2);
// CHECK(proc.Run(message1, ""));
//
//
// Example sawzall code:
//
// proto "myprotobufferinput.proto"
//
// input_record: MyProtoInput = input;
// alternate_record: MyProtoInput = getadditionalinput("alternate_record");


#include <stdlib.h>

#include "engine/globals.h"
#include "public/logging.h"

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
#include "engine/scanner.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/engine.h"
#include "engine/intrinsic.h"
#include "public/sawzall.h"


namespace sawzall {

// ============================================================================
// C++ access

// C++ function to retrieve the most recent bytes value set by
// SetIdentifier (C++) or setadditionalinput (Sawzall) for this key.
// Returns false if no value has been set for this key.

bool GetIdentifier(Process* p, string label, string* identifier) {
  StringVal* key = Factory::NewStringCPP(p->proc(), label);
  BytesVal* value = p->proc()->GetInput(key);
  key->dec_ref();
  if (!value) {
    return false;
  }
  *identifier = string(value->base(), value->length());
  value->dec_ref();
  return true;
}

// C++ function to set values for use gy GetIdentifier (C++ or
// getadditionalinput (Sawzall).

void SetIdentifier(Process* p, string label, const string& identifier) {
  p->proc()->AddInput(label.c_str(), identifier.data(), identifier.size());
}


// ============================================================================
// Sawzall intrinsics

static const char kLock[] = "setadditionalvalue_LOCK";
static StringVal* lock_string;  // kLock as a StringVal, for use with GetInput


// If the system using Sawzall prepends a security prologue to the code,
// it can use lockadditionalinput() to prevent additional values from being
// added.

static const char lockadditionalinput_doc[] =
  "Prevents further calls to setadditionalinput for this record.";

static void lockadditionalinput(Proc* proc, Val**& sp) {
  // Put a marker in the AdditionaInput data structure, which is cleared at the
  // start of each record.
  proc->AddInput(kLock, "", 0);
}


// Like SetIdentifier, but from Sawzall and obeys lockadditionalinput().

static const char setadditionalinput_doc[] = "Stores a (label, value) pair.";

static void setadditionalinput(Proc* proc, Val**& sp) {
  string label = Engine::pop_cpp_string(proc, sp);
  BytesVal* value = Engine::pop_bytes(sp);
  BytesVal* locked = proc->GetInput(lock_string);
  // Locked will be non-NULL if lockadditionalinput was called during the
  // processing of this record.
  if (locked == NULL) {
    // AddInput takes ownership of value, so no dec_ref is needed.
    proc->AddInput(label.c_str(), value);
  } else {
    locked->dec_ref();
    value->dec_ref();
    LOG(ERROR) << "May not call setadditionalinput after "
        "lockadditionalinput";
  }
}


// Like GetIdentifier, but from sawzall.

static const char getadditionalinput_doc[] =
  "A map of strings to bytes may be provided to Proc by the process "
  "running sawzall.  Return the bytes mapped to by the argument.";

static void getadditionalinput(Proc* proc, Val**& sp) {
  // get the input value
  StringVal* a = Engine::pop_string(sp);
  BytesVal* result = proc->GetInput(a);
  a->dec_ref();
  // push the value, if any
  if (result != NULL) {
    Engine::push(sp, result);
  } else {
    // push empty values if no input
    Engine::push(sp, Factory::NewBytesInit(proc, 0, ""));
  }
}


#define DEF(name, type, attribute) \
    SymbolTable::RegisterIntrinsic( \
        #name, type, name, name##_doc, attribute)

static void Initialize() {
  // make sure the SymbolTable is initialized
  assert(SymbolTable::is_initialized());
  Proc* proc = Proc::initial_proc();
  Type* string_type = SymbolTable::string_type();
  Type* bytes_type = SymbolTable::bytes_type();
  Type* void_type = SymbolTable::void_type();

  lock_string = Factory::NewStringC(proc, kLock);

  // register getadditionalinput
  {
    FunctionType* t = 
      FunctionType::New(proc)->par("variable", string_type)-> res(bytes_type);
    DEF(getadditionalinput, t, Intrinsic::kNormal);
  }

  // register setadditionalinput and lockadditionalinput
  { FunctionType* t =
      FunctionType::New(proc)->par("label", string_type)->
                               par("value", bytes_type)->
                               res(void_type);
    DEF(setadditionalinput, t, Intrinsic::kNormal);
  }
  { FunctionType* t =
      FunctionType::New(proc)->res(void_type);
    DEF(lockadditionalinput, t, Intrinsic::kNormal);
  }
}

}  // namespace sawzall

// Note: This must happen outside the namespace
// to avoid linker/name mangling problems.
REGISTER_MODULE_INITIALIZER(
  AdditionalInputIntrinsic, {
    REQUIRE_MODULE_INITIALIZED(Sawzall);
    sawzall::Initialize();
  }
);
