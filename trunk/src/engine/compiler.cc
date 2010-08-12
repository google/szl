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
#include <time.h>
#include <stdio.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/tracer.h"
#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/code.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/parser.h"
#include "engine/analyzer.h"
#include "public/sawzall.h"
#include "engine/codegen.h"
#include "engine/assembler.h"
#include "engine/regsstate.h"
#include "engine/nativecodegen.h"
#include "engine/compiler.h"
#include "engine/proc.h"
#include "public/emitterinterface.h"
#include "engine/outputter.h"


namespace sawzall {


Compilation* Compilation::New(Proc* proc, bool debug) {
  Compilation* c = NEWP(proc, Compilation);
  c->proc_ = proc;
  c->debug_ = debug;
  c->code_ = NULL;
  c->statics_size_ = 0;
  c->source_ = NULL;
  c->error_count_ = 0;
  return c;
}


void Compilation::Finalize() {
  delete[] source_;
  if (code_ != NULL)
    code_->Cleanup();
}


void Compilation::DoCompile(Source* source, bool leave_main_unreturned) {
  symbol_table_.Reset();
  Parser parser(proc_, source, symbol_table());
  parser.ParseProgram();
  error_count_ = parser.error_count() + source->error_count();
  if (error_count_ == 0) {
    // print program graph to source before transforming it
    if ((proc_->mode() & kPrintSource) != 0)
      F.print("%N", program());
    // run analysis pass
    Analyzer analyzer(proc_, symbol_table(),
                      (proc_->mode() & Proc::kIgnoreUndefs) != 0,
                      (proc_->mode() & Proc::kDoCalls) == 0);
    analyzer.analyze();
    error_count_ += analyzer.error_count();
  }
  code_ = NULL;

  // copy the raw source even if errors (for codegen messages and logging)
  source_ = CharList2CStr(parser.source());

  if (error_count_ == 0) {
    // for all non-predefined tuples, bind fields to slots & allocate defaults
    TupleType::BindFieldsToSlotsForAll(proc_);
    // allocate local variables
    // (functions may refer to intermediate variables
    // outside their scope, so the variables of all functions
    // must have been allocated before code generation)
    Functions* flist = functions();
    List<CodeDesc*>* descs = List<CodeDesc*>::New(proc_);

    if ((proc_->mode() & kNative) == 0) {
      // compile to byte code

      for (int i = 0; i < flist->length(); i++)
        CodeGen::AllocateFrameOffsets(flist->at(i));

      // allocate statics
      statics_size_ = CodeGen::AllocateStaticOffsets(symbol_table());

      // generate code
      CodeGen codegen(proc_, source_, debug_);  // byte code generator

      // functions
      { Functions* f = functions();
        for (int i = 0; i < f->length(); i++) {
          Function* fun = f->at(i);
          int begin = codegen.emit_offset();
          bool leave_unreturned =
              leave_main_unreturned &&
              fun->name() != NULL && strcmp(fun->name(), "$main") == 0;
          codegen.GenerateFunction(statics(), fun, leave_unreturned);
          error_count_ += codegen.error_count();
          if (error_count_ > 0)
            return;  // terminate if code generation failed
          int end = codegen.emit_offset();
          descs->Append(CodeDesc::New(proc_, descs->length(), fun, begin, end, 0));
        }
      }

      // initialization code
      { int begin =  codegen.emit_offset();
        codegen.GenerateInitializers(symbol_table(), tables(), statics_size_);
        error_count_ += codegen.error_count();
        if (error_count_ > 0)
          return;  // terminate if code generation failed
        int end = codegen.emit_offset();
        descs->Append(CodeDesc::New(proc_, descs->length(), NULL, begin, end, 0));
      }

      // allocate code
      // (this copies the code from codegen)
      code_ = Code::New(proc_, codegen.code_buffer(), descs,
                        codegen.trap_ranges(), codegen.line_num_info());
      // codegen and its internal code buffer are destroyed here

    } else {
      // compile to native code

      CHECK(!leave_main_unreturned)
          << "don't support leaving main unreturned in native mode yet";

      for (int i = 0; i < flist->length(); i++)
        NCodeGen::AllocateFrameOffsets(proc_, flist->at(i));

      // allocate statics
      statics_size_ = CodeGen::AllocateStaticOffsets(symbol_table());

      // generate code
      NCodeGen ncodegen(proc_, source_, debug_);  // Intel native code generator

      // native code stubs
      int begin = ncodegen.emit_offset();
      int line_begin = ncodegen.line_num_info()->length();
      ncodegen.GenerateTrapHandlerStubs();
      error_count_ += ncodegen.error_count();
      if (error_count_ > 0)
        return;  // terminate if code generation failed
      int end = ncodegen.emit_offset();
      descs->Append(CodeDesc::New(proc_, descs->length(), NULL, begin, end, line_begin));

      // functions
      { Functions* f = functions();
        for (int i = 0; i < f->length(); i++) {
          Function* fun = f->at(i);
          int begin = ncodegen.emit_offset();
          int line_begin = ncodegen.line_num_info()->length();
          ncodegen.GenerateFunction(statics(), fun);
          error_count_ += ncodegen.error_count();
          if (error_count_ > 0)
            return;  // terminate if code generation failed
          int end = ncodegen.emit_offset();
          descs->Append(CodeDesc::New(proc_, descs->length(), fun, begin, end, line_begin));
        }
      }

      // initialization code
      { int begin = ncodegen.emit_offset();
        int line_begin = ncodegen.line_num_info()->length();
        ncodegen.GenerateInitializers(symbol_table(), tables(), statics_size_);
        error_count_ += ncodegen.error_count();
        if (error_count_ > 0)
          return;  // terminate if code generation failed
        int end = ncodegen.emit_offset();
        descs->Append(CodeDesc::New(proc_, descs->length(), NULL, begin, end, line_begin));
      }

      // allocate code
      // (this copies the code from ncodegen)
      code_ = Code::New(proc_, ncodegen.code_buffer(), descs,
                        ncodegen.trap_ranges(), ncodegen.line_num_info());
      // ncodegen and its internal code buffer are destroyed here
    }
  }
}


void Compilation::Compile(const char** file, int num_file,
                          bool leave_main_unreturned) {
  SourceFile* files = new SourceFile[num_file];  // explicitly deallocated
  for (int i = 0; i < num_file; i++){
    files[i].path = Scanner::FindIncludeFile(proc_, proc_->CopyString(file[i]),
                                             NULL);
    if (files[i].path == NULL)
      // FindIncludeFile will return NULL if the file does not exist.  Set
      // the file name to the bad path, and the scanner will report the error.
      files[i].path = proc_->CopyString(file[i]);
    files[i].dir = FileDir(proc_, files[i].path);
  }
  Source source(files, num_file, NULL);
  DoCompile(&source, leave_main_unreturned);
  delete[] files;
}


void Compilation::CompileStr(const char* name, const char* str,
                             bool leave_main_unreturned) {
  SourceFile file = { proc_->CopyString(name), FileDir(proc_, name) };
  Source source(&file, 1, str);
  DoCompile(&source, leave_main_unreturned);
}

}  // namespace sawzall
