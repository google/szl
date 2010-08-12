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
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/scanner.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/when.h"
#include "engine/proc.h"


namespace sawzall {


// Helper routine to extract the expression (usually a variable) that is being
// indexed.  In a[i], it will be 'a'.  Returns NULL if it's not an index expression.
Expr* WhenAnalyzer::VarOf(Expr* expr) {
  CloneMap cmap(proc_, NULL, owner_, expr->file_line());
  if (expr->AsIndex() != NULL)
    return expr->AsIndex()->var()->Clone(&cmap);
  if (expr->AsSlice() != NULL)
    return expr->AsSlice()->var()->Clone(&cmap);
  return NULL;
}

Type* WhenAnalyzer::TypeOfVarOf(Expr* expr) {
  if (expr->AsIndex() != NULL)
    return expr->AsIndex()->var()->type();
  if (expr->AsSlice() != NULL)
    return expr->AsSlice()->var()->type();
  ShouldNotReachHere();
  return NULL;
}

// Helper routine: Is this expression composed entirely of def() calls?
// If so, it must be of the form def() or a compound expression involving
// only &&, ||, def calls, and == true or == false operation, that is,
// logical combinations of def calls.
bool WhenAnalyzer::AllDefs(Expr* cond) {
  Call* call = cond->AsCall();
  if (call != NULL &&
      call->fun()->AsIntrinsic() != NULL &&
      call->fun()->AsIntrinsic()->kind() == Intrinsic::DEF) {
    return true;
  }
  Binary* binary = cond->AsBinary();
  if (binary != NULL) {
    if (binary->op() == Binary::LAND ||
        binary->op() == Binary::LOR ||
        binary->op() == Binary::EQL) {
      return AllDefs(binary->left()) && AllDefs(binary->right());
    }
  }
  Literal* literal = cond->AsLiteral();
  if (literal != NULL && literal->type()->IsEqual(SymbolTable::bool_type(), false)) {
    return true;
  }
  return false;
}


// A QuantScanner walks a tree looking to see if any quantifiers appear
// within the expression.

class QuantScanner: public NodeVisitor {
 public:
  // Creation
  QuantScanner(Proc* proc) : proc_(proc) {}

  List<QuantVarDecl*>* all_quants(Expr* x) {
    all_quants_ = List<QuantVarDecl*>::New(proc_);
    Check(x);
    return all_quants_;
  }

  // expressions
  virtual void DoExpr(Expr* x)  { /* nothing to do */ }
  virtual void DoBinary(Binary* x)  { Check(x->left()); Check(x->right()); }
  virtual void DoComposite(Composite* x)  { CheckExprList(x->list()); }
  virtual void DoConversion(Conversion* x)  { Check(x->src()); CheckExprList(x->params()); }
  virtual void DoDollar(Dollar* x)  { Check(x->array()); }
  virtual void DoSelector(Selector* x)  { Check(x->var()); }
  virtual void DoRuntimeGuard(RuntimeGuard* x)  { Check(x->expr()); }
  virtual void DoIndex(Index* x)  { Check(x->var()); Check(x->index()); }
  virtual void DoNew(New* x)  { Check(x->length()); Check(x->init()); }
  virtual void DoSaw(Saw* x)  { CheckExprList(x->args()); }
  virtual void DoSlice(Slice* x)  { Check(x->var()); Check(x->beg()); Check(x->end()); }
  virtual void DoLiteral(Literal* x)  { /* always static => nothing to do */ }
  virtual void DoVariable(Variable* x)  { if (x->var_decl()->AsQuantVarDecl() != NULL) all_quants_->Append(x->var_decl()->AsQuantVarDecl()); }

  virtual void DoCall(Call* x) {
    Check(x->fun());
    CheckExprList(x->args());
  }

  // statements
  virtual void DoStatement(Statement* x)  { ShouldNotReachHere(); }

 private:
  Proc* proc_;  // the process to use for allocation
  List<QuantVarDecl*>* all_quants_;

  void Check(Expr* x) {
    x->Visit(this);
  }

  void CheckExprList(const List<Expr*>* x) {
    for (int i = 0; i < x->length(); i++)
      Check(x->at(i));
  }
};


// An ExprAnalyzer walks a tree discovering constraints that may be
// used to convert a when's conditional expression into the pieces necessary
// to construct a for loop.  It generates a set of 'uses' of quantifier
// variables that are candidates for those constraints.

class ExprAnalyzer: public NodeVisitor {
 public:
  ExprAnalyzer(Proc* proc, Expr* cond);
  virtual ~ExprAnalyzer();
  const char* error() const { return error_; }
  const bool needs_def() const { return needs_def_; }

  List<Expr*>* Uses(QuantVarDecl* quant) {
    quant_ = quant;
    uses_ = List<Expr*>::New(proc_);
    Analyze(cond_);
    return uses_;
  }

  // expressions
  virtual void DoExpr(Expr* x)  { Failure(x); }
  virtual void DoBinary(Binary* x);
  virtual void DoCall(Call* x);
  virtual void DoComposite(Composite* x);
  virtual void DoConversion(Conversion* x);
  virtual void DoDollar(Dollar* x)  { /* nothing to do */ }
  virtual void DoSelector(Selector* x);
  virtual void DoRuntimeGuard(RuntimeGuard* x);
  virtual void DoIndex(Index* x);
  virtual void DoNew(New* x);
  virtual void DoRegex(Regex* x);
  virtual void DoSaw(Saw* x);
  virtual void DoSlice(Slice* x);
  virtual void DoIntrinsic(Intrinsic* x);
  virtual void DoLiteral(Literal* x)  { /* nothing to do */ }
  virtual void DoTypeName(TypeName* x)  { /* nothing to do */ }
  virtual void DoVariable(Variable* x);

  // Statements appear in StatExpr blocks and most can be handled generically here.
  // Even Assignment is OK because the parser prevents assignment to quantifiers.
  virtual void DoNode(Node* x) { x->VisitChildren(this); }
  virtual void DoVarDecl(VarDecl* x);

 private:
  Proc* proc_;  // the process to use for allocation
  Expr* cond_;  // the (when) condition we are analyzing
  QuantVarDecl* quant_;  // the quantifier we are looking for
  List<Expr*>* uses_;  // the list of expressions using quant_
  List<VarDecl*>* locals_;  // variables declared within a ?{} expression
  const char* error_;
  bool needs_def_;  // condition is unsafe; protect with def()

  void Analyze(Node* x)  { x->Visit(this); }
  void AnalyzeIndex(Expr* expr, Expr* index);
  void Collect(Expr* x)  { uses_->Append(x); }
  void Failure(Node *x)  { error_ = proc_->PrintString("%N unexpected in 'when' analysis", x); };
};


ExprAnalyzer::ExprAnalyzer(Proc* proc, Expr* cond)
 : proc_(proc),
   cond_(cond),
   quant_(NULL),
   locals_(NULL),
   error_(NULL),
   needs_def_(false) {
  assert(cond != NULL);
}


ExprAnalyzer::~ExprAnalyzer() {
  // TODO: do something here
}


// We have found an expression, expr, of the form a[index].  The indexed
// expression 'a' has already been analyzed, the 'index' expression has not.  If
// the expressions are simple enough, 'a' and 'index' are candidates to
// constrain the when loop. Otherwise, recur on 'index' to see if it contains a
// constraining expression.
void ExprAnalyzer::AnalyzeIndex(Expr* expr, Expr* index) {
  QuantScanner scanner(proc_);
  List<QuantVarDecl*>* all_quants = scanner.all_quants(index);
  switch (all_quants->length()) {
    case 0:
      return;
    case 1:
      if (all_quants->at(0) != quant_)  // not the one of interest
        return;
      // If it's a nested array or map index, recur to reach the innermost.  Consider
      // an expression like "a[b[c[i]]]"; it's "i" we care about, and "c" whose range
      // limits the loop.  However, b[c[i]] could be out of range for a valid value of i,
      // so we also need to protect the expression with a 'def'.
      if (index->AsIndex() != NULL) {
        needs_def_ = true;
        DoIndex(index->AsIndex());
        return;
      }
      // It must be a simple variable reference or array index
      if (index->AsVariable() == NULL)
        break;
      Collect(expr);
      return;
    default:
      break;
  }

  // No luck with this index, but maybe it contains a useful constraint; recur.
  Analyze(index);
  // Two possibilities.
  // 1) Further analysis uncovered nothing (uses_->length() is unchanged.)
  // It doesn't help us but another constraint on this variable might work.
  // Consider
  //    when(i: all int; word[i] == word[$-1-i])
  // The second expression (word[$-1-i]) is too hard and does not generate a
  // use but but the first is fine. We just need to guarantee this expression
  // won't trigger an out-of-bounds, so wrap the condition with a def check.
  // 2) The index contains a useful expression.  However, it may be complex
  // enough to cause out-of-bound errors (consider a[f(b[i])]; the recursion
  // will find b[i] but a[f(b[i])] may be out of bounds) so we need
  // to def-protect its evaluation.
  // Conclusion: mark this condition for def-checking and move on. If no other
  // condition constrains this quantifier, we'll discover that when we're done.
  needs_def_ = true;
}


void ExprAnalyzer::DoSlice(Slice* x) {
  Analyze(x->var());
  AnalyzeIndex(x, x->beg());
  AnalyzeIndex(x, x->end());
}


void ExprAnalyzer::DoSelector(Selector* x) {
  Analyze(x->var());
}


void ExprAnalyzer::DoRuntimeGuard(RuntimeGuard* x) {
  Analyze(x->expr());
}


void ExprAnalyzer::DoIndex(Index* x) {
  // Ignore expressions that index variables declared in the expression.
  // Example: ignore 'a' in when(i; some int; ?{a: array of int ...; a[i] ...})
  bool local = false;
  if (locals_ != NULL && x->var()->AsVariable() != NULL) {
    int i = locals_->IndexOf(x->var()->AsVariable()->var_decl());
    if (i >= 0)
      local = true;
  }
  if (local) {
    Analyze(x->index());  // The 'i' in a[i] might still be valuable
  } else {
    Analyze(x->var());
    AnalyzeIndex(x, x->index());
  }
}


void ExprAnalyzer::DoNew(New* x) {
  if (x->length() != NULL)
    Analyze(x->length());
  if (x->init() != NULL)
    Analyze(x->init());
}


void ExprAnalyzer::DoBinary(Binary* x) {
  Analyze(x->left());
  Analyze(x->right());
}


void ExprAnalyzer::DoCall(Call* x) {
  // TODO: figure out whether DoIntrisic below was ever called;
  // it appears that it was not, because DoCall did not analyze x->fun().
  if (x->fun()->AsIntrinsic() == NULL)
    Analyze(x->fun());
  for (int i = 0; i < x->args()->length(); i++)
    Analyze((*x->args())[i]);
}


void ExprAnalyzer::DoConversion(Conversion* x) {
  Analyze(x->src());
  for (int i = 0; i < x->params()->length(); i++)
    Analyze((*x->params())[i]);
}


void ExprAnalyzer::DoRegex(Regex* x) {
  // nothing to do; argument is a type and result is a static string
}


void ExprAnalyzer::DoSaw(Saw* x) {
  Analyze(x->count());
  for (int i = 0; i < x->args()->length(); i++)
    Analyze((*x->args())[i]);
  // if there is a 'rest', we can't handle it but it doesn't parallelize anyway
  for (int i = 0; i < x->flags()->length(); i++)
    if((*x->flags())[i] == Saw::REST)
      error_ = proc_->PrintString("can't handle 'rest' keyword in 'when' analysis for %N", x);
}


void ExprAnalyzer::DoComposite(Composite* x) {
  for (int i = 0; i < x->length(); i++)
    Analyze(x->at(i));
}


void ExprAnalyzer::DoVariable(Variable* x) {
  // do nothing; AnalyzeIndex takes care of our variables
}


void ExprAnalyzer::DoIntrinsic(Intrinsic* x) {
  error_ = proc_->PrintString("unimplemented: can't handle intrinsic call in 'when' analysis for %N", x);
}


void ExprAnalyzer::DoVarDecl(VarDecl* x) {
  // Within a ?{} expression we may encounter variable declarations.
  // Keep a list of them so we know to ignore them when looking for
  // constraints.
  if (locals_ == NULL)
    locals_ = List<VarDecl*>::New(proc_);
  locals_->Append(x);
  x->VisitChildren(this);
}


// Implementation of WhenAnalyzer

WhenAnalyzer::WhenAnalyzer(Proc* proc, When* when, Function* owner, int level)
  : proc_(proc),
    when_(when),
    owner_(owner),
    level_(level),
    error_(NULL),
    namecount_(0) {
  assert(when != NULL);
  assert(owner != NULL);
}


void WhenAnalyzer::set_error(const char *error) {
  error_ = error;
  if (FLAGS_debug_whens)
    F.print("when analysis error: %s\n", error);
}


VarDecl* WhenAnalyzer::TempDecl(Scope* scope, szl_string name, Type* type, Expr* init) {
  assert(name[0] == '$');
  szl_string vname = proc_->PrintString("%s%d", name, namecount_++);
  VarDecl* var = VarDecl::New(proc_, init->file_line(), vname,
                              type, owner_, level_, false, init);
  scope->InsertOrDie(var);  // our temporary name should be unique (not really required though)
  owner_->AddLocal(var);
  return var;
}


bool WhenAnalyzer::RangeLimit(Expr* use, Expr*& min, Expr*& max) {
  Expr* var = VarOf(use);
  if (var == NULL) {
    set_error(proc_->PrintString("can't handle %N in RangeLimit in 'when' analysis", use));
    return false;
  }
  min = SymbolTable::int_0();
  List<Expr*>* args = List<Expr*>::New(proc_);
  args->Append(var);
  max = Call::New(proc_, use->file_line(), SymbolTable::universe()->LookupOrDie("len")->AsIntrinsic(), args);
  // if it's a slice, need to extend the range one more, since a[0:len(a)] is legal
  if (use->AsSlice() != NULL)
    max = Binary::New(proc_, use->file_line(), SymbolTable::int_type(), max, Binary::ADD, add_int, SymbolTable::int_1());
  return true;
}


// Count the number of quantifier appearances in the expression.
// If it's greater than one, we can't handle the expr. Consider
//    when(s: each int; a[i][i])
// To initialize the loop, we would generate
//    $combinekeys(2, a, a[i])
// but i, the quantifier, is not yet set and the generated code
// would be bogus because a[i] would be undefined.  We catch
// this situation here.  Called by CombineRange.
// TODO: Does this reject valid statements?
// TODO: This situation should be able to be handled.
int WhenAnalyzer::NumQuant(Expr* expr) {
  int n = 0;
  for (int i = 0; i < quants_->length(); i++) {
    ExprAnalyzer exanal(proc_, expr);
    n += exanal.Uses(quants_->at(i))->length();
  }
  return n;
}


// Generate the initializers for the iteration.
// We try to avoid the general key-union iteration for maps because it's more
// expensive than iterating over arrays. The logic in CombineRange identifies
// simple cases that can be done by iterating over the map itself rather than
// its key set, using CombineArrayRange in the easy cases, CombineMapRange
// in the hard ones.
bool WhenAnalyzer::CombineRange(Block* block, List<Expr*>* uses, Variable*& min, Variable*& max, Variable** key_arrayp) {
  // Do we need to generate a map-key iterator or can we just use an integer loop?
  // See comments in loop for usage.
  int nmapexpr = 0;
  int narray = 0;
  List<VarDecl*>* mapdecl = List<VarDecl*>::New(proc_);
  static const char kTooComplex[] = "implementation restriction: %N in 'when' condition too complex";
  for (int i = 0; i < uses->length(); i++) {
    Expr* var = VarOf(uses->at(i));
    if (var == NULL) {
      set_error(proc_->PrintString(kTooComplex, uses->at(i)));
      return false;
    }
    // We must decide whether we can use array-style looping or whether
    // we need to generate a unioned key set for the iteration.  This code
    // calculates several values used in the decision below.
    //   mapdecl: a list of distinct maps, made by accumulating distinct
    //             declarations (that is, m[i] counts one for the
    //             declaration of m; m[i] && m[i] creates two Variable
    //             nodes but only one VarDecl)
    //   nmapexpr: a count of map-valued expressions that cannot be reduced
    //             to a declaration.  Anything complicated causes us to use
    //             the general, key-union code. TODO: can we do better?
    //   narray:   a count of the number of non-map expressions. we do not
    //             mix maps and arrays with a single index.
    if (var->type()->is_map()) {
      // Is it a variable?
      if (var->AsVariable() != NULL) {
        VarDecl* decl = var->AsVariable()->var_decl();
        if (mapdecl->IndexOf(decl) < 0)
          mapdecl->Append(decl);
      } else {
        // not a variable
        nmapexpr++;
        if (NumQuant(uses->at(i)) > 1) {  // too hard
          set_error(proc_->PrintString(kTooComplex, uses->at(i)));
          return false;
        }
      }
    } else {
      narray++;
      if (var->type()->is_array() && var->AsVariable() == NULL) {
        if (NumQuant(uses->at(i)) > 1) {  // too hard
          set_error(proc_->PrintString(kTooComplex, uses->at(i)));
          return false;
        }
      }
    }
  }
  if ((nmapexpr == 0 &&
      (mapdecl->length() == 0 || (mapdecl->length() == 1 && narray == 0)))) {
    // We can iterate using an integer for(;;) loop in any of these cases
    // - no map expressions
    // - only one map variable is involved, and no arrays
    // - no maps at all
    *key_arrayp = NULL;
    return CombineArrayRange(block, uses, min, max);
  }
  if (narray > 0) {
    set_error(proc_->PrintString("can't handle mixed array and map access in 'when' analysis"));
    return false;
  }
  return CombineMapRange(block, uses, min, max, key_arrayp);
}


// Generate the code that invokes $combinerange to intersect the index range
// of a set of arrays or a single map.
bool WhenAnalyzer::CombineArrayRange(Block* block, List<Expr*>* uses, Variable*& min, Variable*& max) {
  // create argument list for $combinerange() internal function
  List<Expr*>* args = List<Expr*>::New(proc_);
  args->Append(Literal::NewInt(proc_, block->file_line(), NULL, uses->length())); // count is first arg
  for (int i = 0; i < uses->length(); i++) {
    Expr* min;
    Expr* max;
    if (!RangeLimit(uses->at(i), min, max))
      return false;
    args->Append(min);
    args->Append(max);
  }
  // create call
  Call* call = Call::New(proc_, block->file_line(), SymbolTable::universe()->LookupOrDie("$combinerange")->AsIntrinsic() , args);
  // after call, int is on the stack in the form (max<<32 | min); unpack it
  // FIX: THIS CODE ASSUMES POSITIVE VALUES
  VarDecl* minmax_decl = TempDecl(block->scope(), "$minmax", SymbolTable::int_type(), call);
  block->Append(minmax_decl);
  Variable* minmax;
  Expr* t;

  // extract $min = $minmax & 0x7fffffff
  minmax = Variable::New(proc_, block->file_line(), minmax_decl);
  t = Binary::New(proc_, block->file_line(), SymbolTable::int_type(), minmax, Binary::BAND, and_int, Literal::NewInt(proc_, block->file_line(), NULL, 0x7FFFFFFF));
  VarDecl* min_decl = TempDecl(block->scope(), "$min", SymbolTable::int_type(), t);
  block->Append(min_decl);
  min = Variable::New(proc_, block->file_line(), min_decl);

  // extract $max = $minmax >> 32 (unsigned shift, so no & required)
  minmax = Variable::New(proc_, block->file_line(), minmax_decl);
  t = Binary::New(proc_, block->file_line(), SymbolTable::int_type(), minmax, Binary::SHR, shr_int, Literal::NewInt(proc_, block->file_line(), NULL, 32));
  VarDecl* max_decl = TempDecl(block->scope(), "$max", SymbolTable::int_type(), t);
  block->Append(max_decl);
  max = Variable::New(proc_, block->file_line(), max_decl);

  return true;
}


// Generate the code that invokes $combinekeys to union the key set of several maps.
bool WhenAnalyzer::CombineMapRange(Block* block, List<Expr*>* uses, Variable*& min, Variable*& max, Variable** key_arrayp) {
  // create argument list for $combinekeys() internal function
  List<Expr*>* args = List<Expr*>::New(proc_);
  args->Append(Literal::NewInt(proc_, block->file_line(), NULL, uses->length())); // count is first arg
  for (int i = 0; i < uses->length(); i++)
    args->Append(VarOf(uses->at(i)));
  // create call
  Call* call = Call::New(proc_, block->file_line(), SymbolTable::universe()->LookupOrDie("$combinekeys")->AsIntrinsic() , args);

  // after call, array of key_type is on the stack
  Type* key_array_type = TypeOfVarOf(uses->at(0))->as_map()->key_array_type();
  VarDecl* key_array_decl = TempDecl(block->scope(), "$key_array", key_array_type, call);
  Variable* key_array_var = Variable::New(proc_, block->file_line(), key_array_decl);
  *key_arrayp = key_array_var;
  block->Append(key_array_decl);

  Expr* t;  // the expression assigned to the variable, also reused

  // $min = 0
  t = Literal::NewInt(proc_, block->file_line(), NULL, 0);
  VarDecl* min_decl = TempDecl(block->scope(), "$min", SymbolTable::int_type(), t);
  block->Append(min_decl);
  min = Variable::New(proc_, block->file_line(), min_decl);

  // $max = len($key_array)
  List<Expr*>* lenargs = List<Expr*>::New(proc_);
  key_array_var = Variable::New(proc_, block->file_line(), key_array_decl);
  lenargs->Append(key_array_var);
  t = Call::New(proc_, block->file_line(), SymbolTable::universe()->LookupOrDie("len")->AsIntrinsic(), lenargs);
  VarDecl* max_decl = TempDecl(block->scope(), "$max", SymbolTable::int_type(), t);
  block->Append(max_decl);
  max = Variable::New(proc_, block->file_line(), max_decl);

  return true;
}


// Declare the index variable for the loop. We can use the user's variable
// if it is an integer and if it is not used in a map.  Otherwise, we need
// to use a temporary.
VarDecl* WhenAnalyzer::DeclareIndexVar(FileLine* file_line,
                                       Block* block,
                                       QuantVarDecl* quant_decl,
                                       List<Expr*>* uses,
                                       Variable* min,
                                       Expr** map_use) {
  // See if the variable is used to index a map.
  *map_use = NULL;
  for (int i = 0; i < uses->length(); i++)
    if (TypeOfVarOf(uses->at(i))->is_map()) {
      // If there is more than one map used (such as
      //          map1[s]==1 && map2[s]==2
      // ) the code will still work because the indexes will always be in
      // alignment.  This is only true because we restrict the analysis
      // to trivial indexing expressions. In other words, in our restricted
      // scenario we can use any map to recover the key value associated with
      // the given integer index.
      *map_use = uses->at(i);
      break;
    }
  // We can use the user's variable if it's int and is not used to index a map.
  if (quant_decl->type()->is_int() && *map_use == NULL) {
    quant_decl->set_init(min);
    return quant_decl;
  } else {
    // We're not using the user's variable, but we still need to declare it.
    block->Append(quant_decl);
    return TempDecl(block->scope(), "$index", SymbolTable::int_type(), min);
  }
}


// Create the expression 'quant = $getkeybyindex(map, index)'
// or                    'quant = $key_array[index]'
Assignment* WhenAnalyzer::CreateAssignment(FileLine* file_line,
                                           Block* block,
                                           QuantVarDecl* quant_decl,
                                           Expr* map,
                                           Variable* key_array,
                                           VarDecl* index_decl) {
  Variable* quant = Variable::New(proc_, file_line, quant_decl);
  Variable* index = Variable::New(proc_, file_line, index_decl);
  Expr* rhs;
  // If we have a key_array, the quantifier is assigned by indexing an
  // array of keys; otherwise it is assigned by a call to $getkeybyindex.
  if (key_array != NULL) {
    rhs = Index::New(proc_, file_line, key_array, index, NULL);
  } else {
    List<Expr*>* args = List<Expr*>::New(proc_);
    args->Append(map);
    args->Append(index);
    Intrinsic* fun = SymbolTable::universe()->LookupOrDie("$getkeybyindex")->AsIntrinsic();
    // Create a new Intrinsic with a function type with the right return type.
    // (Parameter types are ignored for Intrinsic.)
    FunctionType* ftype = FunctionType::NewUnfinished(proc_, NULL, NULL);
    ftype->set_result(Field::New(proc_, file_line, NULL,
                          map->type()->as_map()->key_array_type()->elem_type()));
    int attr = fun->thread_safe() ? Intrinsic::kThreadSafe : Intrinsic::kNormal;
    fun = Intrinsic::New(proc_, fun->file_line(), fun->name(),
                         ftype, fun->kind(), fun->function(), NULL,
                         attr, false /* can_fail */);
    rhs = Call::New(proc_, block->file_line(), fun, args);
  }
  // TODO: use VarDecl, not Assignment, to initialize these
  // variables; then they are not initially "modified after init"
  quant_decl->set_modified_after_init();
  return Assignment::New(proc_, file_line, quant, rhs);
}


// Wrap the loop body in a for loop, possibly setting 'succeeded'.
// The various cases are discussed in the big comments introducing
// the routines below.
Block* WhenAnalyzer::CreateForLoop(FileLine* file_line,
                                   QuantVarDecl* quant_decl,
                                   List<Expr*>* uses,
                                   VarDecl* succeeded_decl,
                                   Block* body,
                                   Break* break_loop) {
  if (body == NULL)
    return NULL;
  Block* block = Block::New(proc_, file_line, qvars(), false);
  // Declare the min and max variables to hold the range for this loop
  Variable* min; // the initial value for the for loop index
  Variable* max; // the upper limit for the for loop index (exclusive)
  // Add a declaration for the combined range variable
  Variable* key_array;
  if (!CombineRange(block, uses, min, max, &key_array))
    return NULL;
  // Create some pieces for the loop
  Expr* map_use = NULL;
  VarDecl* before = DeclareIndexVar(file_line, block, quant_decl, uses, min, &map_use);
  // Declare the quantifier if it's used to index a map.
  if (map_use != NULL) {
    // There is a map reference in the code, so we need to initialize the quantifier
    // variable to $getkeybyindex(map_used, index).  Since we can't insert at the
    // beginning of a block, make 'body' a new nested block.
    Block* outer_block = Block::New(proc_, file_line, qvars(), false);
    outer_block->Append(CreateAssignment(file_line, body, quant_decl, VarOf(map_use), key_array, before));
    outer_block->Append(body);
    body = outer_block;
  }
  Variable* index = Variable::New(proc_, file_line, before);
  Expr* loopcond = Binary::New(proc_, file_line, SymbolTable::bool_type(), index, Binary::LSS, lss_int, max);
  index = Variable::New(proc_, file_line, before);
  Statement* after = Increment::New(proc_, file_line, index, 1);
  before->set_modified_after_init();
  // Generate the for loop
  Loop* forloop = Loop::New(proc_, before->file_line(), FOR);
  forloop->set_before(before);
  forloop->set_cond(loopcond);
  forloop->set_after(after);
  forloop->set_body(body);
  // Rewrite the existing break statement, if defined.
  if (break_loop)
    break_loop->set_stat(forloop);
  // If 'succeeded_decl' is set, it points to a boolean variable we use to
  // control the loop when there are multiple quantifiers and this loop
  // is not the innermost.  Do the appropriate rewriting here.
  if (succeeded_decl != NULL) {
    switch (quant_decl->kind()) {
      case QuantVarDecl::SOME: {
          // Add 'if (succeeded) break;'
          assert(break_loop == NULL);
          break_loop = Break::New(proc_, cond()->file_line(), forloop);
          Variable* succeeded = Variable::New(proc_, file_line, succeeded_decl);
          If* if_succeeded = If::New(proc_, cond()->file_line(), succeeded, break_loop, Empty::New(proc_, file_line));
          body->Append(if_succeeded);
        }
        break;
      case QuantVarDecl::EACH:
        // nothing to do
        break;
      case QuantVarDecl::ALL:
        // If we get here, we're handling an 'all' that is not innermost.
        // That requires a two-phase execution and is unimplemented.
        set_error("can't handle alls yet in complex 'when' conditions");
        return NULL;
    }
  }
  block->Append(forloop);
  return block;
}


// Create the while(true) ... break; structure for an 'all' quantifier.
// Set 'succeeded' if non-NULL.  See the comments introducing the
// routines below for more explanation.
Block* WhenAnalyzer::CreateWhileAllLoop(FileLine* file_line,
                                        VarDecl* succeeded_decl,
                                        Block* loop_body,
                                        Break* break_all) {
  Loop* whileloop = Loop::New(proc_, file_line, WHILE);
  whileloop->set_cond(SymbolTable::bool_t());
  Block* whilebody = Block::New(proc_, file_line, NULL, false);
  whilebody->Append(loop_body);
  whilebody->Append(body());
  if (succeeded_decl != NULL) {
    Variable* succeeded = Variable::New(proc_, file_line, succeeded_decl);
    whilebody->Append(Assignment::New(proc_, file_line, succeeded, SymbolTable::bool_t()));
  }
  // Do not share break nodes between different break statements, since this
  // would cause problems in the generation of line number information.
  whilebody->Append(Break::New(proc_, file_line, whileloop));
  whileloop->set_body(whilebody);
  // Patch the break node used in the loop body, if any.
  if (break_all != NULL)
    break_all->set_stat(whileloop);
  Block *block = Block::New(proc_, file_line, qvars(), false);
  block->Append(whileloop);
  return block;
}


// Build the conditional expression (def(cond) && cond).
// If the expression is already of the form
//   def(cond)
// or
//   def(cond) && def(cond)
// etc. there is no reason to protect it, and in fact it would be pointless
// to do so, since def(def()) is always true.  The condition we're
// testing may be a variable holding the result of the original condition,
// so we use the original condition for this test.
Expr* WhenAnalyzer::ProtectCondition(Expr* cond, Block* block) {
  if (AllDefs(cond))
    return cond;
  VarDecl* tmp_decl = TempDecl(block->scope(),
                               "$boolean", SymbolTable::bool_type(), cond);
  block->Append(tmp_decl);
  Variable* var = Variable::New(proc_, block->file_line(), tmp_decl);
  List<Expr*>* args = List<Expr*>::New(proc_);
  args->Append(var);
  Intrinsic* def = SymbolTable::universe()->LookupOrDie("def")->AsIntrinsic();
  Call* call = Call::New(proc_, cond->file_line(), def, args);
  var = Variable::New(proc_, block->file_line(), tmp_decl);
  return Binary::New(proc_, cond->file_line(), SymbolTable::bool_type(),
                     call, Binary::LAND, nop, var);
}


// Rewrite the tree for a when statement that uses only one
// quantifier.  There are some simplifications in the generated
// code for that case that are worth isolating.  The three
// cases are detailed in comments within the routine.
// This routine usually gets simple cases where a regular for
// loop is sufficient to guarantee we don't run the expression
// out of bounds; the flag needs_def signals whether to protect
// the expression anyway.

Statement* WhenAnalyzer::AnalyzeOneVar(QuantVarDecl* quant_decl, List<Expr*>* uses, bool needs_def) {
  FileLine* file_line = body()->file_line();
  Block* block = Block::New(proc_, file_line, qvars(), false); // is qvars right??
  Variable* min; // the initial value for the for loop index
  Variable* max; // the upper limit for the for loop index (exclusive)
  Variable* key_array;
  Block* forbody = Block::New(proc_, file_line, qvars(), false);
  forbody->SetLineCounter();
  if (!CombineRange(block, uses, min, max, &key_array))
    return NULL;
  file_line = quant_decl->file_line();
  // Create some pieces for the loop.
  Expr* map_use = NULL;
  VarDecl* before = DeclareIndexVar(file_line, block, quant_decl, uses, min, &map_use);
  Variable* index = Variable::New(proc_, file_line, before);
  Expr* loopcond = Binary::New(proc_, file_line, SymbolTable::bool_type(), index, Binary::LSS, lss_int, max);
  index = Variable::New(proc_, file_line, before);
  Statement* after = Increment::New(proc_, file_line, index, 1);
  before->set_modified_after_init();
  if (map_use != NULL) {
    // There is a map reference in the code, so we need to initialize the quantifier
    // variable to $getkeybyindex(map_used, index).
    forbody->Append(CreateAssignment(file_line, block, quant_decl, VarOf(map_use), key_array, before));
  }
  if (key_array != NULL)
    needs_def = true;
  Expr* safecond = cond();
  if (needs_def)
    safecond = ProtectCondition(safecond, forbody);

  // generate a for loop
  switch (quant_decl->kind()) {

    // The code
    //  when (i: some int; COND) BODY
    // becomes
    //  for (i in combined range) {
    //    if (COND) {
    //      BODY
    //      break;
    //    }
    //  }
    case QuantVarDecl::SOME: {
        Loop* forloop = Loop::New(proc_, before->file_line(), FOR);
        forloop->set_before(before);
        forloop->set_cond(loopcond);
        forloop->set_after(after);
        Block* ifbody = Block::New(proc_, file_line, NULL, false);
        ifbody->Append(body());
        ifbody->Append(Break::New(proc_, file_line, forloop));
        forbody->Append(If::New(proc_, cond()->file_line(), safecond, ifbody, Empty::New(proc_, file_line)));
        forloop->set_body(forbody);
        block->Append(forloop);
      }
      break;

    // The code
    //  when (i: each int; COND) BODY
    // becomes
    //  for (i in combined range) {
    //    if (COND)
    //      BODY
    //  }
    case QuantVarDecl::EACH: {
        forbody->Append(If::New(proc_, cond()->file_line(), safecond, body(), Empty::New(proc_, file_line)));
        Loop* forloop = Loop::New(proc_, forbody->file_line(), FOR);
        forloop->set_before(before);
        forloop->set_cond(loopcond);
        forloop->set_after(after);
        forloop->set_body(forbody);
        block->Append(forloop);
      }
      break;

    // The code
    //  when (i: all int; COND) BODY
    // becomes
    //  outermost: while (true) {
    //    for (i in combined range) {
    //      if (COND)
    //        ;
    //      else
    //        break outermost;
    //    }
    //    BODY
    //    break;
    //  }
    case QuantVarDecl::ALL: {
        Loop* forloop = Loop::New(proc_, loopcond->file_line(), FOR);
        forloop->set_before(before);
        forloop->set_cond(loopcond);
        forloop->set_after(after);
        Break* break_outer = Break::New(proc_, file_line, NULL);
        forbody->Append(If::New(proc_, file_line, safecond, Empty::New(proc_, file_line), break_outer));
        forloop->set_body(forbody);
        block->Append(forloop);
        block = CreateWhileAllLoop(file_line, NULL, block, break_outer);
      }
      break;
  }
  return block;
}


// Rewrite the tree for a when statement that uses more than
// one quantifier.
// Let's look at the code that's generated from the outside in:
//
// First we build an outer block:
//
//   {
//     succeeded: bool = false;
//     <inner loops>
//   }
//
// The inner loops are built as follows.
// For 'some' quantifiers:
//
//   { declare loop temporary for i;
//     some_loop: for (i in combined range for i) {
//       some_body;
//       if (succeeded)
//         break;
//     }
//   }
//
// If the some is not innermost, some_body is the inner loops;
// otherwise it is:
//     t: bool = COND(quantifiers);
//     if (def(t) && t) {
//       BODY
//       succeeded = true;
//       break;
//     }
//
// For 'each' quantifiers:
//
//   { declare loop temporary for j;
//     each_loop: for (j in combined range for j) {
//       each_body;
//     }
//   }
//
// If the each is not innermost, each_body is the inner loops;
// otherwise it is:
//     t: bool = COND(quantifiers);
//     if (def(t) && t) {
//       BODY
//       succeeded = true;
//     }
//
//
// For 'all' quantifiers, if they are innermost, we code in the
// usual way:
//
//    all_loop: while(true) {
//      declare loop temporary for i;
//      outer_loop: for (i in combined range for i) {
//        declare loop temporary for j;
//        for (j in combined range for j) {
//          t: bool = COND(quantifiers);
//          if (def(t) && t)
//            ;
//          else
//            break all_loop;
//        }
//      }
//      BODY
//      succeeded = true;
//      break all_loop;
//    }
//
// If they are not innermost (i: all, j: some; vs. i: some, j: all) it
// takes two phases, one to examine the state and a second to
// do the execution.  That is unimplemented for now but not
// too hard.
//
// To construct this code, we build it from the inside out.

Statement* WhenAnalyzer::AnalyzeNVars(List<QuantVarDecl*>& quants,
                                      List<List<Expr*>*>& alluses) {
  const int nquants = quants.length();
  assert(nquants == alluses.length());

  // See if all the quantifiers have the same kind ('some', 'each', 'all')
  bool all_some = true;
  bool all_each = true;
  bool all_all = true;
  QuantVarDecl::Kind kind = QuantVarDecl::SOME;  // initialize to silence C++ compiler
  for (int i = 0; i < nquants; i++) {
    QuantVarDecl::Kind thiskind = quants[i]->kind();
    if (thiskind != QuantVarDecl::SOME)
      all_some = false;
    if (thiskind != QuantVarDecl::EACH)
      all_each = false;
    if (thiskind != QuantVarDecl::ALL)
      all_all = false;
    kind = thiskind;
  }
  if (all_some || all_each || all_all)
    return AnalyzeNVarsAllSame(quants, alluses, kind);

  FileLine* file_line = body()->file_line();
  Break* break_loop = NULL;
  Break* break_all = NULL;
  // Create the surrounding block, to hold the 'succeeded' variable
  Block* mainblock = Block::New(proc_, file_line, qvars(), false);
  VarDecl* succeeded_decl = TempDecl(mainblock->scope(), "$succeeded", SymbolTable::bool_type(), SymbolTable::bool_f());
  mainblock->Append(succeeded_decl);
  // Build the statement from the inside out.
  // Start with the innermost loop's block.  We'll walk this out as we go.
  Block* forbody = Block::New(proc_, file_line, qvars(), false);
  // Construct the inner if statement
  Block* ifbody = Block::New(proc_, file_line, NULL, false);
  Statement* else_clause = NULL;
  assert(nquants > 1);  // we only get here if we have two or more
  switch (quants[nquants-1]->kind()) {
    case QuantVarDecl::SOME: {
      ifbody->Append(body());
      Variable* succeeded = Variable::New(proc_, file_line, succeeded_decl);
      ifbody->Append(Assignment::New(proc_, file_line, succeeded, SymbolTable::bool_t()));
      break_loop = Break::New(proc_, file_line, NULL);
      ifbody->Append(break_loop); // will patch the break address later
      else_clause = Empty::New(proc_, file_line);
      break;
    }
    case QuantVarDecl::EACH: {
      ifbody->Append(body());
      Variable* succeeded = Variable::New(proc_, file_line, succeeded_decl);
      ifbody->Append(Assignment::New(proc_, file_line, succeeded, SymbolTable::bool_t()));
      else_clause = Empty::New(proc_, file_line);
      break;
    }
    case QuantVarDecl::ALL:
      ifbody->Append(Empty::New(proc_, file_line));
      break_all = Break::New(proc_, file_line, NULL);
      else_clause = break_all;
      break;
  }
  // Build the conditional expression
  Expr* tcond = ProtectCondition(cond(), forbody);
  If* if_statement = If::New(proc_, cond()->file_line(), tcond, ifbody, else_clause);
  forbody->Append(if_statement);
  forbody->SetLineCounter();
  // 'forbody' is now the body of the innermost loop.
  // Create the innermost loop.
  forbody = CreateForLoop(file_line, quants[nquants-1], alluses[nquants-1], NULL, forbody, break_loop);
  // If the innermost loop is an 'all', need to finish writing it here:
  // wrap it in a while(true) with a break.
  if (forbody != NULL && kind == QuantVarDecl::ALL)
    forbody = CreateWhileAllLoop(file_line, succeeded_decl, forbody, break_all);
  // Lay out the loops from the inside out, using the variables in reverse order
  // of declaration.  We've already generated code for the innermost, that is,
  // [nquants-1].
  // Invariant: 'forbody' is the body of the loop we are about to generate
  for (int i = nquants-1; forbody != NULL && --i >= 0; )
    forbody = CreateForLoop(file_line, quants[i], alluses[i], succeeded_decl, forbody, NULL);
  // Finally, add the generated loops to the main block and return that.
  if (forbody != NULL) {
    mainblock->Append(forbody);
    forbody->SetLineCounter();
  }
  return mainblock;
}


// Rewrite the tree for a when statement that uses more than one quantifier,
// in the special case that the quantifiers are all the same kind
// (some, each, all). In this case we can write a significantly tighter loop so
// it's worth separating out.
//
// For the 'some' case, the code:
//
//  when (i: some int, j: some int; COND(i, j)) BODY
//
// becomes
//
//  { declare loop temporary for i;
//    outer_loop: for (i in combined range for i) {
//      declare loop temporary for j;
//      for (j in combined range for j) {
//        t: bool = COND(i, j);
//        if (def(t) && t) {
//          BODY
//          break outer_loop;
//      }
//    }
// }
//
// The only difference in the 'each' case is that there is no break statement.
//
// For the 'all' case:
//
//  when (i: all int, j: all int; COND(i, j)) BODY
//
// becomes
//
//   while_loop: while(true) {
//      declare loop temporary for i;
//      outer_loop: for (i in combined range for i) {
//        declare loop temporary for j;
//        for (j in combined range for j) {
//          t: bool = COND(i, j);
//          if (def(t) && t)
//            ;
//          else
//            break while_loop;
//        }
//      }
//     BODY
//     break while_loop;
//   }
// }

Statement* WhenAnalyzer::AnalyzeNVarsAllSame(List<QuantVarDecl*>& quants,
                                             List<List<Expr*>*>& alluses,
                                             QuantVarDecl::Kind kind) {
  const int nquants = quants.length();
  assert(nquants == alluses.length());

  FileLine* file_line = body()->file_line();
  Break* break_outermost = Break::New(proc_, file_line, NULL); // ignored if 'each'
  // Build the statement from the inside out.
  // Start with the innermost loop's block.  We'll walk this out as we go.
  Block* forbody = Block::New(proc_, file_line, qvars(), false);
  // Construct the inner if statement
  Block* ifbody = Block::New(proc_, file_line, NULL, false);
  Statement* else_clause = NULL;
  if (kind == QuantVarDecl::SOME) {
    ifbody->Append(body());
    ifbody->Append(break_outermost); // will patch the break address later
    else_clause = Empty::New(proc_, file_line);
  } else if (kind == QuantVarDecl::EACH) {
    ifbody->Append(body());
    else_clause = Empty::New(proc_, file_line);
  } else if (kind == QuantVarDecl::ALL) {
    else_clause = break_outermost;
  }
  // Build the conditional expression (def(t) && t) if necessary
  Expr* tcond = ProtectCondition(cond(), forbody);
  // Put it in an 'if' statement
  If* if_statement = If::New(proc_, cond()->file_line(), tcond, ifbody, else_clause);
  forbody->Append(if_statement);
  // 'forbody' is now the body of the innermost loop.
  // Lay out the loops from the inside out, using the variables in reverse order
  // of declaration.
  // Invariant: 'forbody' is the body of the loop we are about to generate
  for (int i = nquants; forbody != NULL && --i >= 0; )
    forbody = CreateForLoop(file_line, quants[i], alluses[i], NULL, forbody, break_outermost);
  // Finally, if it's all 'all' quantifiers, wrap it in a while loop.
  if (forbody != NULL && kind == QuantVarDecl::ALL)
    forbody = CreateWhileAllLoop(file_line, NULL, forbody, break_outermost);
  return forbody;
}


Statement* WhenAnalyzer::Analyze() {
  // create list of the quantifiers being used; the Scope qvars()
  // may contain other variables and definitions.
  quants_ = List<QuantVarDecl*>::New(proc_);
  for (int i = 0; i < qvars()->num_entries(); i++) {
    VarDecl *var = qvars()->entry_at(i)->AsVarDecl();
    if (var != NULL && var->AsQuantVarDecl() != NULL)
      quants_->Append(var->AsQuantVarDecl());
  }
  // Gather the uses of the quantifiers in the conditionals
  ExprAnalyzer exanal(proc_, cond());
  List<List<Expr*>*> alluses(proc_);
  const int nquants = quants_->length();
  // for all quantifiers
  for (int i = 0; i < nquants; i++) {
    List<Expr*>* uses = NULL;
    // for each quantifier get the 'uses' that are array subscripts
    uses = exanal.Uses(quants_->at(i));
    if (uses->length() == 0) {
      set_error(proc_->PrintString("quantifier %N must be constrained by a simple index expression", quants_->at(i)));
      return NULL;
    }
    alluses.Append(uses);
    if (FLAGS_v > 0)
      for (int j = 0; j < uses->length(); j++)
        F.print("use of variable #%d: %N\n", i, uses->at(j));
  }
  if (exanal.error()) {
    set_error(proc_->CopyString(exanal.error()));
    return NULL;
  }
  assert(nquants == alluses.length());

  switch (quants_->length()) {
    case 0:
      // rewrite as a simple if() statement
      return If::New(proc_, body()->file_line(), cond(), body(), Empty::New(proc_, body()->file_line()));
    case 1:
      // One quantifier is a simpler case; do it separately
      return AnalyzeOneVar(quants_->at(0), alluses[0], exanal.needs_def());
    default:
      return AnalyzeNVars(*quants_, alluses);
  }
}

}  // namespace sawzall
