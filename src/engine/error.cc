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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/error.h"
#include "engine/proc.h"
#include "public/sawzall.h"

namespace sawzall {

Error::Error(ErrorHandler* error_handler)
  : count_(0), error_handler_(error_handler) {
}


void Error::Report(Scanner* scanner, bool is_warning, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Reportv(scanner, NULL, is_warning, fmt, &args);
  va_end(args);
}


void Error::Reportv(Scanner* scanner, bool is_warning, const char* fmt,
                    va_list* args) {
  Reportv(scanner, NULL, is_warning, fmt, args);
}


void Error::Reportv(const FileLine* fileline, bool is_warning, const char* fmt,
                    va_list* args) {
  Reportv(NULL, fileline, is_warning, fmt, args);
}


void Error::Reportv(const Scanner* scanner, const FileLine* fileline,
                    bool is_warning, const char* fmt, va_list* args) {
  Fmt::State f;
  if (!is_warning)
    count_++;
  if (error_handler_ != NULL) {
    const char* file_name = NULL;
    int line = 0;
    int offset = 0;
    if (scanner != NULL) {
      file_name = scanner->file_name();
      line = scanner->line();
      offset = scanner->offset();
    } else if (fileline != NULL) {
      file_name = fileline->file();
      line = fileline->line();
      offset = fileline->offset();
    }
    F.fmtstrinit(&f);
    F.fmtvprint(&f, fmt, args);
    char* message = F.fmtstrflush(&f);
    error_handler_->Report(file_name, line, offset, is_warning, message);
    free(message);
  } else {
    char buf[128];
    F.fmtfdinit(&f, 2, buf, sizeof buf);
    if (scanner != NULL)
      F.fmtprint(&f, "%H ", scanner);
    else if (fileline != NULL)
      F.fmtprint(&f, "%L: ", fileline);
    if (is_warning)
      F.fmtprint(&f, "warning: ");
    F.fmtvprint(&f, fmt, args);
    F.fmtprint(&f, "\n");
    F.fmtfdflush(&f);
  }
}

}
