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

class WhenAnalyzer {
 public:
  WhenAnalyzer(Proc* proc, When* when, Function* owner, int level);
  Statement* Analyze();
  const char* error() const { return error_; }

 private:
  Proc* proc_;  // the process to use for allocation
  When* when_;
  Function* owner_;
  int level_;
  const char* error_;
  int namecount_;
  List<QuantVarDecl*>* quants_;

  // helpers
  Scope* qvars() const  { return when_->qvars(); }
  Expr* cond() const  { return when_->cond(); }
  Statement* body() const  { return when_->body(); }
  VarDecl* TempDecl(Scope *scope, szl_string name, Type* type, Expr* init);
  Expr* VarOf(Expr* expr);
  static Type* TypeOfVarOf(Expr* expr);
  static bool AllDefs(Expr* cond);
  bool RangeLimit(Expr* use, Expr*& min, Expr*& max);
  bool CombineRange(Block* block, List<Expr*>* uses,
                    Variable*& min, Variable*& max, Variable** key_arrayp);
  bool CombineArrayRange(Block* block, List<Expr*>* uses,
                    Variable*& min, Variable*& max);
  bool CombineMapRange(Block* block, List<Expr*>* uses,
                    Variable*& min, Variable*& max, Variable** key_arrayp);
  Statement* AnalyzeOneVar(QuantVarDecl* quant_decl, List<Expr*>* uses, bool needs_def);
  Statement* AnalyzeNVars(List<QuantVarDecl*>& quants, List<List<Expr*>*>& alluses);
  Statement* AnalyzeNVarsAllSame(List<QuantVarDecl*>& quants,
                                 List<List<Expr*>*>& alluses,
                                 QuantVarDecl::Kind kind);
  Block* CreateForLoop(FileLine* file_line,
                       QuantVarDecl* quant_decl,
                       List<Expr*>* uses,
                       VarDecl* succeeded_decl,
                       Block* body,
                       Break* break_loop);
  Block* CreateWhileAllLoop(FileLine* file_line,
                            VarDecl* succeeded_decl,
                            Block* loop_body,
                            Break* break_all);
  Assignment* CreateAssignment(FileLine* file_line,
                               Block* block,
                               QuantVarDecl* quant_decl,
                               Expr* map,
                               Variable* key_array,
                               VarDecl* index_decl);
  VarDecl* DeclareIndexVar(FileLine* file_line,
                           Block* block,
                           QuantVarDecl* quant_decl,
                           List<Expr*>* uses,
                           Variable* min,
                           Expr** map_use);
  Expr* ProtectCondition(Expr* cond, Block* block);
  int NumQuant(Expr* expr);
  void set_error(const char* error);
};

}  // namespace sawzall
