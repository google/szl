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

class SymbolTable;

// The analyzer provides support functions for restructuring and additional
// semantic checks on the intermediate representation of a Sawzall program.
// The function in class IR are intended for use during parsing
// and some of them explicitly depend on the parser, while the functions in
// class Analyzer are intended for use in a separate pass between parsing and
// code generation, and do not depend on the parser.

class Analyzer {
 public:
  Analyzer(Proc* proc, SymbolTable* symbol_table,
           bool ignore_undefs, bool remove_unreachable_functions) :
    proc_(proc),
    symbol_table_(symbol_table),
    ignore_undefs_(ignore_undefs),
    remove_unreachable_functions_(remove_unreachable_functions),
    error_count_(0),
    last_error_line_(-1) { }

  void analyze();
  void PropagateValues();
  void CheckAndOptimizeFunctions(bool remove_unreachable_functions);
  void SetReferencedFields();
  void RewriteAsserts();
  Proc* proc() const  { return proc_; }
  SymbolTable* symbol_table() const  { return symbol_table_; }
  bool ignore_undefs() const  { return ignore_undefs_; }

  // Error handling.
  void Error(const FileLine* fileline, const char* fmt, ...);
  void Warning(const FileLine* fileline, const char* fmt, ...);
  void Errorv(const FileLine* fileline, bool is_warning, const char* fmt,
              va_list* args);
  int error_count() const  { return error_count_; }

  // Utility.
  static Variable* RootVar(Expr* x);

 private:
  Proc* proc_;
  SymbolTable* const symbol_table_;
  bool ignore_undefs_;
  bool remove_unreachable_functions_;
  int error_count_;
  int last_error_line_;
};


}  // namespace sawzall
