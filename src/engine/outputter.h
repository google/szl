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

#include <vector>

namespace sawzall {

class EmitFile;  // The object returned by OpenFile

// An outputter is an interface object for emitting Sawzall output.
//
// Note: Some data associated with an Outputter object survives
// a single Sawzall run and must not be allocated on the corresponding
// Process heap since it is reset after each run!

class Outputter {
 public:
  // create a new output variable specific emitter
  Outputter(Proc* proc, TableInfo* table);
  ~Outputter();

  OutputType* type() const  { return table_->type(); }
  const char* name() const  { return table_->name(); }

  // emit value on top of stack
  // returns message in case of an error, returns NULL otherwise
  const char* Emit(Val**& sp);

  // emitter interface support
  Emitter* emitter() const { return emitter_; }
  void set_emitter(Emitter* emitter)  { emitter_ = emitter; }

  // profiling support
  int emit_count() const  { return emit_count_; }
  void reset_emit_count()  { emit_count_ = 0; }

  TableInfo* table() const { return table_; }

 private:
  Proc* proc_;

  // table information
  TableInfo* table_;

  // backend connection
  Emitter* emitter_;

  // profiling support
  int emit_count_;

  // error handling
  const char* error_msg_;

  // file I/O
  // (note: open_files_ remains alive accross several Sawzall runs - do not
  // allocate on proc_ heap!)
  vector<EmitFile*> open_files_;

  // helpers
  void Error(const char* error_msg);
  EmitFile* OpenFile(char* str, int len, bool is_proc);
  void PutValue(Type* type, Emitter* emitter, Val**& sp, bool on_stack);
};

}  // namespace sawzall
