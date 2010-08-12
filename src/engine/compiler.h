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

// Compilation is the root for all state pertinent to
// a single compilation. It may be used repeatedly
// by calling Compile() for different files.

class Compilation {
 public:
  // Creation
  static Compilation* New(Proc* proc, bool debug);

  // Destruction
  void Finalize();

  // The leave_main_unreturned flag indicates whether to have $main end with
  // terminate rather than return, so that additional main code
  // (e.g. calls) can be executed in the context of main's stack
  // frame.
  void Compile(const char** file, int num_file, bool leave_main_unreturned);
  void CompileStr(const char* name,
                  const char *str,
                  bool leave_main_unreturned);
  void DoCompile(Source* source, bool leave_main_unreturned);

  // For each call to Compile, the following state is setup
  // and alive for the lifetime of the Compilation object:
  SymbolTable* symbol_table()  { return &symbol_table_; }
  Statics* statics()  { return symbol_table_.statics(); }
  Functions* functions()  { return symbol_table_.functions(); }
  Block* program() const  { return symbol_table_.program(); }
  Code* code()  { return code_; }
  size_t statics_size() const { return statics_size_; }
  char* source() const  { return source_; }
  OutputTables* tables() { return &tables_; }
  int error_count() const  { return error_count_; }
  char* source_dir() const  { return source_dir_; }

 private:
  Proc* proc_;
  bool debug_;
  SymbolTable symbol_table_;
  Code* code_;
  size_t statics_size_;
  char* source_;
  OutputTables tables_;
  int error_count_;
  char* source_dir_;

  // Prevent construction from outside the class (must use factory method)
  Compilation(Proc* proc)
    : symbol_table_(proc),
      tables_(proc) {
  }
};

}  // namespace sawzall
