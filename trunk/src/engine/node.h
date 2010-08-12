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

class Type;
class Expr;
class Statement;
class Object;


// FileLine describes the source location corresponding
// to a particular node. It is used for error and profile reporting.
class FileLine {
 public:
  // Creation
  static FileLine* New(Proc* proc, const char* file, int line, int offset,
                       int length);

  // Accessors
  const char* file() const  { return file_; }
  int line() const  { return line_; }
  int offset() const  { return offset_; }
  int length() const  { return length_; }

 private:
  const char* file_;
  int line_;
  int offset_;  // offset in raw source buffer
  int length_;  // length in the raw source buffer

  // Prevent construction from outside the class (must use factory method)
  FileLine()  { /* nothing to do */ }
};


// To hold start & length of raw source buffer for use in code generation.
struct RawSource {
  explicit RawSource(const char* str) : start(str), length(strlen(str))  { }
  const char* start;
  int length;
};


// CodeRange describes the code interval [beg, end[ generated for
// a particular node. beg and end are byte offsets relative to the
// code base.
struct CodeRange {
  int beg;
  int end;

  CodeRange()
    : beg(0),
      end(0) {
  }

  bool contains(int pos) const {
    return beg <= pos && pos < end;
  }
};


// Labels represent branch and call targets during code generation.
// Each code generator provides a concrete subclass for the target architecture.

class Label {
 public:
  // testers
  virtual bool is_bound() const = 0;
  virtual bool is_linked() const = 0;
  virtual ~Label()  {}
};


// Forward declarations
class NodeVisitor;


// The Sawzall compiler creates an intermediate representation
// made up of specialized nodes corresponding to individual Sawzall
// language constructs. Thus, the Node class is the super class for
// all specialized nodes.
class Node {
 public:
  // expressions
  virtual Expr* AsExpr()  { return NULL; }
  virtual BadExpr* AsBadExpr()  { return NULL; }
  virtual Binary* AsBinary()  { return NULL; }
  virtual Call* AsCall()  { return NULL; }
  virtual Composite* AsComposite()  { return NULL; }
  virtual Conversion* AsConversion()  { return NULL; }
  virtual Dollar* AsDollar()  { return NULL; }
  virtual Function* AsFunction()  { return NULL; }
  virtual RuntimeGuard* AsRuntimeGuard()  { return NULL; }
  virtual Index* AsIndex()  { return NULL; }
  virtual New* AsNew()  { return NULL; }
  virtual Regex* AsRegex()  { return NULL; }
  virtual Saw* AsSaw()  { return NULL; }
  virtual Selector* AsSelector()  { return NULL; }
  virtual Slice* AsSlice()  { return NULL; }
  virtual StatExpr* AsStatExpr()  { return NULL; }
  virtual Field* AsField()  { return NULL; }
  virtual Intrinsic* AsIntrinsic()  { return NULL; }
  virtual Literal* AsLiteral()  { return NULL; }
  virtual TypeName* AsTypeName()  { return NULL; }
  virtual Variable* AsVariable()  { return NULL; }
  virtual TempVariable* AsTempVariable()  { return NULL; }

  // statements
  virtual Statement* AsStatement()  { return NULL; }
  virtual BreakableStatement* AsBreakableStatement()  { return NULL; }
  virtual Assignment* AsAssignment()  { return NULL; }
  virtual Block* AsBlock()  { return NULL; }
  virtual Proto* AsProto()  { return NULL; }
  virtual Break* AsBreak()  { return NULL; }
  virtual Continue* AsContinue()  { return NULL; }
  virtual Result* AsResult()  { return NULL; }
  virtual TypeDecl* AsTypeDecl()  { return NULL; }
  virtual VarDecl* AsVarDecl()  { return NULL; }
  virtual QuantVarDecl* AsQuantVarDecl()  { return NULL; }
  virtual Emit* AsEmit()  { return NULL; }
  virtual Empty* AsEmpty()  { return NULL; }
  virtual ExprStat* AsExprStat()  { return NULL; }
  virtual If* AsIf()  { return NULL; }
  virtual Increment* AsIncrement()  { return NULL; }
  virtual Return* AsReturn()  { return NULL; }
  virtual Switch* AsSwitch()  { return NULL; }
  virtual When* AsWhen()  { return NULL; }
  virtual Loop* AsLoop()  { return NULL; }

  // nodes that have Objects return them
  virtual Object* object() { return NULL; }

  // support for literals
  // (these should be const, but declaring them as such causes
  // all kinds of other problems with const-ness... - sigh)
  BytesVal* as_bytes();
  BoolVal* as_bool();
  FingerprintVal* as_fingerprint();
  FloatVal* as_float();
  IntVal* as_int();
  StringVal* as_string();
  TimeVal* as_time();
  UIntVal* as_uint();

  // Code generation utilities.
  // Whether evaluating this node may call a szl function or call the GC.
  // TODO: finish implementation so can default to false.
  virtual bool CanCall(bool is_lhs) const  { return true; }
  // Whether executing this node, including any children, can cause a trap
  // that is not handled within the execution of the node itself.
  // Should only be called on expressions and a few kinds of statements.
  virtual bool CanTrap();

  // visitors
  // (note that Visit() uses covariant return types)
  virtual Node* Visit(NodeVisitor* v) = 0;
  virtual void VisitChildren(NodeVisitor* v)  { }

  // formatting
  static int NodeFmt(Fmt::State* f);  // implements %n

  // fileline
  FileLine* file_line() const  { return fileline_; }
  void set_file_line(FileLine* fl)  { fileline_ = fl; }
  const char* file() const  { return fileline_->file(); }
  const int line() const  { return fileline_->line(); }
  FileLine* clone_fl(CloneMap* cmap) const;

  // code range
  void set_code_range(int beg, int end) {
    assert(beg <= end);
    code_range_.beg = beg;
    code_range_.end = end;
  }
  const CodeRange* code_range() const  { return &code_range_; }

  // The line counting histogram.  Some day we may need UnSetLineCounter()
  void SetLineCounter()  { line_counter_ = true; }
  bool line_counter()  { return line_counter_; }

  virtual ~Node()  {}

 protected:
  // helpers for the VisitChildren() methods
  static void VUpdate(NodeVisitor* v, Expr** e);
  static void VUpdate(NodeVisitor* v, Statement** s);
  static void VUpdate(NodeVisitor* v, VarDecl** d);
  static void VUpdate(NodeVisitor* v, Block** s);
  static void VUpdate(NodeVisitor* v, Field** f);
  static void VUpdate(NodeVisitor* v, TypeName** t);
  static void VUpdate(NodeVisitor* v, List<Expr*>* list);
  static void VUpdate(NodeVisitor* v, List<Statement*>* list);
  static void VUpdate(NodeVisitor* v, List<VarDecl*>* list);

  void Initialize(FileLine* fileline) {
    fileline_ = fileline;
  }

  // Prevent construction from outside the class (must use factory method)
  Node() : line_counter_(false)  { }

 private:
  friend class Case;  // for access to VUpdate()
  FileLine* fileline_;
  CodeRange code_range_;
  bool line_counter_;  // generate a counter for this Node
};


class Expr: public Node {
 public:
  enum DefState { kDefined, kUndefined, kDefnessUnknown };

  virtual Type* type() const = 0;

  // Visitor
  // (Note that the return type is Expr*, not Node*.)
  virtual Expr* Visit(NodeVisitor* v) = 0;

  // Conversion and cloning
  virtual Expr* AsExpr()  { return this; }
  virtual Expr* Clone(CloneMap* cmap) = 0;

  // Code generation utilities.
  // Whether this operation itself (after the evaluation of any operands)
  // can generate a trap.
  virtual bool CanCauseTrap(bool is_lvalue) const = 0;

 protected:
  void Initialize(FileLine* fileline) {
    Node::Initialize(fileline);
  }

  // Prevent construction from outside the class (must use factory method)
  Expr()  { /* nothing to do */ }
};


class Slice: public Expr {
 public:
  // Creation
  static Slice* New(Proc* proc, FileLine* fileline, Expr* var, Expr* beg,
                    Expr* end, Variable* length_temp);

  Expr* var() const  { return var_; }
  Expr* beg() const  { return beg_; }
  Expr* end() const  { return end_; }
  Variable* length_temp() const  { return length_temp_; }

  virtual Type* type() const  { return var_->type(); }
  virtual Slice* AsSlice()  { return this; }
  virtual Slice* Clone(CloneMap* cmap);

  // Code generation utilities.
  virtual bool CanCauseTrap(bool is_lvalue) const  { return is_lvalue; }

  // visitors
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitVar(NodeVisitor* v)  { VUpdate(v, &var_); }
  void VisitBeg(NodeVisitor* v)  { VUpdate(v, &beg_); }
  void VisitEnd(NodeVisitor* v)  { VUpdate(v, &end_); }

 private:
  Expr* var_;
  Expr* beg_;
  Expr* end_;
  Variable* length_temp_;

  // Prevent construction from outside the class (must use factory method)
  Slice()  { /* nothing to do */ }
};


class Binary: public Expr {
 public:
  enum Op {
    // arithmetic
    ADD, SUB, MUL, DIV, MOD,

    // comparison
    EQL, NEQ, LSS, LEQ, GTR, GEQ,

    // bit manipulation
    BAND, BOR, BXOR, SHL, SHR,

    // logical
    LAND, LOR, AND, OR
  };

  // Creation
  static Binary* New(Proc* proc, FileLine* fileline,
                     Type* type, Expr* left, Op op, Opcode opcode, Expr* right);

  virtual Type* type() const  { return type_; }
  Op op() const  { return op_; }
  Opcode opcode() const  { return opcode_; }
  Expr* left() const  { return left_; }
  Expr* right() const  { return right_; }
  static const char* Op2String(Op op);

  // Conversion and cloning
  virtual Binary* AsBinary()  { return this; }
  virtual Binary* Clone(CloneMap* cmap);

  // Code generation utilities
  virtual bool CanCall(bool is_lhs) const;
  virtual bool CanCauseTrap(bool is_lvalue) const;

  // visitors
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitLeft(NodeVisitor* v)  { VUpdate(v, &left_); }
  void VisitRight(NodeVisitor* v)  { VUpdate(v, &right_); }

 private:
  Type* type_;
  Expr* left_;
  Op op_;
  Opcode opcode_;
  Expr* right_;

  // Prevent construction from outside the class (must use factory method)
  Binary()  { /* nothing to do */ }
};


// This class describes scoped objects.  Not all scoped objects are expressions
// so it is not a subclass of Expr.  Conceptually a class like Intrinsic is
// both an Expr and an Object, but rather than use multiple inheritance we
// embed an Object member where needed and add forwarding functions.
// This is mostly transparent except in Scope::Insert and Parser::Declare
// which use overloading to simulate implicit upcasting to Object.

class Object {
 private:
  explicit Object(Node* node) : node_(node), scope_(NULL),
    doc_("no documentation available")  { }
  void Initialize(szl_string name)  { name_ = name; }
  friend class BadExpr;
  friend class Field;
  friend class Intrinsic;
  friend class Literal;
  friend class TypeName;
  friend class VarDecl;

 public:
  Node* node() const { return node_; }
  szl_string name() const  { return name_; }
  szl_string display_name() const {
    return is_anonymous() ? "<anonymous>" : name_;
  }
  bool has_name() const  { return name_ != NULL; }
  const char* doc() const  { return doc_; }
  bool is_anonymous() const  { return name_ == NULL; }
  bool is_internal() const  { return !is_anonymous() && name_[0] == '$'; }
  Scope* scope() const  { return scope_; }
  void set_scope(Scope* scope)  { scope_ = scope; }
  void set_doc(const char* doc)  { doc_ = doc; }

  Type* type() const;

  BadExpr* AsBadExpr()  { return node_->AsBadExpr(); }
  Field* AsField()  { return node_->AsField(); }
  Intrinsic* AsIntrinsic()  { return node_->AsIntrinsic(); }
  Literal* AsLiteral()  { return node_->AsLiteral(); }
  TypeName* AsTypeName()  { return node_->AsTypeName(); }
  VarDecl* AsVarDecl()  { return node_->AsVarDecl(); }
  QuantVarDecl* AsQuantVarDecl()  { return node_->AsQuantVarDecl(); }

 private:
  Node* node_;
  szl_string name_;
  Scope* scope_;
  const char* doc_;
};


class BadExpr: public Expr {
 public:
  // Creation
  static BadExpr* New(Proc* proc, FileLine* fileline, Node* node);
  virtual Object* object()  { return &object_; }

  virtual Type* type() const;
  virtual BadExpr* AsBadExpr()  { return this; }
  virtual BadExpr* Clone(CloneMap* cmap);
  virtual bool CanCauseTrap(bool is_lvalue) const {
    ShouldNotReachHere();
    return false;
  }

  Node* node() const  { return node_; }

  // visitor
  virtual Expr* Visit(NodeVisitor* v);

 private:
  Object object_;
  Node* node_;

  // Prevent construction from outside the class (must use factory method)
  BadExpr() : object_(this)  { /* nothing to do */ }
};


class Literal: public Expr {
 public:
  // Creation
  static Literal* NewBool(Proc* proc, FileLine* fileline, szl_string name, bool val);
  static Literal* NewBytes(Proc* proc, FileLine* fileline, szl_string name, int len, const char* val);
  static Literal* NewFingerprint(Proc* proc, FileLine* fileline, szl_string name, szl_fingerprint val);
  static Literal* NewFloat(Proc* proc, FileLine* fileline, szl_string name, szl_float val);
  static Literal* NewInt(Proc* proc, FileLine* fileline, szl_string name, szl_int val);
  static Literal* NewString(Proc* proc, FileLine* fileline, szl_string name, szl_string val);
  static Literal* NewTime(Proc* proc, FileLine* fileline, szl_string name, szl_time val);
  static Literal* NewUInt(Proc* proc, FileLine* fileline, szl_string name, szl_uint val);

  // accessors
  virtual Object* object()  { return &object_; }
  szl_string name() const  { return object_.name(); }
  bool is_anonymous() const  { return object_.is_anonymous(); }
  virtual Type* type() const;
  Val* val() const  { return val_; }

  // Conversion and cloning (literals are not cloned)
  virtual Literal* AsLiteral()  { return this; }
  virtual Literal* Clone(CloneMap* cmap) { return this; }

  // Code generation utilities.
  virtual bool CanCauseTrap(bool is_lvalue) const { return false; }

  // visitor
  virtual Expr* Visit(NodeVisitor* v);

 private:
  static Literal* New(Proc* proc, FileLine* fileline, szl_string name, Val* val);
  friend class ConstantFoldingVisitor;

  Object object_;
  Val* val_;

  // Prevent construction from outside the class (must use factory method)
  Literal() : object_(this)  { /* nothing to do */ }
};


class Conversion: public Expr {
 public:
  enum Kind { kBasicConv, kArrayToArrayConv, kArrayToMapConv };
   // Creation
  static Conversion* New(Proc* proc, FileLine* fileline, Type* type, Expr* src,
         List<Expr*>* params, int source_param_count, Kind kind,
         ConversionOp op, ConversionOp key_op);

  // Accessors
  // When converting to maps, op() converts value, key_op() converts index/key.
  // Otherwise, op() is conversion and key_op() is ignored.
  Expr* src() const  { return src_; }
  // note that the caller keeps a pointer to the params list and uses it
  // to append optional params to the list even though our copy is const
  const List<Expr*>* params() const  { return params_; }
  int source_param_count() const  { return source_param_count_; }
  Kind kind() const  { return kind_; }
  ConversionOp op() const  { return op_; }
  ConversionOp key_op() const  { return key_op_; }

  virtual Type* type() const  { return type_; }

  // Conversion and cloning
  virtual Conversion* AsConversion()  { return this; }
  virtual Conversion* Clone(CloneMap* cmap);

  // Code generation utilities.
  virtual bool CanCauseTrap(bool is_lvalue) const;

  // visitors
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitSrc(NodeVisitor* v)  { VUpdate(v, &src_); }
  void VisitParams(NodeVisitor* v) {  VUpdate(v, params_); }

 private:
  Type* type_;
  Expr* src_;
  List<Expr*>* params_;
  int source_param_count_;
  Kind kind_;
  ConversionOp op_;
  ConversionOp key_op_;

  // Prevent construction from outside the class (must use factory method)
  Conversion()  { /* nothing to do */ }
};


class New: public Expr {
 public:
  // Creation
  static New* New_(Proc* proc, FileLine* fileline, Type* type, Expr* length, Expr* init);

  // Accessors
  virtual Type* type() const  { return type_; }
  Expr* length() const  { return length_; }
  Expr* init() const  { return init_; }

  // Conversion and cloning
  virtual New* AsNew()  { return this; }
  virtual New* Clone(CloneMap* cmap);

  // Code generation utilities.
  virtual bool CanCauseTrap(bool is_lvalue) const  { return true; }

  // visitors
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitLength(NodeVisitor* v)  { VUpdate(v, &length_); }
  void VisitInit(NodeVisitor* v)  { VUpdate(v, &init_); }

 private:
  Type* type_;
  Expr* length_;
  Expr* init_;

  // Prevent construction from outside the class (must use factory method)
  New()  { /* nothing to do */ }
};


class Regex: public Expr {
 public:
  // Creation
  static Regex* New(Proc* proc, FileLine* fileline, Type* type, Expr* base);

  // Accessors
  virtual Type* type() const;
  Type* arg() const  { return arg_; }
  Expr* base() const { return base_; }

  // Conversion and cloning
  virtual Regex* AsRegex()  { return this; }
  virtual Regex* Clone(CloneMap* cmap);

  // Code generation utilities.
  virtual bool CanCauseTrap(bool is_lvalue) const  { return false; }

  // visitors
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitBase(NodeVisitor* v)  { VUpdate(v, &base_); }

 private:
  Type* arg_;
  Expr* base_;

  // Prevent construction from outside the class (must use factory method)
  Regex()  { /* nothing to do */ }
};


class Saw: public Expr {
 public:
  enum Kind { ILLEGAL, SAW, SAWN, SAWZALL };
  enum Flag { NONE, SKIP, REST, SUBMATCH };

  // Creation
  static Saw* New(Proc* proc, FileLine* fileline, Kind kind, Expr* count, Expr* str, bool static_args, List<Expr*>* args, List<Flag>* flags);

  // Accessors
  Kind kind() const  { return kind_; }
  Expr* count() const  { return count_; }
  Expr* str() const  { return str_; }
  bool static_args() const  { return static_args_; }
  List<Expr*>* args() const  { return args_; }
  List<Flag>* flags() const  { return flags_; }
  virtual Type* type() const;

  // Conversion and cloning
  virtual Saw* AsSaw()  { return this; }
  virtual Saw* Clone(CloneMap* cmap);

  // Code generation utilities.
  virtual bool CanCauseTrap(bool is_lvalue) const  { return true; }

  // printing support
  static const char* Kind2String(Kind kind);
  static const char* Flag2String(Flag flag);

  // visitors
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitCount(NodeVisitor* v)  { VUpdate(v, &count_); }
  void VisitStr(NodeVisitor* v)  { VUpdate(v, &str_); }
  void VisitArgs(NodeVisitor* v)  { VUpdate(v, args_); }
  void VisitArg(NodeVisitor* v, int i)  { VUpdate(v, &args_->at(i)); }

 private:
  Kind kind_;
  Expr* count_;
  Expr* str_;
  bool static_args_;
  List<Expr*>* args_;
  List<Flag>* flags_;

  // Prevent construction from outside the class (must use factory method)
  Saw()  { /* nothing to do */ }
};


class Composite: public Expr {
 public:
  // Creation
  static Composite* New(Proc* proc, FileLine* fileline);

  void Append(Expr* x);

  Expr* at(int i) const;
  int length() const;
  bool is_empty() const  { return list_.is_empty(); }
  List<Expr*>* list()  { return &list_; }
  virtual Type* type() const  { return type_; }
  bool has_pairs() const  { return has_pairs_; }
  bool has_conversion() const  { return has_conversion_; }
  void set_type(Type* type);
  void set_has_pairs(bool has_pairs);
  void set_has_conversion(bool has_conversion) {
    has_conversion_ = has_conversion;
  }

  // Conversion and cloning
  virtual Composite* AsComposite()  { return this; }
  virtual Composite* Clone(CloneMap* cmap);

  // Code generation utilities.
  // Only building strings from composites can fail, due to out-of-range
  // character code point values.
  // (Conversion of individual fields is handled in child nodes and affects
  // CanTrap, but not CanCauseTrap.)
  virtual bool CanCauseTrap(bool is_lvalue) const { return type_->is_string(); }

  // visitors
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitList(NodeVisitor* v)  { VUpdate(v, &list_); }

 private:
  List<Expr*> list_;
  Type* type_;
  bool has_pairs_;
  bool has_conversion_;  // whether this was created by an explicit conversion

  // Prevent construction from outside the class (must use factory method)
  explicit Composite(Proc* proc) : list_(proc)  { }

  // C++ compiler complains if there's no virtual destructor
  // however, no destructor is ever called...
  virtual ~Composite()  { ShouldNotReachHere(); }
};


class StatExpr: public Expr {
 public:
  // Creation
  static StatExpr* New(Proc* proc, FileLine* fileline);

  void set_exit(Label* exit)  { exit_ = exit; }
  Label* exit()  const  { return exit_; }

  Statement* body() const  { return body_; }
  void set_body(Statement* body);
  virtual Type* type() const  { return type_; }
  void set_type(Type* type);

  // The tempvar is used to store the result of the expression.
  // It is never explicitly code-gen'ed.  Instead, result statements
  // assign to the variable.
  virtual TempVariable* tempvar() const  { return tempvar_; }
  void set_tempvar(TempVariable* tempvar);
  virtual Variable* var() const  { return var_; }
  void set_var(Variable* var);
  // Support for value propagation. Avoid reevaluation under recurrence.
  bool analysis_started() const  { return analysis_started_; }
  void set_analysis_started()  { analysis_started_ = true; }

  // Conversion and cloning
  virtual StatExpr* AsStatExpr()  { return this; }
  virtual StatExpr* Clone(CloneMap* cmap);

  // Code generation utilities.
  // TODO: refine this?
  virtual bool CanCauseTrap(bool is_lvalue) const  { return true; }

  // visitors
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitBody(NodeVisitor* v)  { VUpdate(v, &body_); }

 private:
  Type* type_;
  Statement* body_;
  TempVariable* tempvar_;
  Variable* var_;
  Label* exit_;
  bool analysis_started_;
  // Prevent construction from outside the class (must use factory method)
  explicit StatExpr(Proc* proc)  { /* nothing to do */ }

  // C++ compiler complains if there's no virtual destructor
  // however, no destructor is ever called...
  virtual ~StatExpr()  { ShouldNotReachHere(); }
};


class Statement: public Node {
 public:
  // Conversion and cloning
  virtual Statement* AsStatement()  { return this; }
  // Non-declaration statements are always cloned.
  // Declaration statements are only cloned when referenced using
  // a static type of "Statement*", which only happens at the declaration
  // itself, not at references to the declaration (e.g. from Variable).
  // So Statement::Clone is nonvirtual and calls CloneStmt.
  // All subclasses of Statement declare CloneStmt to be virtual.
  // The declaration subclasses define Clone() and calls to these methods
  // are considered to be for references, not declarations.
  // (Since Clone() is inherited, it must be redeclared whenever its
  // return type needs to be the class type.)
  Statement* Clone(CloneMap* cmap)  { return CloneStmt(cmap); }
  virtual Statement* CloneStmt(CloneMap* cmap) = 0;

  // Visitor
  // (Note that the return type is Statement*, not Node*.)
  virtual Statement* Visit(NodeVisitor* v) = 0;

 protected:
  void Initialize(FileLine* fileline)  { Node::Initialize(fileline); }

  // Prevent construction from outside the class (must use factory method)
  Statement()  { /* nothing to do */ }
};


class BreakableStatement: public Statement {
 public:
  void set_exit(Label* exit)  { exit_ = exit; }
  Label* exit()  const  { return exit_; }

  // Conversion
  virtual BreakableStatement* AsBreakableStatement()  { return this; }

 protected:
  void Initialize(Proc* proc, FileLine* fileline);

  // Prevent construction from outside the class (must use factory method)
  BreakableStatement()  { /* nothing to do */ }

 private:
  Label* exit_;
};


class Empty: public Statement {
 public:
  // Creation
  static Empty* New(Proc* proc, FileLine* fileline);

  // Conversion and cloning
  virtual Empty* AsEmpty()  { return this; }
  virtual Empty* CloneStmt(CloneMap* cmap);

  // visitor
  virtual Statement* Visit(NodeVisitor* v);

  // Prevent construction from outside the class (must use factory method)
  Empty()  { /* nothing to do */ }
};


class ExprStat: public Statement {
 public:
  // Creation
  static ExprStat* New(Proc* proc, FileLine* fileline, Expr* expr);

  // Accessors
  Expr* expr() const  { return expr_; }

  // Conversion and cloning
  virtual ExprStat* AsExprStat()  { return this; }
  virtual ExprStat* CloneStmt(CloneMap* cmap);

  // visitors
  virtual Statement* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitExpr(NodeVisitor* v)  { VUpdate(v, &expr_); }

 private:
  Expr* expr_;

  // Prevent construction from outside the class (must use factory method)
  ExprStat()  { /* nothing to do */ }
};


class When: public Statement {
 public:
   // Creation
  static When* New(Proc* proc, FileLine* fileline, Scope* qvars, Expr* cond, Statement* body);

  // Accessors
  Scope* qvars() const  { return qvars_; }
  Expr* cond() const  { return cond_; }
  Statement* body() const  { return body_; }

  Statement* rewritten() const  { return rewritten_; }
  void rewrite(Proc* proc, Function* owner, int level);
  char* error() const  { return error_; }

  // Conversion and cloning
  virtual When* AsWhen()  { return this; }
  virtual When* CloneStmt(CloneMap* cmap);

  // visitors
  virtual Statement* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitCond(NodeVisitor* v)  { VUpdate(v, &cond_); }
  void VisitRewritten(NodeVisitor* v)  { VUpdate(v, &rewritten_); }

 private:
  Scope* qvars_;
  Expr* cond_;
  Statement* body_;
  Statement* rewritten_;
  char* error_;

  // Prevent construction from outside the class (must use factory method)
  When()  { /* nothing to do */ }
};


class If: public Statement {
 public:
  // Creation
  static If* New(Proc* proc, FileLine* fileline, Expr* cond, Statement* then_part, Statement* else_part);

  // Accessors
  Expr* cond() const  { return cond_; }
  Statement* then_part() const  { return then_part_; }
  Statement* else_part() const  { return else_part_; }

  // Conversion and cloning
  virtual If* AsIf()  { return this; }
  virtual If* CloneStmt(CloneMap* cmap);

  // visitors
  virtual Statement* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitCond(NodeVisitor* v)  { VUpdate(v, &cond_); }
  void VisitThen(NodeVisitor* v)  { VUpdate(v, &then_part_); }
  void VisitElse(NodeVisitor* v)  { VUpdate(v, &else_part_); }

 private:
  Expr* cond_;
  Statement* then_part_;
  Statement* else_part_;

  // Prevent construction from outside the class (must use factory method)
  If()  { /* nothing to do */ }
};


class Loop: public BreakableStatement {
 public:
  // Creation
  // (we really would like to use Symbol instead of int for the sym
  // parameter below, but it leads to include conflicts w/o massive
  // reordering - sigh).
  static Loop* New(Proc* proc, FileLine* fileline, int sym);

  void set_before(Statement* before)  { before_ = before; }
  void set_cond(Expr* cond)  { cond_ = cond; }
  void set_after(Statement* after)  { after_ = after; }
  void set_body(Statement* body)  { body_ = body; }

  // Accessors
  int sym() const  { return sym_; }
  Statement* before() const  { return before_; }
  Expr* cond() const  { return cond_; }
  Statement* after() const  { return after_; }
  Statement* body() const  { return body_; }

  // Support for continue statement
  void set_cont(Label* cont)  { cont_ = cont; }
  Label* cont()  const  { return cont_; }

  // Conversion and cloning
  virtual Loop* AsLoop()  { return this; }
  virtual Loop* CloneStmt(CloneMap* cmap);

  // visitors
  virtual Statement* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);

  void VisitBefore(NodeVisitor* v)  { VUpdate(v, &before_); }
  void VisitCond(NodeVisitor* v)  { VUpdate(v, &cond_); }
  void VisitAfter(NodeVisitor* v)  { VUpdate(v, &after_); }
  void VisitBody(NodeVisitor* v)  { VUpdate(v, &body_); }

 private:
  int sym_;  // DO, FOR, or WHILE Symbol
  Statement* before_;
  Expr* cond_;
  Statement* after_;
  Statement* body_;
  Label* cont_;

  // Prevent construction from outside the class (must use factory method)
  Loop()  { /* nothing to do */ }
};


class Break: public Statement {
 public:
  // Creation
  static Break* New(Proc* proc, FileLine* fileline, BreakableStatement* stat);

  // Accessors
  BreakableStatement* stat() const  { return stat_; }
  void set_stat(BreakableStatement* stat)  { stat_ = stat; }

  // Conversion and cloning
  virtual Break* AsBreak()  { return this; }
  virtual Break* CloneStmt(CloneMap* cmap);

  // visitor
  virtual Statement* Visit(NodeVisitor* v);

 private:
  BreakableStatement* stat_;

  // Prevent construction from outside the class (must use factory method)
  Break()  { /* nothing to do */ }
};


class Continue: public Statement {
 public:
  // Creation
  static Continue* New(Proc* proc, FileLine* fileline, Loop* loop);

  // Accessors
  Loop* loop() const  { return loop_; }

  // Conversion and cloning
  virtual Continue* AsContinue()  { return this; }
  virtual Continue* CloneStmt(CloneMap* cmap);

  // visitor
  virtual Statement* Visit(NodeVisitor* v);

 private:
  Loop* loop_;

  // Prevent construction from outside the class (must use factory method)
  Continue()  { /* nothing to do */ }
};


class Result: public Statement {
 public:
  // Creation
  static Result* New(Proc* proc, FileLine* fileline,
                       StatExpr* statexpr, Variable* var, Expr* expr);

  // Accessors
  Expr* expr() const  { return expr_; }
  StatExpr* statexpr() const  { return statexpr_; }
  Variable* var() const  { return var_; }

  // Conversion and cloning
  virtual Result* AsResult()  { return this; }
  virtual Result* CloneStmt(CloneMap* cmap);

  // visitors
  virtual Statement* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitExpr(NodeVisitor* v)  { VUpdate(v, &expr_); }

 private:
  Expr* expr_;
  StatExpr* statexpr_;
  Variable* var_;

  // Prevent construction from outside the class (must use factory method)
  Result()  { /* nothing to do */ }
};


class Return: public Statement {
 public:
  // Creation
  static Return* New(Proc* proc, FileLine* fileline, Expr* result);

  // Accessors
  Expr* result() const  { return result_; }
  bool has_result() const  { return result_ != NULL; }

  // Conversion and cloning
  virtual Return* AsReturn()  { return this; }
  virtual Return* CloneStmt(CloneMap* cmap);

  // visitors
  virtual Statement* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitResult(NodeVisitor* v)  { VUpdate(v, &result_); }

 private:
  Expr* result_;

  // Prevent construction from outside the class (must use factory method)
  Return()  { /* nothing to do */ }
};


class Case {
 public:
  // Creation
  static Case* New(Proc* proc, List<Expr*>* labels, Statement* stat);

  // accessors
  List<Expr*>* labels() const  { return labels_; }
  Statement* stat() const  { return stat_; }

  // cloning
  Case* Clone(CloneMap* cmap);

  // visitors
  void VisitLabel(NodeVisitor* v, int i) {
    Node::VUpdate(v, &labels_->at(i));
  }
  void VisitStat(NodeVisitor* v) { Node::VUpdate(v, &stat_); }

 private:
  List<Expr*>* labels_;
  Statement* stat_;

  // Prevent construction from outside the class (must use factory method)
  Case()  { /* nothing to do */ }
};


class Switch: public BreakableStatement {
 public:
  // Creation
  static Switch* New(Proc* proc, FileLine* fileline, Expr* tag);
  void set_cases(List<Case*>* cases, Statement* default_case);

  // accessors
  Expr* tag() const  { return tag_; }
  List<Case*>* cases() const  { return cases_; }
  Statement* default_case() const  { return default_case_; }

  // Conversion and cloning
  virtual Switch* AsSwitch()  { return this; }
  virtual Switch* CloneStmt(CloneMap* cmap);

  // visitors
  virtual Statement* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitTag(NodeVisitor* v)  { VUpdate(v, &tag_); }
  void VisitCaseLabels(NodeVisitor* v) {
    for (int i = 0; i < cases_->length(); i++) {
      Case* the_case = cases_->at(i);
      for (int j = 0; j < the_case->labels()->length(); j++)
        the_case->VisitLabel(v, j);
    }
  }
  void VisitCaseStatements(NodeVisitor* v) {
    for (int i = 0; i < cases_->length(); i++) {
      Case* the_case = cases_->at(i);
      the_case->VisitStat(v);
    }
  }
  void VisitDefaultCase(NodeVisitor* v) {
    VUpdate(v, &default_case_);
  }

 private:
  Expr* tag_;
  List<Case*>* cases_;
  Statement* default_case_;

  // Prevent construction from outside the class (must use factory method)
  Switch()  { /* nothing to do */ }
};


class Assignment: public Statement {
 public:
  // Creation
  static Assignment* New(Proc* proc, FileLine* fileline, Expr* lvalue, Expr* rvalue);

  // Accessors
  Expr* lvalue() const  { return lvalue_; }
  Expr* rvalue() const  { return rvalue_; }

  // Conversion and cloning
  virtual Assignment* AsAssignment()  { return this; }
  virtual Assignment* CloneStmt(CloneMap* cmap);

  // Code generation utilities.
  virtual bool CanTrap();

  // Dead assignment optimization
  bool is_dead() const;
  Expr* selector_var() const;

  // visitors
  virtual Statement* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitLvalue(NodeVisitor* v)  { VUpdate(v, &lvalue_); }
  void VisitRvalue(NodeVisitor* v)  { VUpdate(v, &rvalue_); }

 private:
  Expr* lvalue_;
  Expr* rvalue_;

  // Prevent construction from outside the class (must use factory method)
  Assignment()  { /* nothing to do */ }
};


class Emit: public Statement {
 public:
  // Creation
  static Emit* New(Proc* proc, FileLine* fileline, Expr* output,
                   List<VarDecl*>* index_decls, VarDecl* elem_decl,
                   List<Expr*>* indices, Expr* value, Expr* weight,
                   Expr* index_format, Expr* elem_format);

  // Accessors
  Expr* output() const  { return output_; }
  List<VarDecl*>* index_decls() const  { return index_decls_; }
  VarDecl* elem_decl() const  { return elem_decl_; }
  List<Expr*>* indices() const  { return indices_; }
  Expr* value() const  { return value_; }
  Expr* weight() const  { return weight_; }
  Expr* index_format() const  { return index_format_; }
  Expr* elem_format() const  { return elem_format_; }

  // Conversion and cloning
  virtual Emit* AsEmit()  { return this; }
  virtual Emit* CloneStmt(CloneMap* cmap);

  // Code generation utilities
  // TODO: update when we get static analysis of emits.
  virtual bool CanTrap()  { return true; }

  // visitors
  virtual Statement* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitOutput(NodeVisitor* v)  { VUpdate(v, &output_); }
  void VisitIndices(NodeVisitor* v)  { VUpdate(v, indices_); }
  void VisitValue(NodeVisitor* v)  { VUpdate(v, &value_); }
  void VisitWeight(NodeVisitor* v)  { VUpdate(v, &weight_); }
  void VisitIndexFormat(NodeVisitor* v)  {  VUpdate(v, &index_format_); }
  void VisitElemFormat(NodeVisitor* v)  {  VUpdate(v, &elem_format_); }

 private:
  Expr* output_;
  // the temporary variables and the expressions that are store in them
  // are store separately (rather than storing the expressions as the init()
  // of the VarDecls) because they are not statements; in particular, they must
  // not be sequence points for value propagation and must not be trap ranges.
  List<VarDecl*>* index_decls_;  // temps to hold index expressions
  VarDecl* elem_decl_;           // temp to hold value expression
  List<Expr*>* indices_;         // index expressions
  Expr* value_;                  // value expression
  Expr* weight_;                 // weight
  Expr* index_format_;           // index format expression, if any
  Expr* elem_format_;            // value format expression, if any

  // Prevent construction from outside the class (must use factory method)
  Emit()  { /* nothing to do */ }
};


class Increment: public Statement {
 public:
  // Creation
  static Increment* New(Proc* proc, FileLine* fileline, Expr* lvalue, int delta);

  // Accessors
  Expr* lvalue() const  { return lvalue_; }
  int8 delta() const  { return delta_; }

  // Conversion and cloning
  virtual Increment* AsIncrement()  { return this; }
  virtual Increment* CloneStmt(CloneMap* cmap);

  // visitors
  virtual Statement* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitLvalue(NodeVisitor* v)  { VUpdate(v, &lvalue_); }

 private:
  Expr* lvalue_;
  int8 delta_;

  // Prevent construction from outside the class (must use factory method)
  Increment()  { /* nothing to do */ }
};


class Decl: public Statement {
 protected:
  void Initialize(FileLine* fileline) {
    Statement::Initialize(fileline);
  }

  // Prevent construction from outside the class (must use factory method)
  Decl()  { /* nothing to do */ }
};


class TypeDecl: public Decl {
 public:
   // Creation
  static TypeDecl* New(Proc* proc, FileLine* fileline, TypeName* tname,
                       bool print_expanded);

  TypeName* tname() const  { return tname_; }
  bool print_expanded() const  { return print_expanded_; }

  // Conversion and cloning
  virtual TypeDecl* AsTypeDecl()  { return this; }
  virtual TypeDecl* Clone(CloneMap* cmap);
  virtual TypeDecl* CloneStmt(CloneMap* cmap);

  // visitor
  virtual Statement* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitTname(NodeVisitor* v)  { VUpdate(v, &tname_); }

 private:
  TypeName* tname_;
  bool print_expanded_;  // whether to expand right side when printing

  // Prevent construction from outside the class (must use factory method)
  TypeDecl()  { /* nothing to do */ }
};


class Block: public Statement {
 public:
  // Creation
  static Block* New(Proc* proc, FileLine* fileline, Scope* scope, bool is_program);

  void Append(Statement* stat);

  // Accessors
  Scope* scope() const  { return scope_; }
  bool is_program() const  { return is_program_; }

  Statement* at(int i) const;
  int length() const;

  // Conversion and cloning
  virtual Block* AsBlock()  { return this; }
  Block* Clone(CloneMap* cmap)  { return CloneStmt(cmap); }
  virtual Block* CloneStmt(CloneMap* cmap);

  // visitors
  virtual Block* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitList(NodeVisitor* v)  { VUpdate(v, &list_); }

 protected:
  Scope* scope_;
  bool is_program_;
  List<Statement*> list_;

  // Prevent construction from outside the class (must use factory method)
  explicit Block(Proc* proc) : list_(proc)  { }

  // C++ compiler complains if there's no virtual destructor
  // however, no destructor is ever called...
  virtual ~Block()  { ShouldNotReachHere(); }
};


// Proto nodes correspond to proto clauses that include generated Sawzall
// code for a given proto file. Protocol compiler always generates complete
// statements, so we can expect that in error-free programs proto clauses
// appear on the statement boundary and can be treated as statements consisting
// of other statements (unlike include clauses that can occur practically
// anywhere). This is similar to a block except without a new scope.
class Proto: public Statement {
 public:
  // Creation
  static Proto* New(Proc* proc, FileLine* fileline, const char* file);

  void Append(Statement* stat);

  // Accessors
  const char* file() const { return file_; }
  Statement* at(int i) const;
  int length() const;

  // Conversion and cloning
  virtual Proto* AsProto()  { return this; }
  Proto* Clone(CloneMap* cmap)  { return CloneStmt(cmap); }
  virtual Proto* CloneStmt(CloneMap* cmap);

  // Visitors
  virtual Proto* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitList(NodeVisitor* v)  { VUpdate(v, &list_); }

 private:
  const char* file_;
  List<Statement*> list_;

  // Prevent construction from outside the class (must use factory method)
  explicit Proto(Proc* proc) : list_(proc)  { }

  // C++ compiler complains if there's no virtual destructor
  // however, no destructor is ever called...
  virtual ~Proto()  { ShouldNotReachHere(); }
};


class VarDecl: public Decl {
 public:
  // Creation
  static VarDecl* New(Proc* proc, FileLine* fileline, szl_string name,
         Type* type, Function* owner, int level, bool is_param, Expr* init);

  virtual Object* object()  { return &object_; }

  szl_string name() const  { return object_.name(); }
  bool has_name() const  { return object_.has_name(); }
  void set_doc(const char* doc)  { object_.set_doc(doc); }
  virtual Type* type() const  { return type_; }
  void set_type(Type* type);
  Expr* init() const  { return init_; }
  void set_init(Expr* expr)  { assert(init_ == NULL); init_ = expr; }
  TupleType* tuple() const  { return tuple_; }
  void set_tuple(TupleType* tuple)  { tuple_ = tuple; }
  void UsesTrapinfoIndex(Proc* proc);  // allocates one if necessary
  int trapinfo_index() const {
    assert(trapinfo_index_ >= 0);
    return trapinfo_index_;
  };
  int offset() const  { return offset_; }
  void set_offset(int offset)  { offset_ = offset; }
  Function* owner() const  { return owner_; }
  int level() const  { return level_; }
  bool is_local() const  { return level() > 0; }
  bool is_param() const  { return is_param_; }
  bool is_static() const  { return level_ == 0; }
  bool modified_after_init() const  { return modified_after_init_; }
  bool modified_at_call() const  { return modified_at_call_; }
  void set_modified_after_init()  { modified_after_init_ = true; }
  void set_modified_at_call()  { modified_at_call_ = true; }

  // Conversion and cloning
  virtual VarDecl* AsVarDecl()  { return this; }
  virtual VarDecl* Clone(CloneMap* cmap);
  virtual VarDecl* CloneStmt(CloneMap* cmap);
  VarDecl* AlwaysCloneStmt(CloneMap* cmap);

  // Code generation utilities.
  virtual bool CanTrap() {
    // Only called for output variables.
    assert(is_static() && type()->is_output());
    return true;
  }

  // visitors
  virtual VarDecl* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitInit(NodeVisitor* v)  { VUpdate(v, &init_); }

protected:
  void Initialize(FileLine* fileline, szl_string name, Type* type,
                  Function* owner, int level, bool is_param, Expr* init);
  int trapinfo_index_;  // index in table of per-variable saved trap info
private:
  Object object_;
  Type* type_;
  int offset_;  // may be negative in native mode
  Function* owner_;
  int level_;
  bool is_param_;
  Expr* init_;
  TupleType* tuple_;  // if is tuple-scope static, this is the tuple
  // computed during parsing, used during static analysis
  bool modified_after_init_;     // is modified after initialization
  bool modified_at_call_;        // is modified in a nested function

 protected:
  // Prevent construction from outside the class (must use factory method)
  VarDecl() : object_(this)  { /* nothing to do */ }
  friend class Variable;
};


class QuantVarDecl: public VarDecl {
 public:
  enum Kind { ALL, EACH, SOME };

  // Creation
  static QuantVarDecl* New(Proc* proc, FileLine* fileline, szl_string name,
                           Type* type, Function* owner, int level, Kind kind);

  // Attributes
  Kind kind() const  { return kind_; }
  const char* KindAsString() const;

  // Conversion and cloning
  QuantVarDecl* AsQuantVarDecl()  { return this; }
  virtual QuantVarDecl* Clone(CloneMap* cmap);
  virtual QuantVarDecl* CloneStmt(CloneMap* cmap);

 protected:
  void Initialize(FileLine* fileline, szl_string name, Type* type,
                  Function* owner, int level, Kind kind);

 private:
  Kind kind_;
};


class Variable: public Expr {
 public:
  // Creation
  static Variable* New(Proc* proc, FileLine* fileline, VarDecl* var_decl);

  virtual Type* type() const  { return var_decl_->type_; }
  virtual Variable* AsVariable()  { return this; }
  virtual Variable* Clone(CloneMap* cmap);
  VarDecl* var_decl() const  { return var_decl_; }
  bool is_lvalue() const  { return is_lvalue_; }
  bool is_rvalue() const  { return is_rvalue_; }
  void set_is_lvalue()  { is_lvalue_ = true; }
  void clear_is_rvalue()  { is_rvalue_ = false; }
  void set_is_defined()  { is_defined_ = true; }
  bool subst_visited() const  { return subst_visited_; }
  void set_subst_visited()  { assert(!subst_visited_); subst_visited_ = true; }

  // Forwarding functions for convenience.
  szl_string name() const  { return var_decl_->name(); }
  bool is_static() const  { return var_decl_->is_static(); }
  int level() const  { return var_decl_->level(); }
  int offset() const  { return var_decl_->offset(); }
  bool is_param() const  { return var_decl_->is_param(); }
  Function* owner() const  { return var_decl_->owner(); }

  // Code generation utilities
  virtual bool CanCall(bool is_lhs) const;
  virtual bool CanCauseTrap(bool is_lvalue) const {
    // As a direct lvalue, cannot cause a trap.
    // If value propagation marked this node as defined, we know it cannot
    // cause a trap (includes statics).
    // Otherwise we have to assume (for non-lvalues) that it can
    return !is_lvalue && !is_defined_;
  }

  // visitor
  virtual Expr* Visit(NodeVisitor* v);

 protected:
  void Initialize(Proc* proc, FileLine* fileline, VarDecl* var_decl);

  VarDecl* var_decl_;   // the declaration
  bool subst_visited_;  // seen by SubstitutionVisitor
  bool is_lvalue_;      // is used as an lvalue
  bool is_rvalue_;      // is used as an rvalue
  bool is_defined_;     // is known to be defined, for trap handling
  // Prevent construction from outside the class (must use factory method)
  Variable()  { /* nothing to do */ }
};


class TempVariable: public Variable {
 public:
  // Creation
  static TempVariable* New(Proc* proc, VarDecl* var_decl, Expr* init);

  // Accessors
  Expr* init() const  { return init_; }
  bool initialized() const  { return init_generated_; }
  void set_initialized()  { init_generated_ = true; }

  // Conversion and cloning
  virtual TempVariable* AsTempVariable()  { return this; }
  virtual TempVariable* Clone(CloneMap* cmap);

  // Code generation utilities
  virtual bool CanCall(bool is_lhs) const;

  // visitor
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitInit(NodeVisitor* v)  { VUpdate(v, &init_); }

 private:
  Expr* init_;  // associated with evaluation, not declaration
  bool init_generated_;  // for temp variables
  // Prevent construction from outside the class (must use factory method)
  TempVariable()  { /* nothing to do */ }
};


// TODO: make Field inherit from Node, not Expr
//   - modify Parser::ParseOperand to create a Selector; but then what to use
//     for the variable?  using type declarations, two output variables may
//     have the same type - so leave Selector var null, or otherwise use
//     a special value?
//   - remove CodeGen::DoField() and NativeCodegen::DoField()

class Field: public Node {
 public:
  // Creation
  static Field* New(Proc* proc, FileLine* fileline, szl_string name, Type* type);

  virtual Object* object()  { return &object_; }
  szl_string name() const  { return object_.name_; }
  bool has_name() const  { return object_.has_name(); }
  bool is_anonymous() const  { return object_.is_anonymous(); }
  Scope* scope() const  { return object_.scope_; }
  virtual Type* type() const  { return type_; }
  void set_type(Type* type)  { type_ = type; }
  virtual Field* AsField()  { return this; }

  int slot_index() const  { CHECK(slot_index_ >= 0); return slot_index_; }
  void set_slot_index(int slot_index)  { slot_index_ = slot_index; }

  Expr* value() const  { return value_; }
  bool has_value() const  { return value_ != NULL; }
  void set_value(Expr* value)  { value_ = value; }

  int tag() const  { return tag_; }
  bool has_tag() const  { return tag_ > 0; }
  void set_tag(int tag)  { assert(tag > 0); tag_ = tag; }

  ProtoBufferType pb_type() const  { return pb_type_; }
  void set_pb_type(const ProtoBufferType pb_type)  { pb_type_ = pb_type; }

  bool read() const;
  void set_read()  { read_ = true; }
  void clear_read() { read_ = false; }

  bool recursive() const  { return recursive_; };

  // Cloning.  Only for OutputType and Emit; see comments in the definition.
  virtual Field* Clone(CloneMap* cmap);

  // Visitors
  virtual Field* Visit(NodeVisitor* v);

 private:
  Object object_;
  Type* type_;
  int slot_index_;         // -1 before the field has been bound to a slot
  Expr* value_;            // either a Literal or a Conversion(string Literal)
  int tag_;
  ProtoBufferType pb_type_;
  bool read_;              // read through a selector or implicitly
  bool recursive_;         // makes use of a type containing this field

  // Prevent construction from outside the class (must use factory method)
  Field() : object_(this)  { /* nothing to do */ }
};


class Intrinsic: public Expr {
 public:
  // Intrinsic attributes
  enum Attribute {
    kNormal = 0 << 0,
    kCanFold = 1 << 0,  // This intrinsic can be constant-folded.
    kThreadSafe = 1 << 1,  // This intrinsic is thread-safe.
  };

  enum Kind {
    // generic intrinsic w/ normal FunctionType & CFunction
    // (ftype provides signature & optional parameter values)
    INTRINSIC,
    // special intrinsic w/ normal FunctionType but special implementation
    // (ftype provides signature, CFunction is not used)
    MATCH,
    MATCHPOSNS,
    MATCHSTRS,
    // special intrinsics recognized by parser and translated into special nodes
    // (ftype not strictly required)
    CONVERT,
    NEW,
    REGEX,
    SAW,
    SAWN,
    SAWZALL,
    // intrinsics w/ special parameter lists and/or code generation
    // (ftype provides result type only)
    ABS,
    ADDRESSOF,
    DEBUG,
    DEF,
    FINGERPRINTOF,
    FORMAT,
    HASKEY,
    HEAPCHECK,
    CLEARPROTO,
    INPROTO,
    KEYS,
    LEN,
    LOOKUP,
    SORT,
    SORTX,
    UNDEFINE,
  };

  typedef const char* (*CFunctionCanFail)(Proc* proc, Val**& sp);
  typedef void (*CFunctionCannotFail)(Proc* proc, Val**& sp);
  typedef void (*CFunction)();

  // Creation
  // Unlike other attributes, can_fail is not set directly at intrinisc
  // registration, so it's specified separately.
  static Intrinsic* New(Proc* proc, FileLine* fileline, szl_string name,
                        FunctionType* ftype, Kind kind, CFunction function,
                        const char* doc, int attributes, bool can_fail);

  virtual Object* object()  { return &object_; }
  CFunction function() const  { return function_; }
  bool can_fail() const  { return can_fail_; }
  bool can_fold() const  { return (attr_ & kCanFold) != 0; }
  bool thread_safe() const { return (attr_ & kThreadSafe) != 0; }
  int attr() const { return attr_; }
  FunctionType* ftype() const  { return ftype_; }
  virtual Type* type() const  { return ftype_; }
  virtual Intrinsic* AsIntrinsic()  { return this; }
  Kind kind() const  { return kind_; }
  szl_string name() const  { return name_; }

  // Cloning - intrinsics are not cloned
  virtual Intrinsic* Clone(CloneMap* cmap) { return this; }

  // Code generation utilities
  virtual bool CanCauseTrap(bool is_lvalue) const  { return false; }

  // visitor
  virtual Expr* Visit(NodeVisitor* v);

  // Add an overload for this function (i.e., same name but
  // different parameter list).  These overloads are linked
  // together via next_overload().
  virtual bool add_overload(Intrinsic* overload);

  // Return the next overloaded function in the linked list (or NULL)
  virtual Intrinsic* next_overload();

 private:
  Object object_;
  szl_string name_;
  FunctionType* ftype_;
  Kind kind_;
  CFunction function_;
  int attr_;  // a bitmap of Attributes
  bool can_fail_;
  Intrinsic* next_overload_;

  // Prevent construction from outside the class (must use factory method)
  Intrinsic() : object_(this), next_overload_(NULL)  { /* nothing to do */ }
};


class TypeName: public Node {
 public:
  // Creation
  static TypeName* New(Proc* proc, FileLine* fileline, szl_string name);

  virtual Object* object()  { return &object_; }
  szl_string name() const  { return object_.name(); }
  void set_type(Type* type);
  virtual Type* type() const  { return type_; }
  virtual TypeName* AsTypeName()  { return this; }
  virtual TypeName* Clone(CloneMap* cmap);
  virtual TypeName* CloneStmt(CloneMap* cmap);

  // visitor
  // (Note that the return type is TypeName*, not Node*.)
  virtual TypeName* Visit(NodeVisitor* v);

 private:
  Object object_;
  Type* type_;

  // Prevent construction from outside the class (must use factory method)
  TypeName() : object_(this)  { /* nothing to do */ }
};


class Selector: public Expr {
 public:
  // Creation
  static Selector* New(Proc* proc, FileLine* fileline, Expr* var, Field* field);

  Expr* var() const  { return var_; }
  Field* field() const  { return field_; }

  virtual Type* type() const  { return field_->type(); }

  // Conversion and cloning
  virtual Selector* AsSelector()  { return this; }
  virtual Selector* Clone(CloneMap* cmap);

  // Code generation utilities
  virtual bool CanCall(bool is_lhs) const;
  virtual bool CanCauseTrap(bool is_lvalue) const  { return false; }

  // visitors
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitVar(NodeVisitor* v)  { VUpdate(v, &var_); }
  void VisitField(NodeVisitor* v)  { VUpdate(v, &field_); }

 private:
  Expr* var_;
  Field* field_;

  // Prevent construction from outside the class (must use factory method)
  Selector()  { /* nothing to do */ }
};


class Index: public Expr {
 public:
  // Creation
  static Index* New(Proc* proc, FileLine* fileline, Expr* var, Expr* index,
                    Variable* length_temp);

  Expr* var() const  { return var_; }
  Expr* index() const  { return index_; }
  Variable* length_temp() const  { return length_temp_; }

  virtual Type* type() const;

  // Conversion and cloning
  virtual Index* AsIndex()  { return this; }
  virtual Index* Clone(CloneMap* cmap);

  // Code generation utilities
  virtual bool CanCall(bool is_lhs) const;
  virtual bool CanCauseTrap(bool is_lvalue) const {
    // An non-map index or a map index used as an rvalue can get an
    // index bounds trap (but a map index as an assigment target cannot).
    return !var_->type()->is_map() || !is_lvalue;
  }

  // visitors
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitVar(NodeVisitor* v)  { VUpdate(v, &var_); }
  void VisitIndex(NodeVisitor* v)  { VUpdate(v, &index_); }

 private:
  Expr* var_;
  Expr* index_;
  Variable* length_temp_;

  // Prevent construction from outside the class (must use factory method)
  Index()  { /* nothing to do */ }
};


class Dollar: public Expr {
 public:
  // Creation
  static Dollar* New(Proc* proc, FileLine* fileline, Expr* array,
                     Expr* length_temp);

  // Accessors
  virtual Type* type() const;
  Expr* array() const  { return array_; }
  Expr* length_temp() const  { return length_temp_; }

  // visitors
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitArray(NodeVisitor* v)  { VUpdate(v, &array_); }
  void VisitLengthTemp(NodeVisitor* v)  { VUpdate(v, &length_temp_); }

  // Conversion and cloning
  virtual Dollar* AsDollar()  { return this; }
  virtual Dollar* Clone(CloneMap* cmap);

  // Code generation utilities
  virtual bool CanCauseTrap(bool is_lvalue) const  { return false; }

 private:
  Expr* array_;
  // The length temp is an Expr, not a Variable, because value propagation
  // and constant folding might let the indexing node compute a constant value
  // for the length and so we need to be able to substitute a literal here.
  Expr* length_temp_;

  // Prevent construction from outside the class (must use factory method)
  Dollar()  { /* nothing to do */ }
};


class RuntimeGuard: public Expr {
 public:
  // Creation
  static RuntimeGuard* New(Proc* proc, FileLine* fileline,
                    Expr* guard, Expr* expr, const char* msg);

  // Accessors
  virtual Type* type() const;
  Expr* guard() const  { return guard_; }
  Expr* expr() const  { return expr_; }
  const char* msg() const  { return msg_; }

  // Conversion and cloning
  virtual RuntimeGuard* AsRuntimeGuard()  { return this; }
  virtual RuntimeGuard* Clone(CloneMap* cmap);

  // Code generation utilities.
  virtual bool CanCauseTrap(bool is_lvalue) const  { return true; }

  // Visitors
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitGuard(NodeVisitor* v)  { VUpdate(v, &guard_); }
  void VisitExpr(NodeVisitor* v)  { VUpdate(v, &expr_); }

 private:
  Expr* guard_;
  Expr* expr_;
  const char* msg_;

  // Prevent construction from outside the class (must use factory method)
  RuntimeGuard()  { /* nothing to do */ }
};


class Function: public Expr {
 public:
  // Creation
  static Function* New(Proc* proc, FileLine* fileline, szl_string name,
                       FunctionType* ftype, Function* owner, int level);
  virtual ~Function();

  void set_body(Block* body)  { body_ = body; }

  // Accessors
  szl_string name() const  { return name_; }
  FunctionType* ftype() const  { return ftype_; }
  virtual Type* type() const  { return ftype_; }
  Function* owner() const  { return owner_; }
  int level() const  { return level_; }
  int context_level() const  { return context_level_; }
  void set_context_level(int level)  { context_level_ = level; }
  Block* body() const  { return body_; }
  VarDecl* nonlocal_variable() const  { return nonlocal_variable_; }
  void set_nonlocal_variable(VarDecl* var)  { nonlocal_variable_ = var; }
  bool might_rtn_undef() const  { return !analysis_done_ || might_rtn_undef_; }
  void set_might_rtn_undef()  { might_rtn_undef_ = true; }
  bool analysis_started() const  { return analysis_started_; }
  void set_analysis_started()  { analysis_started_ = true; }
  bool analysis_done() const  { return analysis_done_; }
  void set_analysis_done()  { analysis_done_ = true; }

  // Hash value of function (not closure).
  int32 hash() const { return file_line()->offset(); }

  // Conversion and cloning
  virtual Function* AsFunction()  { return this; }
  virtual Function* Clone(CloneMap* cmap);
  virtual Function* AlwaysClone(CloneMap* cmap);

  // Code generation utilities.
  virtual bool CanCauseTrap(bool is_lvalue) const  { return false; }

  // visitors
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitBody(NodeVisitor* v)  { VUpdate(v, &body_); }

  // local variables
  void AddLocal(VarDecl* var);
  List<VarDecl*>* locals()  { return &locals_; }
  void set_params_size(size_t params_size)  { params_size_ = params_size; }
  size_t params_size() const  { return params_size_; }
  void set_locals_size(size_t locals_size)  { locals_size_ = locals_size; }
  size_t locals_size() const  { return locals_size_; }  // excluding params
  void set_frame_size(size_t frame_size)  { frame_size_ = frame_size; }
  size_t frame_size() const  { return frame_size_; }  // distance between fp and sp
  // returns bp delta to reach this static level
  static int bp_delta(Function* fun, int level);
  void AddParam(VarDecl* var)  { params_.Append(var); }
  List<VarDecl*>* params()  { return &params_; }
  List<Function*>* local_functions()  { return &local_functions_; }
  void add_local_function(Function* fun)  { local_functions_.Append(fun); }

  // code generation
  void set_entry(Label* entry)  { entry_ = entry; }
  Label* entry() const  { return entry_; }

 private:
  szl_string name_;
  FunctionType* ftype_;
  Function* owner_;
  int level_;
  int context_level_;
  VarDecl* nonlocal_variable_;  // var that caused context_level to be nonzero
  Block* body_;
  List<VarDecl*> locals_;
  List<VarDecl*> params_;
  List<Function*> local_functions_;
  size_t params_size_;
  size_t locals_size_;  // excluding params
  size_t frame_size_;
  Label* entry_;
  bool might_rtn_undef_;          // might return an undefined value
  bool analysis_started_;
  bool analysis_done_;

  // Prevent construction from outside the class (must use factory method)
  explicit Function(Proc* proc) :
    locals_(proc), params_(proc), local_functions_(proc)  { }
};


class Call: public Expr {
 public:
  // Creation
  static Call* New(Proc* proc, FileLine* fileline, Expr* fun, List<Expr*>* args);

  // Accessors
  virtual Type* type() const  { return fun_->type()->as_function()->result_type(); }
  Expr* fun() const  { return fun_; }
  // note that the caller keeps a pointer to the args list and uses it
  // to append optional args to the list even though our copy is const
  const List<Expr*>* args() const  { return args_; }
  int source_arg_count() const  { return source_arg_count_; }

  // Conversion and cloning
  virtual Call* AsCall()  { return this; }
  virtual Call* Clone(CloneMap* cmap);

  // Code generation utilities
  virtual bool CanCauseTrap(bool is_lvalue) const;

  // visitors
  virtual Expr* Visit(NodeVisitor* v);
  virtual void VisitChildren(NodeVisitor* v);
  void VisitFun(NodeVisitor* v)  { VUpdate(v, &fun_); }
  void VisitArgs(NodeVisitor* v)  {  VUpdate(v, args_); }

 private:
  Expr* fun_;
  List<Expr*>* args_;
  int source_arg_count_;

  // Prevent construction from outside the class (must use factory method)
  Call()  { /* nothing to do */ }
};


inline void Node::VUpdate(NodeVisitor* v, Expr** e) {
  if (*e != NULL) *e = (*e)->Visit(v);
}


inline void Node::VUpdate(NodeVisitor* v, Statement** s) {
  if (*s != NULL) *s = (*s)->Visit(v);
}


inline void Node::VUpdate(NodeVisitor* v, VarDecl** d) {
  if (*d != NULL) *d = (*d)->Visit(v);
}


inline void Node::VUpdate(NodeVisitor* v, Block** b) {
  if (*b != NULL) *b = (*b)->Visit(v);
}


inline void Node::VUpdate(NodeVisitor* v, TypeName** t) {
  if (*t != NULL) *t = (*t)->Visit(v);
}


inline void Node::VUpdate(NodeVisitor* v, Field** f) {
  if (*f != NULL) *f = (*f)->Visit(v);
}


class NodeVisitor {
 public:
  virtual ~NodeVisitor()  {  }

 private:
  // Override these methods if the graph is not being changed.
  // The default implementation of these methods is to call DoNode()
  // To visit the entire graph, call VisitChildren() in DoNode().
  // These methods are private because they are only called from this class.

  // default
  virtual void DoNode(Node* x)  {  }

  // expressions
  virtual void DoBadExpr(BadExpr* x)  { DoNode(x); }
  virtual void DoBinary(Binary* x)  { DoNode(x); }
  virtual void DoCall(Call* x)  { DoNode(x); }
  virtual void DoComposite(Composite* x)  { DoNode(x); }
  virtual void DoConversion(Conversion* x)  { DoNode(x); }
  virtual void DoDollar(Dollar* x)  { DoNode(x); }
  virtual void DoFunction(Function* x)  { DoNode(x); }
  virtual void DoRuntimeGuard(RuntimeGuard* x)  { DoNode(x); }
  virtual void DoIndex(Index* x)  { DoNode(x); }
  virtual void DoNew(New* x)  { DoNode(x); }
  virtual void DoRegex(Regex* x)  { DoNode(x); }
  virtual void DoSaw(Saw* x)  { DoNode(x); }
  virtual void DoSelector(Selector* x)  { DoNode(x); }
  virtual void DoSlice(Slice* x)  { DoNode(x); }
  virtual void DoStatExpr(StatExpr* x)  { DoNode(x); }
  virtual void DoIntrinsic(Intrinsic* x)  { DoNode(x); }
  virtual void DoLiteral(Literal* x)  { DoNode(x); }
  virtual void DoVariable(Variable* x)  { DoNode(x); }
  virtual void DoTempVariable(TempVariable* x)  { DoNode(x); }

  // statements
  virtual void DoAssignment(Assignment* x)  { DoNode(x); }
  virtual void DoBreak(Break* x)  { DoNode(x); }
  virtual void DoContinue(Continue* x)  { DoNode(x); }
  virtual void DoTypeDecl(TypeDecl* x)  { DoNode(x); }
  virtual void DoVarDecl(VarDecl* x)  { DoNode(x); }
  virtual void DoQuantVarDecl(QuantVarDecl* x)  { DoVarDecl(x); }
  virtual void DoEmit(Emit* x)  { DoNode(x); }
  virtual void DoEmpty(Empty* x)  { DoNode(x); }
  virtual void DoExprStat(ExprStat* x)  { DoNode(x); }
  virtual void DoIf(If* x)  { DoNode(x); }
  virtual void DoIncrement(Increment* x)  { DoNode(x); }
  virtual void DoSwitch(Switch* x)  { DoNode(x); }
  virtual void DoResult(Result* x)  { DoNode(x); }
  virtual void DoReturn(Return* x)  { DoNode(x); }
  virtual void DoWhen(When* x)  { DoNode(x); }
  virtual void DoLoop(Loop* x)  { DoNode(x); }

  // block
  virtual void DoBlock(Block* x)  { DoNode(x); }

  // other
  virtual void DoField(Field* x)  { DoNode(x); }
  virtual void DoTypeName(TypeName* x)  { DoNode(x); }

  // proto clauses
  // This node is here to aid printing, so other visitors can ignore it and
  // move on to its child type declarations right away.
  virtual void DoProto(Proto* x)  { x->VisitChildren(this); }

 public:
  // Override these methods to update the graph using the return value.
  // The default is to call the DoXXX() version and return the parameter
  // value so that the node is not replaced.
  // These methods are public because they are called from the XXX::Visit()
  // methods.

  // expressions
  virtual Expr* VisitBadExpr(BadExpr* x)  { DoBadExpr(x); return x; }
  virtual Expr* VisitBinary(Binary* x)  { DoBinary(x); return x; }
  virtual Expr* VisitCall(Call* x)  { DoCall(x); return x; }
  virtual Expr* VisitComposite(Composite* x)  { DoComposite(x); return x; }
  virtual Expr* VisitConversion(Conversion* x)  { DoConversion(x); return x; }
  virtual Expr* VisitDollar(Dollar* x)  { DoDollar(x); return x; }
  virtual Expr* VisitFunction(Function* x)  { DoFunction(x); return x; }
  virtual Expr* VisitRuntimeGuard(RuntimeGuard* x)  { DoRuntimeGuard(x); return x; }
  virtual Expr* VisitIndex(Index* x)  { DoIndex(x); return x; }
  virtual Expr* VisitNew(New* x)  { DoNew(x); return x; }
  virtual Expr* VisitRegex(Regex* x)  { DoRegex(x); return x; }
  virtual Expr* VisitSaw(Saw* x)  { DoSaw(x); return x; }
  virtual Expr* VisitSelector(Selector* x)  { DoSelector(x); return x; }
  virtual Expr* VisitSlice(Slice* x)  { DoSlice(x); return x; }
  virtual Expr* VisitStatExpr(StatExpr* x)  { DoStatExpr(x); return x; }
  virtual Expr* VisitIntrinsic(Intrinsic* x)  { DoIntrinsic(x); return x; }
  virtual Expr* VisitLiteral(Literal* x)  { DoLiteral(x); return x; }
  virtual Expr* VisitVariable(Variable* x)  { DoVariable(x); return x; }
  virtual Expr* VisitTempVariable(TempVariable* x)  { DoTempVariable(x); return x; }

  // statements
  virtual Statement* VisitAssignment(Assignment* x)  { DoAssignment(x); return x; }
  virtual Statement* VisitBreak(Break* x)  { DoBreak(x); return x; }
  virtual Statement* VisitContinue(Continue* x)  { DoContinue(x); return x; }
  virtual Statement* VisitTypeDecl(TypeDecl* x)  { DoTypeDecl(x); return x; }
  virtual VarDecl* VisitVarDecl(VarDecl* x)  { DoVarDecl(x); return x; }
  virtual Statement* VisitQuantVarDecl(QuantVarDecl* x)  { DoQuantVarDecl(x); return x; }
  virtual Statement* VisitEmit(Emit* x)  { DoEmit(x); return x; }
  virtual Statement* VisitEmpty(Empty* x)  { DoEmpty(x); return x; }
  virtual Statement* VisitExprStat(ExprStat* x)  { DoExprStat(x); return x; }
  virtual Statement* VisitIf(If* x)  { DoIf(x); return x; }
  virtual Statement* VisitIncrement(Increment* x)  { DoIncrement(x); return x; }
  virtual Statement* VisitSwitch(Switch* x)  { DoSwitch(x); return x; }
  virtual Statement* VisitResult(Result* x)  { DoResult(x); return x; }
  virtual Statement* VisitReturn(Return* x)  { DoReturn(x); return x; }
  virtual Statement* VisitWhen(When* x)  { DoWhen(x); return x; }
  virtual Statement* VisitLoop(Loop* x)  { DoLoop(x); return x; }

  // block
  virtual Block* VisitBlock(Block* x)  { DoBlock(x); return x; }

  // other
  virtual Field* VisitField(Field* x)  { DoField(x); return x; }
  virtual TypeName* VisitTypeName(TypeName* x)  { DoTypeName(x); return x; }

  // proto clauses
  virtual Proto* VisitProto(Proto* x) { DoProto(x); return x; }
};


// AST's can contain Nodes within Types when we have static variables
// declared in tuples or expressions used in output type parameters or
// in file, proc and format decorators.  A DeepNodeVisitor visits both
// the Nodes and the Types to reach all parts of the AST.
class DeepNodeVisitor: public NodeVisitor, public TypeVisitor {
 public:
  DeepNodeVisitor() { }
  virtual ~DeepNodeVisitor()  {  }

 protected:
  // Override these methods if the graph is not being changed.

  // Methods overridden from NodeVisitor

  // Visit variable declarations in tuples at their TypeDecl.
  virtual void DoTypeDecl(TypeDecl* x);

  // Visit param expressions of an output type in a VarDecl.
  virtual void DoVarDecl(VarDecl* x);

  // Methods overridden from TypeVisitor

  // Visit Nodes in param expressions.
  virtual void DoOutputType(OutputType* x);

  // Visit Nodes in variable declarations.
  virtual void DoTupleType(TupleType* x);
};


}  // namespace sawzall
