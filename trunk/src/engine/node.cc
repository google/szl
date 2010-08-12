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
#include <time.h>
#include <assert.h>

#include "engine/globals.h"
#include "public/commandlineflags.h"
#include "public/logging.h"

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
#include "engine/when.h"
#include "engine/ir.h"
#include "engine/tracer.h"
#include "engine/codegen.h"
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"


namespace sawzall {


DEFINE_bool(read_all_fields,
            false,
            "for debugging purposes, ignore field reference analysis and "
            "keep all tuple fields");


// ------------------------------------------------------------------------------
// Implementation of FileLine

FileLine* FileLine::New(Proc* proc, const char* file, int line, int offset,
                        int length) {
  FileLine* fl = NEW(proc, FileLine);
  fl->file_ = file;
  fl->line_ = line;
  fl->offset_ = offset;
  fl->length_ = length;
  return fl;
}


// ------------------------------------------------------------------------------
// Implementation of Node

BytesVal* Node::as_bytes() {
  Literal* lit = AsLiteral();
  if (lit != NULL && lit->val()->is_bytes())
    return lit->val()->as_bytes();
  return NULL;
}


BoolVal* Node::as_bool() {
  Literal* lit = AsLiteral();
  if (lit != NULL && lit->val()->is_bool())
    return lit->val()->as_bool();
  return NULL;
}


FingerprintVal* Node::as_fingerprint() {
  Literal* lit = AsLiteral();
  if (lit != NULL && lit->val()->is_fingerprint())
    return lit->val()->as_fingerprint();
  return NULL;
}


FloatVal* Node::as_float() {
  Literal* lit = AsLiteral();
  if (lit != NULL && lit->val()->is_float())
    return lit->val()->as_float();
  return NULL;
}



IntVal* Node::as_int() {
  Literal* lit = AsLiteral();
  if (lit != NULL && lit->val()->is_int())
    return lit->val()->as_int();
  return NULL;
}


StringVal* Node::as_string() {
  Literal* lit = AsLiteral();
  if (lit != NULL && lit->val()->is_string())
    return lit->val()->as_string();
  return NULL;
}



TimeVal* Node::as_time() {
Literal* lit = AsLiteral();
  if (lit != NULL && lit->val()->is_time())
    return lit->val()->as_time();
  return NULL;
}


UIntVal* Node::as_uint() {
  Literal* lit = AsLiteral();
  if (lit != NULL && lit->val()->is_uint())
    return lit->val()->as_uint();
  return NULL;
}



FileLine* Node::clone_fl(CloneMap* cmap) const {
  return (cmap->file_line() != NULL) ? cmap->file_line() : fileline_;
}


void Node::VUpdate(NodeVisitor* v, List<Expr*>* list) {
  if (list != NULL) {
    for (int i = 0; i < list->length(); i++)
      VUpdate(v, &list->at(i));
  }
}


void Node::VUpdate(NodeVisitor* v, List<Statement*>* list) {
  if (list != NULL) {
    for (int i = 0; i < list->length(); i++)
      VUpdate(v, &list->at(i));
  }
}


void Node::VUpdate(NodeVisitor* v, List<VarDecl*>* list) {
  if (list != NULL) {
    for (int i = 0; i < list->length(); i++)
      VUpdate(v, &list->at(i));
  }
}


int Node::NodeFmt(Fmt::State* f) {
  const RawSource* source = FMT_ARG(f, const RawSource*);
  Node* n = FMT_ARG(f, Node*);
  if (n == NULL)
    return 0;
  FileLine* fl = n->file_line();
  if (fl == NULL)
    return 0;
  assert(fl->offset() + fl->length() <= source->length);
  return F.fmtprint(f, "%.*s", fl->length(), source->start + fl->offset());
}


// ------------------------------------------------------------------------------
// Implementation of Object

// Infrequently used.
Type* Object::type() const {
  if (node_->AsExpr() != NULL)
    return node_->AsExpr()->type();
  else if (node_->AsField() != NULL)
    return node_->AsField()->type();
  else if (node_->AsTypeName() != NULL)
    return node_->AsTypeName()->type();
  else if (node_->AsVarDecl() != NULL)
    return node_->AsVarDecl()->type();
  ShouldNotReachHere();
  return NULL;
}


// ------------------------------------------------------------------------------
// Implementation of BadExpr

BadExpr* BadExpr::New(Proc* proc, FileLine* fileline, Node* node) {
  BadExpr* b = NEW(proc, BadExpr);
  b->Node::Initialize(fileline);
  b->object()->Initialize(NULL);
  b->node_ = node;
  return b;
}


BadExpr* BadExpr::Clone(CloneMap* cmap) {
  // Do not clone "node_", it is not used for analysis or codegen.
  return New(cmap->proc(), clone_fl(cmap), node_);
}


Expr* BadExpr::Visit(NodeVisitor* v) {
  return v->VisitBadExpr(this);
}


Type* BadExpr::type() const {
  return SymbolTable::bad_type();
}


// ------------------------------------------------------------------------------
// Implementation of Literal

Literal* Literal::New(Proc* proc, FileLine* fileline, szl_string name, Val* val) {
  val->set_readonly();
  Literal* lit = NEW(proc, Literal);
  lit->Node::Initialize(fileline);
  lit->object()->Initialize(name);
  lit->val_ = val;
  return lit;
}


Literal* Literal::NewBool(Proc* proc, FileLine* fileline, szl_string name, bool val) {
  return New(proc, fileline, name, Factory::NewBool(proc, val));
}


Literal* Literal::NewBytes(Proc* proc, FileLine* fileline, szl_string name, int length, const char* val) {
  return New(proc, fileline, name, Factory::NewBytesInit(proc, length, val));
}


Literal* Literal::NewFingerprint(Proc* proc, FileLine* fileline, szl_string name, szl_fingerprint val) {
  return New(proc, fileline, name, Factory::NewFingerprint(proc, val));
}


Literal* Literal::NewFloat(Proc* proc, FileLine* fileline, szl_string name, szl_float val) {
  return New(proc, fileline, name, Factory::NewFloat(proc, val));
}


Literal* Literal::NewInt(Proc* proc, FileLine* fileline, szl_string name, szl_int val) {
  return New(proc, fileline, name, Factory::NewInt(proc, val));
}


Literal* Literal::NewString(Proc* proc, FileLine* fileline, szl_string name, szl_string val) {
  return New(proc, fileline, name, Factory::NewStringC(proc, val));
}


Literal* Literal::NewTime(Proc* proc, FileLine* fileline, szl_string name, szl_time val) {
  return New(proc, fileline, name, Factory::NewTime(proc, val));
}


Literal* Literal::NewUInt(Proc* proc, FileLine* fileline, szl_string name, szl_uint val) {
  return New(proc, fileline, name, Factory::NewUInt(proc, val));
}


Type* Literal::type() const {
  return val_->type();
}


Expr* Literal::Visit(NodeVisitor* v) {
  return v->VisitLiteral(this);
}


// ------------------------------------------------------------------------------
// Implementation of Dollar

Dollar* Dollar::New(Proc* proc, FileLine* fileline, Expr* array,
                    Expr* length_temp) {
  Dollar* d = NEW(proc, Dollar);
  d->Expr::Initialize(fileline);
  d->array_ = array;
  d->length_temp_= length_temp;
  return d;
}


Type* Dollar::type() const {
  return SymbolTable::int_type();
}


Dollar* Dollar::Clone(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap), array_->Clone(cmap),
             cmap->CloneOrNull(length_temp_));
}


Expr* Dollar::Visit(NodeVisitor* v) {
  return v->VisitDollar(this);
}


void Dollar::VisitChildren(NodeVisitor *v) {
  VisitArray(v);
  VisitLengthTemp(v);
}


// ------------------------------------------------------------------------------
// Implementation of RuntimeGuard

RuntimeGuard* RuntimeGuard::New(
  Proc* proc, FileLine* fileline,
  Expr* guard, Expr* expr, const char* msg) {
  assert(guard->type()->is_bool());
  RuntimeGuard* g = NEW(proc, RuntimeGuard);
  g->Expr::Initialize(fileline);
  g->guard_ = guard;
  g->expr_ = expr;
  g->msg_ = msg;
  return g;
}


Type* RuntimeGuard::type() const {
  return expr_->type();
}


RuntimeGuard* RuntimeGuard::Clone(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap), guard_->Clone(cmap),
             expr_->Clone(cmap), msg_);
}


Expr* RuntimeGuard::Visit(NodeVisitor* v) {
  return v->VisitRuntimeGuard(this);
}


void RuntimeGuard::VisitChildren(NodeVisitor *v) {
  VisitGuard(v);
  VisitExpr(v);
}


// ------------------------------------------------------------------------------
// Implementation of Function

Function* Function::New(Proc* proc, FileLine* fileline, szl_string name,
                        FunctionType* ftype, Function* owner, int level) {
  Function* f = NEWP(proc, Function);
  f->Expr::Initialize(fileline);
  f->name_ = name;
  f->ftype_ = ftype;
  f->owner_ = owner;
  f->level_ = level;
  f->context_level_ = -1;
  f->nonlocal_variable_ = NULL;
  f->body_ = NULL;
  f->params_size_ = 0;
  f->locals_size_ = 0;
  f->frame_size_ = 0;
  f->entry_ = NULL;
  f->might_rtn_undef_ = false;  // getter returns true until analysis done
  f->analysis_started_ = false;
  f->analysis_done_ = false;
  assert(ftype != NULL);
  return f;
}


Function::~Function() {
  // TODO: do something here
}


void Function::AddLocal(VarDecl* var) {
  assert(var != NULL && var->owner() == this);
  // make sure variable is added only once
  for (int i = 0; i < locals_.length(); i++)
    assert(locals_[i] != var);
  locals_.Append(var);
};


Function* Function::Clone(CloneMap* cmap) {
  // Use a cloned Function if one exists - also catches recursive calls to
  // a function being cloned, which otherwise would not be treated as using
  // a function that needed to be cloned because the context levels are the
  // same.
  Function* clone = cmap->Find(this);
  if (clone != NULL)
    return clone;

  // Only clone functions that use local variables within the current
  // function context.  We can safely do this by testing the context level
  // because no function with a context level at or higher than the current
  // context but not in the current context can be available here.
  // (Using the original instead of a clone has the potential to confuse code
  // that inspects "owner_", just as with static declarations that are not
  // cloned.)
  //
  // If we knew that the function did not refer to any variable in the
  // current (or enclosed) context for which the declaration was cloned by
  // the current cloning operation, we could avoid cloning the function;
  // but this is painful to determine and probably not worth the work.
  //
  // Note that if we were to support equality of functions or the use
  // of functions as map indices, there would be an issue with comparing
  // clones of the same function.  But the problem would not arise when
  // using cloning for inlining, because any function value that could
  // be returned from an inline function would not be cloned.

  if (context_level() <= cmap->context()->context_level())
    return this;

  // Clone it and insert in the map.
  return AlwaysClone(cmap);
}


Function* Function::AlwaysClone(CloneMap* cmap) {
  assert(cmap->table() != NULL);
  assert(cmap->Find(this) == NULL);
  assert(owner_ != NULL);    // never clone $main
  assert(params_size_ == 0);
  assert(locals_size_ == 0);
  assert(frame_size_ == 0);
  assert(entry_ == NULL);
  assert(!analysis_started_);
  assert(!analysis_done_);
  Function* owner = cmap->Find(owner_);
  if (owner == NULL)
    owner = owner_;
  Function* clone = New(cmap->proc(), clone_fl(cmap), name(), ftype_,
                        owner, level_);
  cmap->Insert(this, clone);
  clone->context_level_ = context_level_;
  clone->might_rtn_undef_ = might_rtn_undef_;
  for (int i = 0; i < params_.length(); i++)
    clone->params_.Append(params_.at(i)->CloneStmt(cmap));
  clone->body_ = body_->Clone(cmap);
  // Locals and local functions should already be cloned; just rebuild the
  // lists.
  cmap->CloneListOfAlreadyCloned(&locals_, &clone->locals_);
  cmap->CloneListOfAlreadyCloned(&local_functions_, &clone->local_functions_);
  clone->nonlocal_variable_ = cmap->CloneOrNull(nonlocal_variable_);
  cmap->table()->add_function(clone);
  return clone;
}


Expr* Function::Visit(NodeVisitor* v) {
  return v->VisitFunction(this);
}


void Function::VisitChildren(NodeVisitor *v) {
  VisitBody(v);
}


int Function::bp_delta(Function* fun, int level) {
  // Compute the delta: how far back must we go in the static frame list,
  // starting from the specified function's frame, to get to the specified
  // level.  Note that the static frame list is sparse: it does not contain
  // frames for which no active function references any variable in that frame.
  // So it is *not* sufficient to simply compute the difference of the levels;
  // we must walk the stack of enclosed functions and count the actual number
  // of frames that will be in the frame list between the current context
  // and the desired context at runtime.
  if (level < 0)
    level = 0;
  int delta = 0;
  while (fun != NULL && fun->level() != level) {
    // no match, go to the nearest enclosing frame that will be represented
    // in the static frame list, and count that as one move in the list
    int next_static_frame_level = fun->context_level();
    while (fun != NULL && fun->level() > next_static_frame_level) {
      fun = fun->owner();
    }
    delta++;
  }
  assert((fun == NULL) == (level == 0));
  return delta;
}


// ------------------------------------------------------------------------------
// Implementation of Call

Call* Call::New(Proc* proc, FileLine* fileline, Expr* fun, List<Expr*>* args) {
  Call* c = NEW(proc, Call);
  c->Expr::Initialize(fileline);
  c->fun_ = fun;
  c->args_ = args;
  c->source_arg_count_ = args->length();
  assert(fun->type()->is_function());
  return c;
}


Call* Call::Clone(CloneMap* cmap) {
  Call* clone = New(cmap->proc(), clone_fl(cmap), fun_->Clone(cmap),
                    cmap->CloneList(args_));
  clone->source_arg_count_ = source_arg_count_;
  return clone;
}


Expr* Call::Visit(NodeVisitor* v) {
  return v->VisitCall(this);
}


void Call::VisitChildren(NodeVisitor *v) {
  VisitFun(v);
  VisitArgs(v);
}


// ------------------------------------------------------------------------------
// Implementation of Conversion

Conversion* Conversion::New(Proc* proc, FileLine* fileline, Type* type,
                            Expr* src, List<Expr*>* params,
                            int source_param_count, Kind kind,
                            ConversionOp op, ConversionOp key_op) {
  Conversion* c = NEW(proc, Conversion);
  c->Expr::Initialize(fileline);
  c->type_ = type;
  c->src_ = src;
  c->params_ = params;
  c->source_param_count_ = source_param_count;
  c->kind_ = kind;
  c->op_ = op;
  c->key_op_ = key_op;
  return c;
}


Conversion* Conversion::Clone(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap), type_, src_->Clone(cmap),
             cmap->CloneList(params_), source_param_count_, kind_,
             op_, key_op_);
}


Expr* Conversion::Visit(NodeVisitor* v) {
  return v->VisitConversion(this);
}


void Conversion::VisitChildren(NodeVisitor *v) {
  VisitSrc(v);
  VisitParams(v);
}


// ------------------------------------------------------------------------------
// Implementation of New

New* New::New_(Proc* proc, FileLine* fileline, Type* type, Expr* length, Expr* init) {
  New* n = NEW(proc, New);
  n->Expr::Initialize(fileline);
  n->type_ = type;
  n->length_ = length;
  n->init_ = init;
  assert(type != NULL && (type->is_allocatable() || type->is_bad()));
  assert((length != NULL));
  assert((init != NULL) == (type->is_indexable() || type->is_bad()));
  return n;
};


New* New::Clone(CloneMap* cmap) {
  return New_(cmap->proc(), clone_fl(cmap), type_, length_->Clone(cmap),
              cmap->CloneOrNull(init_));
}


Expr* New::Visit(NodeVisitor* v) {
  return v->VisitNew(this);
}


void New::VisitChildren(NodeVisitor *v) {
  VisitLength(v);
  VisitInit(v);
}


// ------------------------------------------------------------------------------
// Implementation of Regex

Regex* Regex::New(Proc* proc, FileLine* fileline, Type* arg, Expr* base) {
  Regex* r = NEW(proc, Regex);
  r->Expr::Initialize(fileline);
  r->arg_ = arg;
  r->base_ = base;
  return r;
}


Regex* Regex::Clone(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap), arg_, cmap->CloneOrNull(base_));
}


Expr* Regex::Visit(NodeVisitor* v) {
  return v->VisitRegex(this);
}


void Regex::VisitChildren(NodeVisitor *v) {
  VisitBase(v);
}


Type* Regex::type() const {
  return SymbolTable::string_type();
}

// ------------------------------------------------------------------------------
// Implementation of Saw

Saw* Saw::New(Proc* proc, FileLine* fileline, Kind kind, Expr* count, Expr* str,
              bool static_args, List<Expr*>* args, List<Flag>* flags) {
  Saw* s = NEW(proc, Saw);
  s->Expr::Initialize(fileline);
  s->kind_ = kind;
  s->count_ = count;
  s->str_ = str;
  s->static_args_ = static_args;
  s->args_ = args;
  s->flags_ = flags;
  return s;
}


Type* Saw::type() const {
  return SymbolTable::array_of_string_type();
}


Saw* Saw::Clone(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap), kind_, count_->Clone(cmap),
             str_->Clone(cmap), static_args_, cmap->CloneList(args_), flags_);
}


const char* Saw::Kind2String(Kind kind) {
  switch (kind) {
  case ILLEGAL:
    return "illegal";
  case SAW:
    return "saw";
  case SAWN:
    return "sawn";
  case SAWZALL:
    return "sawzall";
  }
  // extend Kind2String => FatalError
  FatalError("unknown Saw::Kind %d", kind);
  return NULL;
}


const char* Saw::Flag2String(Flag flag) {
  switch (flag) {
  case NONE:
    return "";
  case SKIP:
    return "skip "; // space included to help Printvisitor
  case REST:
      return "rest ";
  case SUBMATCH:
      return "submatch ";
  }
  // extend Flag2String => FatalError
  FatalError("unknown Saw::Flag %d", flag);
  return NULL;
}


Expr* Saw::Visit(NodeVisitor* v) {
  return v->VisitSaw(this);
}


void Saw::VisitChildren(NodeVisitor *v) {
  VisitCount(v);
  VisitStr(v);
  VisitArgs(v);
}


// ------------------------------------------------------------------------------
// Implementation of Composite

Composite* Composite::New(Proc* proc, FileLine* fileline) {
  Composite* c = NEWP(proc, Composite);
  c->Expr::Initialize(fileline);
  c->type_ = SymbolTable::incomplete_type();
  c->has_pairs_ = false;
  c->has_conversion_ = false;
  return c;
}


void Composite::Append(Expr* x) {
  list_.Append(x);
}


Expr* Composite::at(int i) const {
  return list_[i];
}


int Composite::length() const {
  return list_.length();
}


void Composite::set_type(Type* type) {
  assert(type != NULL && type->is_map() == has_pairs_);
  type_ = type;
}


void Composite::set_has_pairs(bool has_pairs) {
  assert(type_->is_incomplete() || type_->is_map() == has_pairs);
  has_pairs_ = has_pairs;
}

Composite* Composite::Clone(CloneMap* cmap) {
  Composite* clone = NEWP(cmap->proc(), Composite);
  clone->Expr::Initialize(clone_fl(cmap));
  cmap->CloneList(&list_, &clone->list_);
  clone->type_ = type_;
  clone->has_pairs_ = has_pairs_;
  return clone;
}


Expr* Composite::Visit(NodeVisitor* v) {
  return v->VisitComposite(this);
}


void Composite::VisitChildren(NodeVisitor *v) {
  VisitList(v);
}


// ------------------------------------------------------------------------------
// Implementation of Slice

Slice* Slice::New(Proc* proc, FileLine* fileline, Expr* var, Expr* beg,
                  Expr* end, Variable* length_temp) {
  Slice* s = NEW(proc, Slice);
  s->Expr::Initialize(fileline);
  s->var_ = var;
  s->beg_ = beg;
  s->end_ = end;
  s->length_temp_ = length_temp;
  return s;
}


Slice* Slice::Clone(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap), var_->Clone(cmap), beg_->Clone(cmap),
             end_->Clone(cmap), cmap->CloneOrNull(length_temp_));
}


Expr* Slice::Visit(NodeVisitor* v) {
  return v->VisitSlice(this);
}


void Slice::VisitChildren(NodeVisitor *v) {
  VisitVar(v);
  VisitBeg(v);
  VisitEnd(v);
}


// ------------------------------------------------------------------------------
// Implementation of StatExpr

StatExpr* StatExpr::New(Proc* proc, FileLine* fileline) {
  StatExpr* s = NEWP(proc, StatExpr);
  s->Expr::Initialize(fileline);
  s->type_ = SymbolTable::incomplete_type();
  s->exit_ = NULL;
  s->analysis_started_ = false;
  return s;
}


void StatExpr::set_type(Type* type) {
  assert(type != NULL);
  type_ = type;
}


void StatExpr::set_tempvar(TempVariable* tempvar) {
  assert(tempvar != NULL);
  tempvar_ = tempvar;
}


void StatExpr::set_var(Variable* var) {
  assert(var != NULL);
  var_ = var;
}


void StatExpr::set_body(Statement* body) {
  assert(body != NULL);
  body_ = body;
}


StatExpr* StatExpr::Clone(CloneMap* cmap) {
  assert(exit_ == NULL);
  assert(!analysis_started_);
  StatExpr* clone = New(cmap->proc(), clone_fl(cmap));
  cmap->Insert(this, clone);  // for Result; must be done before body cloned
  clone->type_ = type_;
  clone->body_ = body_->Clone(cmap);
  clone->tempvar_ = tempvar_->Clone(cmap);
  clone->var_ = var_->Clone(cmap);
  return clone;
}

Expr* StatExpr::Visit(NodeVisitor* v) {
  return v->VisitStatExpr(this);
}


void StatExpr::VisitChildren(NodeVisitor *v) {
  VisitBody(v);
}


// ------------------------------------------------------------------------------
// Implementation of Selector

Selector* Selector::New(Proc* proc, FileLine* fileline, Expr* var, Field* field) {
  assert(var->type()->is_tuple());
  Selector* s = NEW(proc, Selector);
  s->Expr::Initialize(fileline);
  s->var_ = var;
  s->field_ = field;
  return s;
}


Selector* Selector::Clone(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap), var_->Clone(cmap), field_);
}


Expr* Selector::Visit(NodeVisitor* v) {
  return v->VisitSelector(this);
}


void Selector::VisitChildren(NodeVisitor *v) {
  VisitVar(v);
  VisitField(v);
}


// ------------------------------------------------------------------------------
// Implementation of Index

Index* Index::New(Proc* proc, FileLine* fileline, Expr* var, Expr* index,
                  Variable* length_temp) {
  Index* n = NEW(proc, Index);
  n->Expr::Initialize(fileline);
  n->var_ = var;
  n->index_ = index;
  n->length_temp_ = length_temp;
  return n;
}


Type* Index::type() const {
  if (var_->type()->is_string() || var_->type()->is_bytes())
    return SymbolTable::int_type();
  if (var_->type()->is_array())
    return var_->type()->as_array()->elem_type();
  if (var_->type()->is_map())
    return var_->type()->as_map()->elem_type();
  return NULL;
}


Index* Index::Clone(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap), var_->Clone(cmap),
             index_->Clone(cmap), cmap->CloneOrNull(length_temp_));
}


Expr* Index::Visit(NodeVisitor* v) {
  return v->VisitIndex(this);
}


void Index::VisitChildren(NodeVisitor *v) {
  VisitVar(v);
  VisitIndex(v);
}


// ------------------------------------------------------------------------------
// Implementation of BinaryExpr

Binary* Binary::New(Proc* proc, FileLine* fileline,
                    Type* type, Expr* left, Op op, Opcode opcode, Expr* right) {
  Binary* b = NEW(proc, Binary);
  b->Expr::Initialize(fileline);
  b->type_ = type;
  b->left_ = left;
  b->op_ = op;
  b->opcode_ = opcode;
  b->right_ = right;
  return b;
}


Binary* Binary::Clone(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap), type_,
             left_->Clone(cmap), op_, opcode_, right_->Clone(cmap));
}


Expr* Binary::Visit(NodeVisitor* v) {
  return v->VisitBinary(this);
}


void Binary::VisitChildren(NodeVisitor *v) {
  VisitLeft(v);
  VisitRight(v);
}


const char* Binary::Op2String(Op op) {
  switch (op) {
    case ADD:
      return "+";
    case SUB:
      return "-";
    case MUL:
      return "*";
    case DIV:
      return "/";
    case MOD:
      return "%";
    case EQL:
      return "==";
    case NEQ:
      return "!=";
    case LSS:
      return "<";
    case LEQ:
      return "<=";
    case GTR:
      return ">";
    case GEQ:
      return ">=";
    case BAND:
      return "&";
    case BOR:
      return "|";
    case BXOR:
      return "^";
    case SHL:
      return "<<";
    case SHR:
      return ">>";
    case LAND:
      return "&&";
    case LOR:
      return "||";
    case AND:
      return "and";
    case OR:
      return "or";
  }
  return "unknown-binary?";
}


// ------------------------------------------------------------------------------
// Implementation of TypeDecl

TypeDecl* TypeDecl::New(Proc* proc, FileLine* fileline, TypeName* tname,
                        bool print_expanded) {
  TypeDecl* t = NEW(proc, TypeDecl);
  t->Decl::Initialize(fileline);
  t->tname_ = tname;
  t->print_expanded_ = print_expanded;
  return t;
}


TypeDecl* TypeDecl::Clone(CloneMap* cmap) {
  // Use a cloned TypeDecl if one exists, otherwise do not clone it here.
  TypeDecl* clone = cmap->Find(this);
  if (clone == NULL)
    clone = this;
  return clone;
}


TypeDecl* TypeDecl::CloneStmt(CloneMap* cmap) {
  assert(cmap->Find(this) == NULL);
  TypeDecl* clone = New(cmap->proc(), clone_fl(cmap), tname_->CloneStmt(cmap),
                        print_expanded_);
  cmap->Insert(this, clone);
  return clone;
}


Statement* TypeDecl::Visit(NodeVisitor* v) {
  return v->VisitTypeDecl(this);
}


void TypeDecl::VisitChildren(NodeVisitor *v) {
  VisitTname(v);
}


// ------------------------------------------------------------------------------
// Implementation of VarDecl

VarDecl* VarDecl::New(Proc* proc, FileLine* fileline, szl_string name,
           Type* type, Function* owner, int level, bool is_param, Expr* init) {
  VarDecl* d = NEW(proc, VarDecl);
  d->Initialize(fileline, name, type, owner, level, is_param, init);
  return d;
}


void VarDecl::Initialize(FileLine* fileline, szl_string name, Type* type,
                       Function* owner, int level, bool is_param, Expr* init) {
  Decl::Initialize(fileline);
  object()->Initialize(name);
  // prevent crashes
  type_ = (type != NULL) ? type : SymbolTable::incomplete_type();
  offset_ = 0;
  owner_ = owner;
  level_ = level;
  is_param_ = is_param;
  init_ = init;
  tuple_ = NULL;
  trapinfo_index_ = -1;
  modified_after_init_ = false;
  modified_at_call_ = false;
  assert(is_local() || !is_param);
}


void VarDecl::set_type(Type* type) {
  // output variables must be global and are implicitly static
  assert(!type->is_output() || (is_static() && !is_param()));
  type_ = type;
}


void VarDecl::UsesTrapinfoIndex(Proc* proc) {
  if (trapinfo_index_ < 0)
    trapinfo_index_ = proc->AllocateVarTrapinfoIndex();
}


VarDecl* VarDecl::Clone(CloneMap* cmap) {
  // Use a cloned VarDecl if one exists, otherwise do not clone it here.
  VarDecl* clone = cmap->Find(this);
  if (clone == NULL)
    clone = this;
  return clone;
}


VarDecl* VarDecl::CloneStmt(CloneMap* cmap) {
  if (is_static()) {
    // Do not clone statics; use the original.
    // (This has the potential to confuse code that inspects "owner_".)
    cmap->Insert(this, this);
    return this;
  }
  return AlwaysCloneStmt(cmap);
}


VarDecl* VarDecl::AlwaysCloneStmt(CloneMap* cmap) {
  assert(cmap->Find(this) == NULL);
  assert(tuple_ == NULL);
  assert(offset_ == 0);
  Function* owner = cmap->Find(owner_);
  if (owner == NULL)
    owner = cmap->context();
  // Take care to enter the clone in cmap before we clone the
  // initializer because it may refer to this declaration.
  VarDecl* clone = New(cmap->proc(), clone_fl(cmap), name(), type_, owner,
                       owner->level(), is_param_, NULL);
  clone->modified_after_init_ = modified_after_init_;
  clone->modified_at_call_ = modified_at_call_;
  if (trapinfo_index_ >= 0)
    clone->UsesTrapinfoIndex(cmap->proc());
  cmap->Insert(this, clone);
  clone->init_ = cmap->CloneOrNull(init_);
  return clone;
}


VarDecl* VarDecl::Visit(NodeVisitor* v) {
  return v->VisitVarDecl(this);
}


void VarDecl::VisitChildren(NodeVisitor *v) {
  // note that the variable is not visited
  VisitInit(v);
}


// ------------------------------------------------------------------------------
// Implementation of QuantVarDecl

QuantVarDecl* QuantVarDecl::New(Proc* proc, FileLine* fileline, szl_string name,
                           Type* type, Function* owner, int level, Kind kind) {
  QuantVarDecl* d = NEW(proc, QuantVarDecl);
  d->Initialize(fileline, name, type, owner, level, kind);
  return d;
}


void QuantVarDecl::Initialize(FileLine* fileline, szl_string name, Type* type,
                       Function* owner, int level, Kind kind) {
  VarDecl::Initialize(fileline, name, type, owner, level, false, NULL);
  kind_ = kind;
}


const char* QuantVarDecl::KindAsString() const {
  static const char* const names[] = { "all", "each", "some" };
  assert(kind_ >= 0 && kind_ < ARRAYSIZE(names));
  return names[kind_];
}


QuantVarDecl* QuantVarDecl::Clone(CloneMap* cmap) {
  // Use a cloned QuantVarDecl if one exists, otherwise do not clone it here.
  QuantVarDecl* clone = cmap->Find(this);
  if (clone == NULL)
    clone = this;
  return clone;
}


QuantVarDecl* QuantVarDecl::CloneStmt(CloneMap* cmap) {
  // See VarDecl::Clone.  Not quite worth sharing the common code.
  QuantVarDecl* clone = cmap->Find(this);
  if (clone == NULL) {
    assert(tuple() == NULL);
    assert(offset() == 0);
    clone = New(cmap->proc(), clone_fl(cmap), name(), type(), owner(),
                level(), kind_);
    if (init() != NULL)
      clone->set_init(init()->Clone(cmap));
    if (modified_after_init())
      clone->set_modified_after_init();
    if (modified_at_call())
      clone->set_modified_at_call();
    if (trapinfo_index_ >= 0)
      clone->UsesTrapinfoIndex(cmap->proc());
    cmap->Insert(this, clone);
  }
  return clone;
}


// ------------------------------------------------------------------------------
// Implementation of BreakableStatement

void BreakableStatement::Initialize(Proc* proc, FileLine* fileline) {
  Statement::Initialize(fileline);
  exit_ = NULL;
}


// ------------------------------------------------------------------------------
// Implementation of Empty

Empty* Empty::New(Proc* proc, FileLine* fileline) {
  Empty* e = NEW(proc, Empty);
  e->Statement::Initialize(fileline);
  return e;
}


Empty* Empty::CloneStmt(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap));
}


Statement* Empty::Visit(NodeVisitor* v) {
  return v->VisitEmpty(this);
}


// ------------------------------------------------------------------------------
// Implementation of ExprStat

ExprStat* ExprStat::New(Proc* proc, FileLine* fileline, Expr* expr) {
  ExprStat* e = NEW(proc, ExprStat);
  e->Statement::Initialize(fileline);
  e->expr_ = expr;
  assert(expr != NULL);
  return e;
}


ExprStat* ExprStat::CloneStmt(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap), expr_->Clone(cmap));
}


Statement* ExprStat::Visit(NodeVisitor* v) {
  return v->VisitExprStat(this);
}


void ExprStat::VisitChildren(NodeVisitor *v) {
  VisitExpr(v);
}


// ------------------------------------------------------------------------------
// Implementation of If

If* If::New(Proc* proc, FileLine* fileline, Expr* cond, Statement* then_part, Statement* else_part) {
  If* n = NEW(proc, If);
  n->Statement::Initialize(fileline);
  n->cond_ = cond;
  n->then_part_ = then_part;
  n->else_part_ = else_part;
  return n;
}


If* If::CloneStmt(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap), cond_->Clone(cmap),
             then_part_->Clone(cmap), cmap->CloneOrNull(else_part_));
}


Statement* If::Visit(NodeVisitor* v) {
  return v->VisitIf(this);
}


void If::VisitChildren(NodeVisitor *v) {
  VisitCond(v);
  VisitThen(v);
  VisitElse(v);
}


// ------------------------------------------------------------------------------
// Implementation of Loop

Loop* Loop::New(Proc* proc, FileLine* fileline, int sym) {
  assert(sym == DO || sym == FOR || sym == WHILE);
  Loop* l = NEW(proc, Loop);
  l->BreakableStatement::Initialize(proc, fileline);
  l->sym_ = sym;
  l->before_ = NULL;
  l->cond_ = NULL;
  l->after_ = NULL;
  l->body_ = NULL;
  l->cont_ = NULL;
  return l;
}


Loop* Loop::CloneStmt(CloneMap* cmap) {
  assert(cont_ == NULL);
  Loop* clone = New(cmap->proc(), clone_fl(cmap), sym_);
  // for Break and Continue; must be done before body cloned
  cmap->Insert(this, clone);
  if (before_ != NULL)
    clone->set_before(before_->Clone(cmap));
  if (cond_ != NULL)
    clone->set_cond(cond_->Clone(cmap));
  if (after_ != NULL)
    clone->set_after(after_->Clone(cmap));
  clone->set_body(body_->Clone(cmap));
  return clone;
}


Statement* Loop::Visit(NodeVisitor* v) {
  return v->VisitLoop(this);
}


void Loop::VisitChildren(NodeVisitor *v) {
  VisitBefore(v);
  VisitCond(v);
  VisitAfter(v);
  VisitBody(v);
}


// ------------------------------------------------------------------------------
// Implementation of Break

Break* Break::New(Proc* proc, FileLine* fileline, BreakableStatement* stat) {
  Break* b = NEW(proc, Break);
  b->Statement::Initialize(fileline);
  b->stat_ = stat;
  return b;
}


Break* Break::CloneStmt(CloneMap* cmap) {
  // Use a cloned BreakableStatement if one exists, otherwise do not clone it.
  BreakableStatement* stat = cmap->Find(stat_);
  if (stat == NULL)
    stat = stat_;
  return New(cmap->proc(), clone_fl(cmap), stat);
}


Statement* Break::Visit(NodeVisitor* v) {
  return v->VisitBreak(this);
}


// ------------------------------------------------------------------------------
// Implementation of Continue

Continue* Continue::New(Proc* proc, FileLine* fileline, Loop* loop) {
  Continue* c = NEW(proc, Continue);
  c->Statement::Initialize(fileline);
  c->loop_ = loop;
  return c;
}


Continue* Continue::CloneStmt(CloneMap* cmap) {
  // Use a cloned Loop if one exists, otherwise do not clone it.
  Loop* loop = cmap->Find(loop_);
  if (loop == NULL)
    loop = loop_;
  return New(cmap->proc(), clone_fl(cmap), loop);
}


Statement* Continue::Visit(NodeVisitor* v) {
  return v->VisitContinue(this);
}


// ------------------------------------------------------------------------------
// Implementation of When

When* When::New(Proc* proc, FileLine* fileline, Scope* qvars, Expr* cond, Statement* body) {
  When* w = NEW(proc, When);
  w->Statement::Initialize(fileline);
  w->qvars_ = qvars;
  w->cond_ = cond;
  w->body_ = body;
  w->rewritten_ = NULL;
  w->error_ = NULL;
  return w;
}


When* When::CloneStmt(CloneMap* cmap) {
  // Should we clone the pre-rewrite version?
  // Could we just use the original copy of qvars_, cond_ and body_ ?
  // If we were to clone it, we would have to make two versions of qvars, one
  // with only QuantVarDecls (for the non-rewritten) and one with the added
  // temps (for the rewritten).  The non-rewritten QuantVarDecls are not in
  // any block, and so would have to be cloned explicitly with CloneStmt.
  // For now we do not clone the pre-rewrite version, assuming it is only
  // used for --print_source and for printing function bodies.
  assert(rewritten_ != NULL);
  When* clone =  When::New(cmap->proc(), clone_fl(cmap), NULL, cond_, body_);
  clone->rewritten_ = rewritten_->Clone(cmap);
  clone->qvars_ = Scope::New(cmap->proc());
  Scope::Clone(cmap, qvars_, clone->qvars_);
  return clone;
}


Statement* When::Visit(NodeVisitor* v) {
  return v->VisitWhen(this);
}


void When::VisitChildren(NodeVisitor *v) {
  // Note that the variables are not visited
  VisitCond(v);
  VisitRewritten(v);
}


void When::rewrite(Proc* proc, Function* owner, int level) {
  if (FLAGS_debug_whens)
    F.print("before rewriting:\n%N", this);
  WhenAnalyzer wa(proc, this, owner, level);
  rewritten_ = wa.Analyze();
  if (wa.error())
    error_ = proc->CopyString(wa.error());
  if (FLAGS_debug_whens)
    F.print("after rewriting:\n%N", rewritten_);
}


// ------------------------------------------------------------------------------
// Implementation of Block

Block* Block::New(Proc* proc, FileLine* fileline, Scope* scope, bool is_program) {
  Block* b = NEWP(proc, Block);
  b->Statement::Initialize(fileline);
  b->scope_ = scope;
  b->is_program_ = is_program;
  return b;
}


void Block::Append(Statement* stat) {
  assert(stat != NULL);
  list_.Append(stat);
}


Statement* Block::at(int i) const {
  return list_[i];
}


int Block::length() const {
  return list_.length();
}


Block* Block::Visit(NodeVisitor* v) {
  return v->VisitBlock(this);
}


Block* Block::CloneStmt(CloneMap* cmap) {
  // Defer cloning the scope until all nested blocks have been cloned
  // because scopes are shared and we have to wait until all the declarations
  // in the scope have been cloned.  If an enclosed blocks shares this scope,
  // do not clone at all; let the enclosed block do it.
  // TODO: consider the issue of cloning a block that shares a scope
  // with an enclosing block.
  Scope* scope = NULL;
  bool clone_scope = false;
  if (scope_ != NULL) {
    scope = cmap->Find(scope_);
    if (scope == NULL) {
      scope = scope_->New(cmap->proc());
      cmap->Insert(scope_, scope);
      clone_scope = true;
    }
  }
  Block* clone = New(cmap->proc(), clone_fl(cmap), scope, is_program_);
  cmap->CloneList(&list_, &clone->list_);
  if (clone_scope)
    Scope::Clone(cmap, scope_, scope);
  return clone;
}


void Block::VisitChildren(NodeVisitor *v) {
  VisitList(v);
}


// ------------------------------------------------------------------------------
// Implementation of Proto

Proto* Proto::New(Proc* proc, FileLine* fileline,
                                const char* file) {
  Proto* p = NEWP(proc, Proto);
  p->Statement::Initialize(fileline);
  p->file_ = file;
  return p;
}


void Proto::Append(Statement* stat) {
  assert(stat != NULL);
  list_.Append(stat);
}


Statement* Proto::at(int i) const {
  return list_[i];
}


int Proto::length() const {
  return list_.length();
}


Proto* Proto::CloneStmt(CloneMap* cmap) {
  Proto* clone = New(cmap->proc(), clone_fl(cmap), file_);
  cmap->CloneList(&list_, &clone->list_);
  return clone;
}


Proto* Proto::Visit(NodeVisitor* v) {
  return v->VisitProto(this);
}


void Proto::VisitChildren(NodeVisitor *v) {
  VisitList(v);
}


// ------------------------------------------------------------------------------
// Implementation of Assignment

Assignment* Assignment::New(Proc* proc, FileLine* fileline, Expr* lvalue, Expr* rvalue) {
  Assignment* a = NEW(proc, Assignment);
  a->Statement::Initialize(fileline);
  a->lvalue_ = lvalue;
  a->rvalue_ = rvalue;
  // For variables on the LHS, set modified_after_init and is_lvalue here
  // for convenience since Assignment nodes are created in "when" rewriting.
  Variable *lvar = IR::RootVar(lvalue);
  if (lvar != NULL) {
    lvar->var_decl()->set_modified_after_init();
    lvar->set_is_lvalue();
    if (lvar == lvalue)
      lvar->clear_is_rvalue();
  }
  return a;
}


Assignment* Assignment::CloneStmt(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap),
             lvalue_->Clone(cmap), rvalue_->Clone(cmap));
}


bool Assignment::is_dead() const {
  // assignment to an unread tuple field is dead
  Selector* selector = lvalue_->AsSelector();
  return (selector != NULL && !selector->field()->read());
}


Expr* Assignment::selector_var() const {
  assert(is_dead());
  // the non-dead part of the LHS, which should be evaluated for side effects
  Selector* selector = lvalue_->AsSelector();
  return selector->var();
}


Statement* Assignment::Visit(NodeVisitor* v) {
  return v->VisitAssignment(this);
}


void Assignment::VisitChildren(NodeVisitor *v) {
  VisitLvalue(v);
  VisitRvalue(v);
}


// ------------------------------------------------------------------------------
// Implementation of Emit

Emit* Emit::New(Proc* proc, FileLine* fileline, Expr* output,
                List<VarDecl*>* index_decls, VarDecl* elem_decl,
                List<Expr*>* indices, Expr* value, Expr* weight,
                Expr* index_format, Expr* elem_format) {
  Emit* e = NEW(proc, Emit);
  e->Statement::Initialize(fileline);
  e->output_ = output;
  e->index_decls_ = index_decls;
  e->elem_decl_ = elem_decl;
  e->indices_ = indices;
  e->value_ = value;
  e->weight_ = weight;
  e->index_format_ = index_format;
  e->elem_format_ = elem_format;
  return e;
}


Emit* Emit::CloneStmt(CloneMap* cmap) {
  // Clone element decl and index decls before their uses
  List<VarDecl*>* index_decls = cmap->AlwaysCloneStmtList(index_decls_);
  VarDecl* elem_decl = elem_decl_->AlwaysCloneStmt(cmap);
  return New(cmap->proc(), clone_fl(cmap), output_->Clone(cmap),
             index_decls, elem_decl, cmap->CloneList(indices_),
             value_->Clone(cmap), cmap->CloneOrNull(weight_),
             cmap->CloneOrNull(index_format_),
             cmap->CloneOrNull(elem_format_));
}


Statement* Emit::Visit(NodeVisitor* v) {
  return v->VisitEmit(this);
}


void Emit::VisitChildren(NodeVisitor *v) {
  // note that the variable is not visited
  VisitOutput(v);
  VisitIndices(v);
  VisitValue(v);
  VisitWeight(v);
  VisitIndexFormat(v);
  VisitElemFormat(v);
}


// ------------------------------------------------------------------------------
// Implementation of Increment

Increment* Increment::New(Proc* proc, FileLine* fileline, Expr* lvalue, int delta) {
  Increment* n = NEW(proc, Increment);
  n->Statement::Initialize(fileline);
  n->lvalue_ = lvalue;
  n->delta_ = delta;
  assert((int8)delta == delta);  // must fit into 8bits for now
  return n;
}


Increment* Increment::CloneStmt(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap), lvalue_->Clone(cmap), delta_);
}


Statement* Increment::Visit(NodeVisitor* v) {
  return v->VisitIncrement(this);
}


void Increment::VisitChildren(NodeVisitor *v) {
  VisitLvalue(v);
}


// ------------------------------------------------------------------------------
// Implementation of Switch

Case* Case::New(Proc* proc, List<Expr*>* labels, Statement* stat) {
  Case* c = NEW(proc, Case);
  c->labels_ = labels;
  c->stat_ = stat;
  assert(labels->length() > 0);  // must have at least one label
  assert(stat != NULL);
  return c;
}


Case* Case::Clone(CloneMap* cmap) {
  return New(cmap->proc(), cmap->CloneList(labels_), stat_->Clone(cmap));
}


Switch* Switch::New(Proc* proc, FileLine* fileline, Expr* tag) {
  Switch* s = NEW(proc, Switch);
  s->BreakableStatement::Initialize(proc, fileline),
  s->tag_ = tag;
  assert(tag != NULL);
  return s;
}


void Switch::set_cases(List<Case*>* cases, Statement* default_case) {
  assert(default_case != NULL);
  cases_ = cases;
  default_case_ = default_case;
}


Switch* Switch::CloneStmt(CloneMap* cmap) {
  Switch* clone = New(cmap->proc(), clone_fl(cmap), tag_->Clone(cmap));
  cmap->Insert(this, clone);  // for Break; must be done before cases cloned
  clone->set_cases(cmap->CloneList(cases_), default_case_->Clone(cmap));
  return clone;
}


Statement* Switch::Visit(NodeVisitor* v) {
  return v->VisitSwitch(this);
}


void Switch::VisitChildren(NodeVisitor *v) {
  VisitTag(v);
  VisitCaseLabels(v);
  VisitCaseStatements(v);
  VisitDefaultCase(v);
}


// ------------------------------------------------------------------------------
// Implementation of Result

Result* Result::New(Proc* proc, FileLine* fileline, StatExpr* statexpr, Variable* var, Expr* expr) {
  Result* r = NEW(proc, Result);
  r->Statement::Initialize(fileline);
  r->statexpr_ = statexpr;
  r->expr_ = expr;
  r->var_ = var;
  return r;
}


Result* Result::CloneStmt(CloneMap* cmap) {
  // Use a cloned StatExpr if one exists, otherwise do not clone it.
  StatExpr* statexpr = cmap->Find(statexpr_);
  if (statexpr == NULL)
    statexpr = statexpr_;
  return New(cmap->proc(), clone_fl(cmap), statexpr, var_->Clone(cmap),
             expr_->Clone(cmap));
}


Statement* Result::Visit(NodeVisitor* v) {
  return v->VisitResult(this);
}


void Result::VisitChildren(NodeVisitor *v) {
  VisitExpr(v);
}


// ------------------------------------------------------------------------------
// Implementation of Return

Return* Return::New(Proc* proc, FileLine* fileline, Expr* result) {
  Return* r = NEW(proc, Return);
  r->Statement::Initialize(fileline);
  r->result_ = result;
  return r;
}


Return* Return::CloneStmt(CloneMap* cmap) {
  return New(cmap->proc(), clone_fl(cmap), cmap->CloneOrNull(result_));
}


Statement* Return::Visit(NodeVisitor* v) {
  return v->VisitReturn(this);
}


void Return::VisitChildren(NodeVisitor *v) {
  VisitResult(v);
}


// ------------------------------------------------------------------------------
// Implementation of Variable

Variable* Variable::New(Proc* proc, FileLine* fileline, VarDecl* var_decl) {
  Variable* v = NEW(proc, Variable);
  v->Initialize(proc, fileline, var_decl);
  return v;
}


void Variable::Initialize(Proc* proc, FileLine* fileline, VarDecl* var_decl) {
  Node::Initialize(fileline);
  var_decl_ = var_decl;
  is_lvalue_ = false;  // defaults
  is_rvalue_ = true;
  is_defined_ = false;
  subst_visited_ = false;
}


Variable* Variable::Clone(CloneMap* cmap) {
  // Use a cloned VarDecl if one exists, otherwise do not clone it.
  VarDecl* vardecl_clone = cmap->Find(var_decl_);
  if (vardecl_clone == NULL)
    vardecl_clone = var_decl_;
  Variable* clone = New(cmap->proc(), clone_fl(cmap), vardecl_clone);
  clone->is_lvalue_ = is_lvalue_;
  clone->is_rvalue_ = is_rvalue_;
  return clone;
}


Expr* Variable::Visit(NodeVisitor* v) {
  return v->VisitVariable(this);
}


// ------------------------------------------------------------------------------
// Implementation of TempVariable

TempVariable* TempVariable::New(Proc* proc, VarDecl* var_decl, Expr* init) {
  TempVariable* v = NEW(proc, TempVariable);
  v->Variable::Initialize(proc, init->file_line(), var_decl);
  v->init_ = init;
  v->init_generated_ = false;
  return v;
}


TempVariable* TempVariable::Clone(CloneMap* cmap) {
  TempVariable* clone = New(cmap->proc(), var_decl_->Clone(cmap),
                            init_->Clone(cmap));
  clone->is_lvalue_ = is_lvalue_;
  clone->is_rvalue_ = is_rvalue_;
  assert(!clone->init_generated_);
  return clone;
}


Expr* TempVariable::Visit(NodeVisitor* v) {
  return v->VisitTempVariable(this);
}


void TempVariable::VisitChildren(NodeVisitor *v) {
  VisitInit(v);
}


// ------------------------------------------------------------------------------
// Implementation of Field

Field* Field::New(Proc* proc, FileLine* fileline, szl_string name, Type* type) {
  Field* f = NEW(proc, Field);
  f->Node::Initialize(fileline);
  f->object()->Initialize(name);
  f->type_ = type;
  f->slot_index_ = -1;  // indicates not yet assigned
  f->value_ = NULL;
  f->tag_ = 0;
  f->pb_type_ = PBTYPE_UNKNOWN;
  f->read_ = false;
  // if the type is unfinished, this field must be contained within the
  // definition of the type and so this is a recursive reference
  f->recursive_ = !type->is_finished();
  assert(type != NULL);
  return f;
}


bool Field::read() const {
  return FLAGS_read_all_fields || read_;
}


// The only place a Field without a Selector appears in expressions is in
// the index and element format argument lists of an output type and in
// an Emit node, where those lists are cloned.  Do not clone the field
// unless we are cloning the output type that contains it, and in that case
// it will aready be in the map.
Field* Field::Clone(CloneMap* cmap) {
  Field* clone = cmap->Find(this);
  if (clone == NULL)
    clone = this;
  return clone;
}


Field* Field::Visit(NodeVisitor* v) {
  return v->VisitField(this);
}


// ------------------------------------------------------------------------------
// Implementation of Intrinsic

Intrinsic* Intrinsic::New(Proc* proc, FileLine* fileline, szl_string name,
                          FunctionType* ftype, Kind kind, CFunction function,
                          const char* doc, int attr, bool can_fail) {
  Intrinsic* n = NEW(proc, Intrinsic);
  n->Node::Initialize(fileline);
  n->object()->Initialize(name);
  n->name_ = name;
  n->ftype_ = ftype;
  n->kind_ = kind;
  n->function_ = function;
  n->object_.set_doc(doc);
  n->attr_ = attr;
  n->can_fail_ = can_fail;
  assert(name != NULL);
  assert(ftype != NULL);
  assert(function != NULL);
  return n;
}


Expr* Intrinsic::Visit(NodeVisitor* v) {
  return v->VisitIntrinsic(this);
}


bool Intrinsic::add_overload(Intrinsic* fun) {
  assert(fun != NULL);
  // names must be the same for overloaded functions
  assert(strcmp(this->name(), fun->name()) == 0);

  // special intrinsics cannot be overloaded
  if (fun->kind() != Intrinsic::INTRINSIC)
    return false;

  Intrinsic* prev = this;
  for (Intrinsic* curr = this;
       curr != NULL;
       prev = curr, curr = curr->next_overload_) {
    if (fun->ftype()->IsEqualParameters(curr->ftype(), false)) {
      // if an overload of the same type exists, then cannot overload again
      return false;
    }
  }
  // reached end of list; no matching signature, so overload allowed.
  prev->next_overload_ = fun;
  return true;
}


Intrinsic* Intrinsic::next_overload() {
  return next_overload_;
}


// ------------------------------------------------------------------------------
// Implementation of TypeName

TypeName* TypeName::New(Proc* proc, FileLine* fileline, szl_string name) {
  TypeName* t = NEW(proc, TypeName);
  t->Node::Initialize(fileline);
  t->object()->Initialize(name);
  t->type_ = SymbolTable::incomplete_type();
  assert(name != NULL);
  return t;
}


void TypeName::set_type(Type* type) {
  assert(type != NULL);
  type_ = type;
  // associate type with a type name if possible
  if (type->type_name() == NULL)
    type->set_type_name(this);
}


TypeName* TypeName::Clone(CloneMap* cmap) {
  // Use a cloned TypeName if one exists, otherwise do not clone it here.
  TypeName* clone = cmap->Find(this);
  if (clone == NULL)
    clone = this;
  return clone;
}


TypeName* TypeName::CloneStmt(CloneMap* cmap) {
  TypeName* clone = cmap->Find(this);
  if (clone == NULL) {
    clone = New(cmap->proc(), clone_fl(cmap), name());
    clone->set_type(type_);
    cmap->Insert(this, clone);
  }
  return clone;
}


TypeName* TypeName::Visit(NodeVisitor* v) {
  return v->VisitTypeName(this);
}


// -----------------------------------------------------------------------------
// Implementation of DeepNodeVisitor

void DeepNodeVisitor::DoTypeDecl(TypeDecl* x) {
  x->VisitChildren(this);
  // Visit the type in case it contains a tuple with a variable declaration.
  x->tname()->type()->Visit(this);
}


void DeepNodeVisitor::DoVarDecl(VarDecl* x) {
  x->VisitChildren(this);
  // Visit output types to reach expressions in param.
  OutputType* output_type = x->type()->as_output();
  if (output_type != NULL) {
    output_type->Visit(this);
  }
}


void DeepNodeVisitor::DoOutputType(OutputType* x) {
  x->VisitChildren(this);

  // Visit expressions in the parameter.
  if (x->param() != NULL) {
    x->param()->Visit(this);
  }
}


void DeepNodeVisitor::DoTupleType(
    TupleType* x) {
  x->VisitChildren(this);

  // Invoke the node visitor on initializers of static VarDecls.
  for (int i = 0; i < x->scope()->num_entries(); i++) {
    Object* obj = x->scope()->entry_at(i);
    if (obj->AsVarDecl() != NULL) {
      obj->AsVarDecl()->Visit(this);
    } else if (obj->AsTypeName() != NULL) {
      obj->AsTypeName()->type()->Visit(this);
    }
  }
}


}  // namespace sawzall
