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

class Position;

class Parser {
 public:
  Parser(Proc* proc, Source* source, SymbolTable* table);

  // analysis
  void ParseProgram();
  void CheckForInputProtoConversion(Variable* var, TupleType* type);
  VarDecl* CreateTempDecl(FileLine* pos, Type* type);
  TempVariable* CreateTempVar(Expr* src);

  // error handling
  void Errorv(bool is_warning, const char* fmt, va_list* args);
  void Error(const char* fmt, ...);
  void Warning(const char* fmt, ...);
  int error_count() const  { return scanner_.error_count(); }
  void ValidateTableName(szl_string name);

  // source code for logging, available while the parser is alive
  List<char>* source()  { return scanner_.source(); }

  // token scanner; used only by Position
  const Scanner* scanner()  { return &scanner_; }

  // accessors
  Proc* proc() const  { return proc_; }


  // returns a FileLine that spans from the specified starting position
  // to the end of the current token (if the scanner has not advanced)
  // or to the end of the previous token (if the scanner has advanced)
  FileLine* Span(Position* start);

  // The remaining public functions are used exclusively by ProtoDB
  // or SuperParser

  // scope helpers
  Scope* OpenScope();
  void ReopenScope(Scope* scope);
  void CloseScope(Scope* scope);
  Scope* OpenFunctionScope(Function* fun);
  void CloseFunctionScope(Scope* scope);
  // See if entry is in symbol table or proto database - no errors printed.
  Object* ExistingDeclaration(Position* start, szl_string name);
  Scope* OpenMain(Position* start, FileLine* init_fl);
  void CloseMain(Scope* scope, Position* start);

  // symbol table helpers
  void Declare(Object* obj);
  void Declare(Field* x)     { Declare(x->object()); }
  void Declare(VarDecl* x)   { Declare(x->object()); }
  void Declare(TypeName* x)  { Declare(x->object()); }
  void CreateParameters(Function* fun);
  Function* main_function() const { return table_->main_function(); }
  Block* program() const { return table_->program(); }
  void set_main_function(Function* main) { table_->set_main_function(main); }
  void set_program(Block* program) { table_->set_program(program); }
  void ResetTable() { table_->Reset(); }

  // parse helpers
  void Next();
  void Expect(Symbol sym);
  void Verify(Symbol sym);
  void ConsumeOffendingSymbol();
  szl_string ParseIdent();
  TypeDecl* ParseTypeDecl(bool expect_semi);
  Type* ParseType() { return ParseType(NULL, NULL, true /*output_ok*/); }
  Statement* ParseStatement(BreakableStatement* bstat, Loop* loop);
  Proto* ParseProto();

  Symbol sym() { return sym_; }
  const char* PrintSymbol(Symbol sym) { return scanner_.PrintSymbol(sym); };
  Tracer* tlevel() { return &tlevel_; }

  bool IncludeFile(const char* file) { return scanner_.IncludeFile(file); }
  const char* current_file_name() { return scanner_.current_file_name(); }

 private:
  Proc* proc_;
  SymbolTable* table_;
  Scanner scanner_;  // the scanner used for lexical analysis
  Symbol sym_;  // one symbol look-ahead
  Stack<VarDecl*> quants_;  // used to validate when statements
  Tracer tlevel_;  // tracing support (debugging only)
  int nonstatic_var_refs_;  // running total of references to nonstatic vars

  // Scope management
  struct FunctionAndFlag {
    Function* fun;
    int stat_expr_level;
    bool at_static_decl;
  };
  Stack<Scope*> scope_stack_;
  Stack<FunctionAndFlag> function_stack_;
  Stack<StatExpr*> statexpr_stack_;

  Scope* top_scope() const  { return scope_stack_.top(); }
  Function* top_function() const  { return function_stack_.top().fun; }
  int top_level() const {
    const FunctionAndFlag& top = function_stack_.top();
    return top.at_static_decl ? 0 : top.fun->level();
  }
  void SetStaticDeclFlag(bool flag)  {
    function_stack_.mutable_top().at_static_decl = flag;
  }
  bool StaticDeclFlag() const {
    return function_stack_.top().at_static_decl;
  }
  bool InStatExpr() const {
    return statexpr_stack_.length() > function_stack_.top().stat_expr_level;
  }
  bool Reachable(VarDecl* var);
  bool InStaticDecl() const;

  // These functions are a temporary conversion hack.
  // This function does the automatic int<->uint, string<->bytes conversion.
  // warning template must have 2 "%s" for expected type and actual type.
  bool ConvertIfPossible(
    Type *expected_type, Type *actual_type, Position *start, Expr **expr,
    const char* warning_template);
  // The 3 functions below are for the conversions inside proto.
  // Array for repeated fields, Tuple for message/group in proto.
  // Composite does the conversion for all these,
  // for example, repeated message proto which contains repeated fields.
  Expr* ConvertableArray(Position* start, Expr* x, Type *type);
  Expr* ConvertableComposite(Position* start, Expr* x, Type *type);
  Expr* ConvertableTuple(Position* start, Expr* x, Type *type);
  // This function does the conversion for parameters in function call.
  Call* ConvertableCall(Position* start, Expr* x, List<Expr*>* args);
  // End of temporary conversion hack.

  // Look up entry in symbol table, print error if undefined.
  Object* Lookup(Position* start, szl_string name);
  // Parse and look up package qualified identifers.
  Object* ParseAndLookupPackageQualifiedIdent(Position* start, szl_string name);

  szl_string ParsePackageQualifiedIdent(Position* start, szl_string name);
  // note that the scanner must be positioned immediately after "right"
  Expr* CreateBinary(Position* start, Type* type, Expr* left, Binary::Op op,
                     Opcode opcode, Expr* right);
  Expr* Bad(Expr* expr);
  bool IsCompatibleIntrinsicArgList(Intrinsic* fun, List<Expr*>* args);
  bool CompatiblePrintArgs(StringVal* fmt_val, List<Expr*>* args, int argno);
  List<Expr*>* FormatArgs(Symbol which, List<Expr*>* args, Scope* scope);
  Opcode OpcodeFor(Symbol sym, Expr* expr);
  bool CheckSortSig(Intrinsic* fun, List<Expr*>* args) ;
  void MarkLvalue(Expr* expr, bool also_rvalue);
  struct Indexing {
    Expr* array;
    VarDecl* temp;
  };

  // types
  Field* ParseField();
  Field* ParseTupleField();
  TupleType* ParseTuple(TypeName* tname, bool is_message);
  ArrayType* ParseArray(TypeName* tname);
  MapType* ParseMap(TypeName* tname);
  FunctionType* ParseFunctionType(TypeName* tname);
  Type* ParseTypeName(Position* start, szl_string name);
  Type* ParseType(TypeName* tname_def, Type* named_type, bool output_ok);
  Type* ParseOutputType();
  Type* ParseProtoType(TypeName* tname);

  // declarations
  void ParseParameter(FunctionType* ftype);
  VarDecl* ParseDecl(Position* start, szl_string name, bool is_static,
                     bool expect_semi);
  VarDecl* ParseDeclInOutputType();

  // expressions
  List<Expr*>* ParseArgList(bool expect_parens);
  Composite* ParseComposite();
  Expr* ParseNew(Position* start, Intrinsic* fun);
  Expr* ParseConvert(Position* start, Intrinsic* fun);
  Regex* ParseRegex(Position* start, Intrinsic* fun);
  Expr* ParseSaw(Position* start, Intrinsic* fun);
  Function* ParseFunction(Position* start, szl_string name, FunctionType* ftype);
  Expr* ParseOperand(Position* start, szl_string name, Indexing* indexing);
  Expr* ParseSelector(Position* start, Expr* x);
  Object* ParseStaticSelector(Position* start, TypeName* x);
  Expr* ParseIndex(Position* start, Expr* x);
  Expr* ParseCall(Position* start, Expr* x);
  Expr* ParseFactor(Position* start, szl_string name, Indexing* indexing);
  Expr* ParseTerm(Position* start, szl_string name, Indexing* indexing);
  Expr* ParseSimpleExpr(Position* start, szl_string name, Indexing* indexing);
  Expr* ParseComparison(Position* start, szl_string name, Indexing* indexing);
  Expr* ParseConjunction(Position* start, szl_string name, Indexing* indexing);
  Expr* ParseDisjunction(Position* start, szl_string name, Indexing* indexing);
  Expr* ParseExpression(Position* start, szl_string name, Indexing* indexing,
                        Type* hint);
  Expr* ParseExpression(Type* hint);
  Expr* ParseExpression();
  Expr* ParseBoolExpression(Position* start, szl_string name);

  // statements
  Break* ParseBreak(BreakableStatement* bstat);
  Continue* ParseContinue(Loop* loop);
  Emit* ParseEmit();
  When* ParseWhen();
  If* ParseIf(BreakableStatement* bstat, Loop* loop);
  Loop* ParseDo();
  Loop* ParseWhile();
  Loop* ParseFor();
  Result* ParseResult();
  Return* ParseReturn();
  Statement* ParseCaseStatements(BreakableStatement* bstat, Loop* loop);
  Switch* ParseSwitchStatement(Loop* loop);
  Assignment* ParseAssignment(Position* start, Expr* lvalue, bool expect_semi);
  Block* ParseBlock(BreakableStatement* bstat, Loop* loop, bool new_scope);
  Statement* ParseSimpleStatement(bool is_static, bool expect_semi);
  Statement* ParseControlStatementBody(BreakableStatement* bstat, Loop* loop);

  // utility functions
  Expr* GenIncompatibleCallError(FileLine* fl, szl_string message,
                                 Intrinsic* fun, List<Expr*>* args);
};


// Position tracking.
// Captures the start of the most recently returned token
struct Position {
  Position(Parser* p)  {
    const Scanner* s = p->scanner();
    file_name = s->file_name();
    line = s->line();
    offset = s->offset();
  }
  const char* file_name;
  int line;
  int offset;
};

}  // namespace sawzall
