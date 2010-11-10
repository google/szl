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

// Implements SzlEmitterFactory class.
// See comments in the header file.

#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/strutils.h"
#include "fmt/fmt.h"

#include "public/sawzall.h"
#include "public/szlvalue.h"
#include "public/emitterinterface.h"
#include "public/szlemitter.h"
#include "public/szltabentry.h"

#include "app/szlemitterfactory.h"
#include "app/printemitter.h"


SzlEmitterFactory::SzlEmitterFactory(Fmt::State* f, string vocal_szl_emitters):
    f_(f) {
  if (vocal_szl_emitters == "") {
    all_print_emitters_ = true;
  } else {
    all_print_emitters_ = false;
    SplitStringAtCommas(vocal_szl_emitters, &vocal_szl_emitters_);
  }
}


SzlEmitterFactory::~SzlEmitterFactory() {
  // Explicitly deallocate each emitter
  for (int i = 0; i < emitters_.size(); i++) {
    sawzall::Emitter* e = emitters_[i];
    delete e;
  }
}


bool SzlEmitterFactory::is_vocal_szl_emitter(string name) {
  return (find(vocal_szl_emitters_.begin(), vocal_szl_emitters_.end(), name) !=
          vocal_szl_emitters_.end());
}


// Creates aggregating emitter for most tables and falls back to the
// printing emitter for those that don't have aggregation support
sawzall::Emitter* SzlEmitterFactory::NewSzlEmitter(
    sawzall::TableInfo* table_info, string* error) {
  const char* name = table_info->name();
  sawzall::Emitter* emitter = NULL;
  SzlType szl_type(SzlType::VOID);
  string type_error;
  if (szl_type.ParseFromSzlArray(table_info->type_string().data(),
                                 table_info->type_string().size(),
                                 &type_error)) {
    SzlTabWriter* tab_writer = SzlTabWriter::CreateSzlTabWriter(szl_type,
                                                                &type_error);
    if (tab_writer != NULL && tab_writer->WritesToMill())
      emitter = new SzlEmitter(name, tab_writer, is_vocal_szl_emitter(name));
    else if (type_error.empty())
      emitter = new PrintEmitter(name, f_, is_vocal_szl_emitter(name));
  }
  if (emitter == NULL) {
    CHECK(!type_error.empty());
    *error = StringPrintf("failed to create emitter for table %s: %s",
                          name, type_error.c_str());
  }
  return emitter;
}


sawzall::Emitter* SzlEmitterFactory::NewEmitter(sawzall::TableInfo* table_info,
                                                string* error) {
  const char* name = table_info->name();
  sawzall::Emitter* emitter = NULL;
  if (all_print_emitters_) {
    emitter = new PrintEmitter(name, f_, true);
  } else {
    emitter = NewSzlEmitter(table_info, error);
  }
  emitters_.push_back(emitter);
  return emitter;
}
