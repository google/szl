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

#include <assert.h>
#include <set>

#include "public/hash_map.h"

#include "engine/globals.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "engine/utils.h"
#include "engine/memory.h"
#include "engine/opcode.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/analyzer.h"
#include "engine/symboltable.h"


namespace sawzall {


// ----------------------------------------------------------------------------

// Conservative value propagation of function literals to perform
// closure checking and unreachable function elimination.  Closure
// checking is an analysis to check for potential calls to nested
// functions outside their required contexts.  Unreachable function
// elimination is a transformation that removes functions (both the
// value and the variable) that are not reachable from the entry
// point.
//
//                    Closure checking
//
// For each variable (local, global, parameter) that might hold a function
// value, we keep a set of function literal values that the variable might hold.
// (Tuples, maps and arrays are considered to hold a function value if any
// key, value, element or non-static field holds a function value.)
//
// Most variables are not represented as they cannot hold function values.
// Of the variables that can, most will hold exactly one value: the value of
// their initializer.  We do not add initializers that are function literals
// to the set, but process them when we iterate over the set.  For programs
// that never use function values except for initialization and calls, no
// sets are needed at all.
//
// For each function that can return a function value, we keep the set of
// values that it can return.
//
// For each call site, we keep the set of function values that might be called,
// again excluding the initializer when the call target is a variable.
//
// We process the functions repeatedly, propagating sets of potential values,
// until a pass is made with no changes (where an initialization does not
// count as a change); normally this will be a single pass (with no function
// sets created at all), but a program that manipulates function values might
// require two or three passes.  In theory this algorithm could be O(N^2) but
// in practice it will always terminate within a few passes.

// Whenever a function value is stored to an outer-scope variable we verify
// that its required level is at or below that scope.  (Passing a parameter
// to an outer-scope function does not count as storing to an outer scope
// since the context is still on the stack.)
// Whenever a function value is returned we verify that its required level
// is at or below that of the scope enclosing the function returning the value.

// Known possible function values for a variable are kept in "fun_sets_",
// which maps a Node* value to a set of Function* values.  The map index
// can be a VarDecl* to indicate possible values for a variable, a Function*
// to indicate possible return values for a function, or a Call* to indicate
// possible targets of the call.
//
//                    Unreachable function elimination
//
// Unreachable function elimination happens in two passes, implemented by
// ReachableVisitor and UnreachableVisitor.  ReachableVisitor performs
// an iterative deepening search from the $main function, using the function
// sets computed by closure checking to mark functions reachable from $main.
// Along the way, it marks variables referenced from reachable code.  The
// analysis stops when the set of reachable functions ceases to grow.
//
// UnreachableVisitor clears the symbol table's lists of functions,
// then it visits the entire AST, starting from $main.  It deletes
// unreferenced function-valued variables completely.  While visiting
// nodes, we add reachable functions to the symbol table.  After
// visiting, we prune the symbol table's list of static variables,
// removing any unreferenced ones.
//
// We treat any use of a function value (outside of a function valued
// VarDecl initializer) as if it were a call.  For example, storing
// the value of a function in an array makes the function reachable,
// even if there is no call to a member of the array.  This is more
// pessimistic than necessary because the body of a used but
// unreachable function is irrelevant.  We could simply replace it
// with an empty body, which might make its original callees
// unreachable.  However, this has visible side effects for functions
// that are converted to strings, so we decided to be pessimistic.
//
// ----------------------------------------------------------------------------


class PropagateFunctionValuesVisitor : public NodeVisitor {
 public:
  // External interface.
  static void AnalyzeFunctions(Analyzer* analyzer, bool remove_unreachable) {
    PropagateFunctionValuesVisitor visitor(analyzer);
    visitor.AnalyzeFunctionsImpl(remove_unreachable);
  }

 private:
  // The types used to accumulate a set of functions associated with a node
  typedef set<Function*> FunctionSet;
  typedef FunctionSet::iterator SetIterator;
  typedef set<VarDecl*> VarDeclSet;
  typedef hash_map<Node*, FunctionSet*> MapType;
  typedef MapType::const_iterator MapIterator;

  // These classes are used to declare share typedefs and analysis state
  // between closure checking and unreachable function analysis, without
  // making them part of the public interface.

  class ClosureCheckVisitor : public DeepNodeVisitor {
   public:
    ClosureCheckVisitor(PropagateFunctionValuesVisitor* outer) :
        outer_(outer), lvalue_(NULL), lvalue_type_(NULL), set_count_(0) { }
    ~ClosureCheckVisitor() {
      VLOG(3) << "function set count = " << set_count_;
    }

    void CheckClosures();
   private:
    // For most nodes just visit the child nodes
    virtual void DoNode(Node* x)  { x->VisitChildren(this); }

    // At assignments and init update the value set of the target variable.
    virtual void DoAssignment(Assignment* x);
    virtual void DoVarDecl(VarDecl* x);
    // At returns update the value set of the function itself.
    virtual void DoReturn(Return* x);

    // At calls accumulate the set of potential targets, propagate
    // from arguments to parameters, and propagate the known potential
    // return values.
    virtual void DoCall(Call* x);
    void CallOne(Call* x, Function* fun);  // a helper

    // At variables and function literals propagate the function set.
    virtual void DoVariable(Variable* x);
    virtual void DoFunction(Function* x);

    // At other expression nodes traverse the parts that might not
    // contribute a function value for error checking and traverse
    // those that might for error checking and to propagate the set of
    // potential function values.  By default we keep doing whatever
    // the parent was doing.
    virtual void DoIndex(Index* x);
    virtual void DoSlice(Slice* x);
    virtual void DoConversion(Conversion* x);
    virtual void DoRuntimeGuard(RuntimeGuard* x);

    // For most types just visit the child types
    virtual void DoType(Type* x)  { x->VisitChildren(this); }

    // Visit the expression and propagate values associated with the value of
    // this subexpression to the specified (VarDecl, Call or Function) node.
    void Propagate(FileLine* fl, Node* lvalue, Type* lvalue_type, Expr* rvalue);

    // Visit the expression but do not propagate values (because the value of
    // this subexpression is never assigned, called or passed as an argument).
    // But since this subexpression might contain a Call, we must visit the
    // subtree in case we need to propagate arguments to parameters.
    void NoPropagate(Expr* rvalue) {
      Propagate(NULL, NULL, NULL, rvalue);
    }

    // Propagate function values to the current (as indicated by lvalue_) set.
    void PropagateFunctionSet(Node* rvalue);
    void PropagateFunction(Function* fun);

    Analyzer* analyzer() { return outer_->analyzer(); }
    MapType* fun_sets() { return outer_->fun_sets(); }

    PropagateFunctionValuesVisitor* outer_;  // enclosing instance
    Node* lvalue_;       // the node to which we are current propagating
    Type* lvalue_type_;  // the type of the value granting access to function(s)
    FileLine* fl_;       // the location to report in error messages
    Function* fun_;      // the current function
    bool changed_;       // whether any functions were added to the sets
    bool report_errors_; // whether to report errors
    int set_count_;      // debugging
  };


  // Visitor to find reachable functions and referenced variables.  It
  // uses the results of closure checking stored in fun_sets_ and
  // stores its results in reachable_ and referenced_.
  class ReachableVisitor : public DeepNodeVisitor {
   public:
    ReachableVisitor(PropagateFunctionValuesVisitor* outer) :
        outer_(outer) { }

    void FindReachable();
   private:
    // For most nodes just visit the child nodes
    virtual void DoNode(Node* x)  { x->VisitChildren(this); }

    // Mark used functions reachable.
    virtual void DoFunction(Function* x);

    // Mark variables in StatExpr
    virtual void DoStatExpr(StatExpr* x);

    // Mark referenced variables.
    virtual void DoVariable(Variable* x);

    // Don't visit initializers of function-valued variable declarations.
    virtual void DoVarDecl(VarDecl* x);

    // For most types just visit the child types
    virtual void DoType(Type* x)  { x->VisitChildren(this); }

    void MarkReachable(Function* fun);

    Analyzer* analyzer() { return outer_->analyzer(); }
    MapType* fun_sets() { return outer_->fun_sets(); }
    FunctionSet* reachable() { return outer_->reachable(); }
    VarDeclSet* referenced() { return outer_->referenced(); }
    void add_referenced(VarDecl* x) { outer_->add_referenced(x); }
    bool is_referenced(VarDecl* x) { return outer_->is_referenced(x); }
    bool is_function_init(VarDecl* x) { return outer_->is_function_init(x); }
    bool is_non_function_init(VarDecl* x) {
      return outer_->is_non_function_init(x);
    }

    PropagateFunctionValuesVisitor* outer_;  // enclosing instance
    vector<Function*> worklist_; // an ordered list of reachable functions
  };

  // Visit variable declarations whose initializers are unreachable
  // functions, deleting them.
  class UnreachableVisitor : public DeepNodeVisitor {
   public:
    UnreachableVisitor(PropagateFunctionValuesVisitor* outer) :
        outer_(outer) { }

    void EliminateUnreachable();
   private:
    // For most nodes just visit the child nodes
    virtual void DoNode(Node* x)  { x->VisitChildren(this); }

    // Strip out the bodies of unreachable functions.
    virtual Expr* VisitFunction(Function* x);

    // Remove unreferenced variable declarations.
    virtual VarDecl* VisitVarDecl(VarDecl* x);

    // Omit declarations of unreachable functions from their containing
    // Block.
    virtual Block* VisitBlock(Block* x);

    // For most types just visit the child types
    virtual void DoType(Type* x)  { x->VisitChildren(this); }

    Analyzer* analyzer() { return outer_->analyzer(); }
    FunctionSet* reachable() { return outer_->reachable(); }
    void add_referenced(VarDecl* x) { outer_->add_referenced(x); }
    bool is_referenced(VarDecl* x) { return outer_->is_referenced(x); }
    bool is_function_init(VarDecl* x) { return outer_->is_function_init(x); }
    bool is_non_function_init(VarDecl* x) {
      return outer_->is_non_function_init(x);
    }
    void keep_function(Function* fun) { outer_->keep_function(fun); }

    PropagateFunctionValuesVisitor* outer_;  // enclosing instance
    vector<Function*> worklist_; // an ordered list of reachable functions
  };

  // Constructor and destructor; only AnalyzeFunctions may use instances.
  PropagateFunctionValuesVisitor(Analyzer* analyzer) : analyzer_(analyzer) { }
  ~PropagateFunctionValuesVisitor() {
    for (MapType::iterator it = fun_sets_.begin(); it != fun_sets_.end(); ++it)
      delete it->second;
  }

  // Repeatedly visit all the functions to propagate and check
  // function values.  If remove_unreachable is true, then delete
  // functions unreachable from $main.
  void AnalyzeFunctionsImpl(bool remove_unreachable);

  static bool Filter(Type* type, FunctionType* ftype);

  Analyzer* analyzer() { return analyzer_; }
  MapType* fun_sets() { return &fun_sets_; }
  FunctionSet* reachable() { return &reachable_; }
  VarDeclSet* referenced() { return &referenced_; }
  void add_referenced(VarDecl* x) { referenced_.insert(x); }
  bool is_referenced(VarDecl* x) {
    return referenced_.find(x) != referenced_.end(); }
  bool is_function_init(VarDecl* x) {
    return (x->init() != NULL && x->init()->AsFunction() != NULL);
  }
  bool is_non_function_init(VarDecl* x) {
    return (x->init() != NULL && x->init()->AsFunction() == NULL);
  }
  void keep_function(Function* fun) {
    if (kept_functions_.find(fun) == kept_functions_.end()) {
      kept_functions_.insert(fun);
      analyzer_->symbol_table()->add_function(fun);
    }
  }

  Analyzer* analyzer_;
  MapType fun_sets_;    // map from VarDecl/Call/Function to function set
  FunctionSet reachable_;
  FunctionSet kept_functions_;
  VarDeclSet referenced_;
};


// ----------------------------------------------------------------------------


void PropagateFunctionValuesVisitor::AnalyzeFunctionsImpl(
    bool remove_unreachable) {
  ClosureCheckVisitor closure_checker(this);
  closure_checker.CheckClosures();

  if (remove_unreachable) {
    ReachableVisitor reachable(this);
    reachable.FindReachable();

    UnreachableVisitor eliminator(this);
    eliminator.EliminateUnreachable();
  }
}


// Determine if a value of the specified type can hold or grant access to
// (through a function call) a function value of the specified type.
bool PropagateFunctionValuesVisitor::Filter(Type* type, FunctionType* ftype) {
  while (true) {
    if (type->is_basic()) {
      return false;
    } else if (type->is_array()) {
      Field* elem = type->as_array()->elem();
      if (elem->recursive())
        return false;
      type = elem->type();
    } else if (type->is_function()) {
      if (ftype->IsEqual(type, false))
        return true;
      Field* result = type->as_function()->result();
      if (result->recursive())
        return false;
      type = result->type();
    } else if (type->is_map()) {
      Field* index = type->as_map()->index();
      Field* elem = type->as_map()->elem();
      return (!index->recursive() && Filter(index->type(), ftype)) ||
             (!elem->recursive()  && Filter(elem->type(), ftype));
    } else if (type->is_tuple()) {
      List<Field*>* fields = type->as_tuple()->fields();
      for (int i = 0; i < fields->length(); i++) {
        Field* f = fields->at(i);
        if (!f->recursive() && Filter(f->type(), ftype))
          return true;
      }
      return false;
    } else {
      return false;
    }
  }
}


// ----------------------------------------------------------------------------
//  Closure checking implementation
// ----------------------------------------------------------------------------


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::CheckClosures() {
  const int kIterationLimit = 100;
  List<Function*>* list = analyzer()->symbol_table()->functions();
  // Propagate until no more changes, without reporting (repeated) errors..
  report_errors_ = false;
  int count;  // just to be paranoid, limit the number of tries
  for (count = 0; count < kIterationLimit; count++) {
    // Visit all the functions, propagating the sets of function literals.
    changed_ = false;
    for (int i = 0; i < list->length(); i++) {
      fun_ = list->at(i);
      fun_->body()->Visit(this);
    }
    if (changed_) {
      // The sets are still growing, continue looping.
      assert(!report_errors_);
    } else if (!report_errors_) {
      // No more growth; one last loop, reporting errors.
      report_errors_ = true;
    } else {
      // Already reported errors, done.
      break;
    }
  }
  if (count == kIterationLimit) {
    analyzer()->Warning(analyzer()->symbol_table()->init_file_line(),
      "internal error in closure analysis; there may be closure scope "
      "problems even if no errors were reported");
    LOG(ERROR) << "Internal error in closure analysis: never terminated.";
  }
}


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::Propagate(
    FileLine* fl, Node* lvalue, Type* lvalue_type, Expr* rvalue) {
  FileLine* old_fl = fl_;
  Node* old_lvalue = lvalue_;
  Type* old_lvalue_type = lvalue_type_;
  fl_ = fl;
  lvalue_ = lvalue;
  lvalue_type_ = lvalue_type;
  rvalue->Visit(this);
  fl_ = old_fl;
  lvalue_ = old_lvalue;
  lvalue_type_ = old_lvalue_type;
}


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::DoAssignment(
    Assignment* x) {
  Variable* root = Analyzer::RootVar(x->lvalue());
  NoPropagate(x->lvalue());
  Propagate(x->file_line(), root->var_decl(), x->lvalue()->type(), x->rvalue());
}


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::DoVarDecl(
    VarDecl* x) {
  // As a special case, defer handling initializers that are function literals
  // so simple function variables do not require value sets.
  if (x->init() != NULL && x->init()->AsFunction() == NULL)
    Propagate(x->file_line(), x, x->type(), x->init());

  // Visit output types to reach expressions in param.
  OutputType* output_type = x->type()->as_output();
  if (output_type != NULL) {
    output_type->Visit(this);
  }
}


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::DoReturn(Return* x) {
  // Add the possible return values to the set for the function.
  if (x->has_result())
    Propagate(x->file_line(), fun_, fun_->ftype()->result_type(), x->result());
}


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::DoCall(Call* x) {
  Expr* init;
  if (x->fun()->AsVariable() != NULL &&
      (init = x->fun()->AsVariable()->var_decl()->init()) != NULL &&
      init->AsFunction() != NULL) {
    // When the target is a variable initialized with a function literal,
    // we do not add the literal to the set.  Handle that case here.
    CallOne(x, init->AsFunction());
  } else if (x->fun()->AsIntrinsic() != NULL) {
    // An intrinsic cannot capture references to variables, but its actual
    // arguments may contain arbitrary operations on variables in the
    // current function, so visit them.
    const List<Expr*>* args = x->args();
    for (int i = 0; i < args->length(); i++) {
      NoPropagate(args->at(i));
    }
  } else {
    // Accumulate the set of functions that we think might be a target of
    // the call.  (We accumulate in the Call node; when we consider the set
    // of possible return values later, they are added to the current lvalue.)
    Propagate(x->file_line(), x, x->fun()->type(), x->fun());
    // Then, for each function that might be called, propagate the arguments
    // to the parameters and add the set of return values to the possible
    // values of this node.
    MapIterator mit = fun_sets()->find(x);
    if (mit != fun_sets()->end()) {
      FunctionSet* fun_set = mit->second;
      for (SetIterator sit = fun_set->begin(); sit != fun_set->end(); ++sit) {
        Function* fun = *sit;
        if (fun->type()->IsEqual(x->fun()->type(), false)) {
          // Matches.  Propagate args to parameters.
          CallOne(x, fun);
        }
      }
    }
  }
}


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::CallOne(
    Call* x, Function* fun) {
  // Propagate args to parameters.
  const List<VarDecl*>* params = fun->params();
  int param_count = fun->ftype()->parameters()->length();
  const List<Expr*>* args = x->args();
  assert(param_count == args->length());  // intrinsics should not get here
  for (int i = 0; i < param_count; i++) {
    Propagate(x->file_line(), params->at(i), params->at(i)->type(),
              args->at(i));
  }
  // Add all the possible return values to the current set being
  // accumulated for the result of the call.
  PropagateFunctionSet(fun);
}


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::DoIndex(Index* x) {
  NoPropagate(x->index());
  x->var()->Visit(this);
}


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::DoSlice(Slice* x) {
  NoPropagate(x->beg());
  NoPropagate(x->end());
  x->var()->Visit(this);
}


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::DoConversion(
    Conversion* x) {
  const List<Expr*>* params = x->params();
  for (int i = 0; i < params->length(); i++)
    NoPropagate(params->at(i));
  x->src()->Visit(this);
}


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::DoRuntimeGuard(
    RuntimeGuard* x) {
  NoPropagate(x->guard());
  x->expr()->Visit(this);
}


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::DoVariable(
    Variable* x) {
  VarDecl* var = x->var_decl();
  // For constant function value initializers, we do not create a set.
  if (var->init() != NULL && var->init()->AsFunction() != NULL)
    PropagateFunction(var->init()->AsFunction());
  PropagateFunctionSet(var);
  // TODO: skip when marked as an lvalue?  harmless.
}


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::DoFunction(
    Function* x) {
  PropagateFunction(x);
}


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::PropagateFunctionSet(
    Node* rvalue) {
  if (lvalue_ != NULL && lvalue_ != rvalue) {
    // VarDecl initializers that are function literals are not in the set.
    if (rvalue->AsVarDecl() != NULL && rvalue->AsVarDecl()->init() != NULL) {
      Function* fun = rvalue->AsVarDecl()->init()->AsFunction();
      if (fun != NULL)
        PropagateFunction(fun);
    }
    MapIterator mit = fun_sets()->find(rvalue);
    if (mit != fun_sets()->end()) {
      FunctionSet* fun_set = mit->second;
      for (SetIterator sit = fun_set->begin(); sit != fun_set->end(); ++sit) {
        // Only propagate if we might access a function of this type.
        if (Filter(lvalue_type_, (*sit)->ftype()))
          PropagateFunction(*sit);
      }
    }
  }
}


void PropagateFunctionValuesVisitor::ClosureCheckVisitor::PropagateFunction(
    Function* fun) {
  if (lvalue_ == NULL)
    return;

  if (lvalue_->AsFunction() != NULL) {
    // Check for a return value that would not be valid in the scope immediately
    // enclosing the function.  If the function may return to a scope ouside the
    // one immediately enclosing its definition, the operation (return or assign)
    // that enables that call will do the necessary checking.
    if (fun->context_level() >= fun_->level()) {
      if (report_errors_) {
        assert(fun->nonlocal_variable() != NULL);
        if (fun->name() != NULL) {
          analyzer()->Error(fl_,
            "the return value might be function %s "
            "(or some value that could contain it or could be used to get it), "
            "but %s uses variable %s which does not exist in the "
            "scope to which the value might be returned",
            fun->name(), fun->name(), fun->nonlocal_variable()->name());
        } else {
          analyzer()->Error(fl_,
            "the return value might be the anonymous function defined at %L "
            "(or some value that could contain it or could be used to get it), "
            "but that function uses variable %s which does not exist in the "
            "scope to which the value might be returned",
            fun->file_line(), fun->nonlocal_variable()->name());
        }
      }
      return;  // do not propagate, so as to minimize error cascades
    }
  } else if (lvalue_->AsVarDecl() != NULL) {
    // Check for assignment to an outer scope variable.
    VarDecl* var = lvalue_->AsVarDecl();
    if (!var->is_param() && fun->context_level() > var->level()) {
      if (report_errors_) {
        assert(fun->nonlocal_variable() != NULL);
        if (fun->name() != NULL) {
          analyzer()->Error(fl_,  // location is uncertain
            "the value being assigned to variable %s (or part of %s) might be "
            "function %s "
            "(or some value that could contain it or could be used to get it), "
            "but %s uses variable %s which does not exist in the "
            "scope where %s was declared",
            var->name(), var->name(), fun->name(), fun->name(),
            fun->nonlocal_variable()->name(), var->name());
        } else {
          analyzer()->Error(fl_,  // location is uncertain
            "the value being assigned to variable %s (or part of %s) might be "
            "the anonymous function defined at %L "
            "(or some value that could contain it or could be used to get it), "
            "but that function uses variable %s which does not exist in the "
            "scope where %s was declared",
            var->name(), var->name(), fun->file_line(),
            fun->nonlocal_variable()->name(), var->name());
        }
      }
      return;  // do not propagate, so as to minimize error cascades
    }
  } else if (lvalue_->AsCall() != NULL) {
    // No special checks: just accumulating the set of possible targets.
  } else {
    ShouldNotReachHere();
  }

  // Add the function to the current set.
  FunctionSet** fun_set = &fun_sets()->operator[](lvalue_);
  if (*fun_set == NULL) {
    *fun_set = new FunctionSet;
    set_count_++;  // for debugging
  }
  if ((*fun_set)->insert(fun).second)
    changed_ = true;
}


// ----------------------------------------------------------------------------
//  Reachable function node visitor implementation.
// ----------------------------------------------------------------------------

void PropagateFunctionValuesVisitor::ReachableVisitor::FindReachable() {
  const int kIterationLimit = 100;
  SymbolTable* symbol_table = analyzer()->symbol_table();
  Function* main_function = symbol_table->main_function();
  worklist_.clear();
  MarkReachable(main_function);

  // Start with the initializers of non-function valued static variables,
  // which are executed before $main.
  for (int i = 0; i < symbol_table->statics()->length(); i++) {
    VarDecl* var_decl = symbol_table->statics()->at(i);
    if (is_non_function_init(var_decl)) {
      add_referenced(var_decl);
      var_decl->init()->Visit(this);
    }
  }

  // Visit the body of each newly reachable function
  int count;  // just to be paranoid, limit the number of tries
  int first_reachable = 0;
  int last_reachable = worklist_.size();
  assert(last_reachable >= 1);
  for (count = 0; count < kIterationLimit; count++) {
    for (int reachable = first_reachable;
         reachable < last_reachable;
         reachable++) {
      // Visit all the newly reachable functions added on the last pass.
      Function* fun = worklist_.at(reachable);
      fun->body()->Visit(this);
    }
    first_reachable = last_reachable;
    last_reachable = worklist_.size();

    if (first_reachable == last_reachable) {
      // Done.
      break;
    }
  }
  if (count == kIterationLimit) {
    analyzer()->Warning(symbol_table->init_file_line(),
                        "internal error in unreachable function elimination");

    LOG(ERROR) <<
        "Internal error in unreachable function elimination: never terminated.";
  }

  // Debugging:
  List<Function*>* list = symbol_table->functions();
  for (int i = 0; i < list->length(); i++) {
    Function* fun = list->at(i);
    if (fun != symbol_table->main_function()) {
      szl_string fname = fun->name() == NULL ? "<unnamed>" : fun->name();
      SetIterator sit = reachable()->find(fun);
      if (sit != reachable()->end()) {
        VLOG(1) << "REACHABLE: " << fname;
      } else {
        VLOG(1) << "NOT REACHABLE: " << fname;
      }
    }
  }

  List<VarDecl*>* sts = symbol_table->statics();
  for (int i = 0; i < sts->length(); i++) {
    VarDecl* st = sts->at(i);
    if (st != SymbolTable::output_var() && st != SymbolTable::stdout_var() &&
        st != SymbolTable::stderr_var() && st != SymbolTable::undef_cnt_var() &&
        st != SymbolTable::undef_details_var() &&
        st != SymbolTable::line_count_var()) {
      szl_string fname = st->name();
      set<VarDecl*>::iterator sit = referenced()->find(st);
      if (sit != referenced()->end()) {
        VLOG(1) << "REFERENCED: " << fname;
      } else {
        VLOG(1) << "NOT REFERENCED: " << fname;
      }
    }
  }
}


void PropagateFunctionValuesVisitor::ReachableVisitor::DoFunction(Function* x) {
  // See remark at top about treating uses of functions as calls.
  x->VisitChildren(this);
  MarkReachable(x);
}


void PropagateFunctionValuesVisitor::ReachableVisitor::DoStatExpr(StatExpr* x) {
  x->VisitChildren(this);
  // StatExpr's refer to their variables and temp variables.
  if (x->var() != NULL) {
    x->var()->Visit(this);
  }
  if (x->tempvar() != NULL) {
    x->tempvar()->Visit(this);
  }
}


void PropagateFunctionValuesVisitor::ReachableVisitor::DoVariable(Variable* x) {
  x->VisitChildren(this);
  // This variable is referenced in reachable code.
  VarDecl* var_decl = x->var_decl();
  assert(var_decl != NULL);
  add_referenced(var_decl);

  // See remark at top about treating uses of functions as calls.
  if (is_function_init(var_decl)) {
    MarkReachable(var_decl->init()->AsFunction());
  }
}


void PropagateFunctionValuesVisitor::ReachableVisitor::DoVarDecl(VarDecl* x) {
  if (is_non_function_init(x)) {
    x->VisitChildren(this);
  }
}


void PropagateFunctionValuesVisitor::ReachableVisitor::MarkReachable(
    Function* fun) {
  SetIterator sit = reachable()->find(fun);
  if (sit == reachable()->end()) {
    // A new reachable function.
    reachable()->insert(fun);
    worklist_.push_back(fun);
  }
}


// ----------------------------------------------------------------------------
//  Unreachable function elimination node visitor implementation.
// ----------------------------------------------------------------------------

void PropagateFunctionValuesVisitor::UnreachableVisitor::EliminateUnreachable()
{
  SymbolTable* symbol_table = analyzer()->symbol_table();
  // Reset the list of functions and fill it with visited functions.
  symbol_table->functions()->Clear();

  // Remove unreferenced static functions from the list of statics.
  // Would prefer to do this through a visitor, but some statics
  // (e.g. stdout, stderr) are created specially and not part of the
  // AST.
  List<VarDecl*> remaining_statics(analyzer()->proc());
  for (int i = 0; i < symbol_table->statics()->length(); i++) {
    VarDecl* var_decl = symbol_table->statics()->at(i);
    if (!var_decl->type()->is_function() || is_referenced(var_decl)) {
      // Keep the static non-function variables and the referenced functions.
      if (var_decl->init() != NULL) {
        // Visit static initializers, which are always reachable.
        var_decl->init()->Visit(this);
      }
      remaining_statics.Append(var_decl);
    }
  }
  symbol_table->statics()->Clear();
  for (int i = 0; i < remaining_statics.length(); i++) {
    symbol_table->add_static(remaining_statics.at(i));
  }

  assert(symbol_table->program() == symbol_table->main_function()->body());
  Block* new_program = symbol_table->program()->Visit(this);
  symbol_table->set_program(new_program);
  symbol_table->main_function()->set_body(new_program);
  // The $main function was not visited, but it is reachable.
  keep_function(symbol_table->main_function());
}

// Don't visit the bodies of unreachable functions.
Expr* PropagateFunctionValuesVisitor::UnreachableVisitor::VisitFunction(
    Function* x) {
  if (reachable()->find(x) != reachable()->end()) {
    x->VisitChildren(this);
  }
  keep_function(x);
  return x;
}


VarDecl* PropagateFunctionValuesVisitor::UnreachableVisitor::VisitVarDecl(
    VarDecl* x) {
  // If the variable is bound to a function and never referenced, delete the
  // declaration.
  if (is_function_init(x) && !is_referenced(x)) {
    return NULL;
  } else {
    x->VisitChildren(this);
    // Visit output types to reach expressions in param.
    OutputType* output_type = x->type()->as_output();
    if (output_type != NULL) {
      output_type->Visit(this);
    }
    return x;
  }
}


// Remove declarations of variables that were initialized to
// unreachable function from the block.
// TODO:  Is there any way to free memory allocated at compile
// time with New?
Block* PropagateFunctionValuesVisitor::UnreachableVisitor::VisitBlock(
    Block* x) {
  bool changed = false;
  Block* new_block = Block::New(analyzer()->proc(), x->file_line(), x->scope(),
                                x->is_program());
  for (int i = 0; i < x->length(); i++) {
    Statement* old_statement = x->at(i);
    Statement* new_statement = old_statement->Visit(this);
    if (new_statement != old_statement) {
      changed = true;
    }
    if (new_statement != NULL) {
      new_block->Append(new_statement);
    }
  }
  if (changed) {
    return new_block;
  } else {
    return x;
  }
}


// ----------------------------------------------------------------------------
//  Analyzer interface to function value propagation.
// ----------------------------------------------------------------------------

void Analyzer::CheckAndOptimizeFunctions(bool remove_unreachable) {
  // Propagate function values and check potential uses.
  PropagateFunctionValuesVisitor::AnalyzeFunctions(this, remove_unreachable);
}


}  // namespace sawzall
