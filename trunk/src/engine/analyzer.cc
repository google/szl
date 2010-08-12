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

#include "public/hash_map.h"

#include "engine/globals.h"
#include "public/commandlineflags.h"
#include "public/logging.h"

#include "engine/error.h"
#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/opcode.h"
#include "engine/node.h"
#include "engine/scanner.h"
#include "engine/proc.h"
#include "engine/analyzer.h"
#include "engine/symboltable.h"


namespace sawzall {


DEFINE_bool(optimize_sawzall_code,
            true,
            "run the optimizer pass for faster Sawzall execution");
DEFINE_bool(remove_unreachable_functions,
            true,
            "aid optimization by removing functions that are never referenced");
DECLARE_bool(report_all_errors);



void Analyzer::Error(const FileLine* fileline, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Errorv(fileline, false, fmt, &args);
  va_end(args);
}


void Analyzer::Warning(const FileLine* fileline, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Errorv(fileline, true, fmt, &args);
  va_end(args);
}


void Analyzer::Errorv(const FileLine* fileline, bool is_warning, const char* fmt,
                      va_list* args) {
  // only report and count an error if it's on a different line
  if (FLAGS_report_all_errors || last_error_line_ != fileline->line()) {
    // TODO: represent the file using a pointer to a SourceFile
    // object, and have those contain a pointer to the file that included
    // them, so we can provide the same list that Scanner prints?
    proc_->error()->Reportv(fileline, is_warning, fmt, args);

    // update state
    if (!is_warning) {
      error_count_++;
      last_error_line_ = fileline->line();
    }
  }
}


Variable* Analyzer::RootVar(Expr* x) {
  while (true) {
    if (x->AsVariable() != NULL) {
      return x->AsVariable();
    } else if (x->AsIndex() != NULL) {
      x = x->AsIndex()->var();
    } else if (x->AsSlice() != NULL) {
      x = x->AsSlice()->var();
    } else if (x->AsSelector() != NULL) {
      x = x->AsSelector()->var();
    } else {
      ShouldNotReachHere();
      return NULL;
    }
  }
}


void Analyzer::analyze() {
  CheckAndOptimizeFunctions(remove_unreachable_functions_ &&
                            FLAGS_remove_unreachable_functions);
  SetReferencedFields();
  if (FLAGS_optimize_sawzall_code) {
    PropagateValues();
    RewriteAsserts();
  }
}


}  // namespace sawzall
