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
#include <utility>

#include "public/hash_map.h"

#include "engine/globals.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/analyzer.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/constantfolding.h"


namespace sawzall {

/* Value propagation.
 *
 * The value of a variable after initialization is found through its VarDecl.
 * Changes resulting from assignment are tracked using a method resembling
 * single static assignment (SSA), where the most recently assigned value
 * (version) is tracked in each block and joined (ssa phi function) at control
 * flow join points.  Unlike SSA, only the most recent version is tracked so
 * any intermediate references must be analyzed on the fly, before the value
 * has been discarded.
 *
 * Loops are handled by a "lookahead" scan that accounts for the possiblity
 * that the loop body has already executed at least once, so that values are
 * not propagated into the loop body if they are referenced and then modified
 * within the loop.
 *
 * For variables that are ever modified by a non-local assignment, every
 * function call is treated as setting the variable to an unknown value.
 * The parser detects non-local assignments and flags the variable, and also
 * tracks whether each variable is ever the target of an assignment.
 * Note that this may be expensive for programs with a large number of global
 * variables that are initialized or assigned to at global scope and also
 * assigned to within a function, and that it requires scanning all global
 * variables at each function call.
 * TODO: optimize if necessary.
 * TODO: track non-local assignments on a per-function basis?
 *
 * The definedness of a variable can be treated as an associated variable
 * that is set to true after each successful (non-trapped) reference to the
 * original variable.  In the special case of "if (def(var))..." the variable
 * is known to be defined at the start of the "then" block and undefined at
 * the start of the "else" block.  Definedness state (the version of the
 * associated definedness variable) is updated and merged together with the
 * value state (version).
 */

// Utility functions.

// Determine if two expressions are equivalent in the sense that they always
// yield the same value regardless of when they are executed.
// Returns one of the parameter values or NULL.
static Expr* JoinExpr(Expr* one, Expr* two);

// For an expression of the form "def(var)" or "!def(var)", get the variable
// and whether the def was negated.  Sets var to NULL if not in that form.
static bool IsDefOfVariable(Expr* expr, Variable** var, bool* negated);

class ScopedFlag {
 public:
  ScopedFlag(bool* ptr, bool value) {
    ptr_ = (*ptr == value) ? NULL : ptr;
    *ptr = value;
  }
  ~ScopedFlag()  { if (ptr_ != NULL) *ptr_ = !*ptr_; };
 private:
  bool* ptr_;
};

// ----------------------------------------------------------------------------

// Represents a version of a variable.
// Information about a variable is accumulated and merged in Version objects.
// The version state for consecutive statements unaffected by control flow
// is just accumulated in the current Version object.
// Versions may be joined as alternatives (e.g. distinct versions occurring in
// the two parts of an if-then-else), conditional (e.g. if-then) or
// unconditional (e.g. if-then-else, where one part is later found to return).
// A call to one of the join methods updates the "this" object with the result.

class Version {
 public:
  typedef Expr::DefState DefState;  // for convenience
  // No default constructor: stack must always use map_.Find(), not map_[].
  Version(VarDecl* var, int locals_level);

  DefState def_state() const  { return def_state_; }
  bool modified() const  { return modified_; }
  Expr* known_value() const { return known_value_; }
  void set_def_state(DefState def_state)  { def_state_ = def_state; }
  void set_modified()  { modified_ = true; }
  void set_known_value(Expr* expr)  { known_value_ = expr; }

  void SetUnknown();
  void SetValue(Expr* expr, DefState def_state);
  void JoinAlternatives(Version one, Version two);
  void JoinUnconditional(Version other);
  bool JoinConditional(Version other);

  // Simplify code by not requiring qualifiers for these constants.
  static const DefState kDefined        = Expr::kDefined;
  static const DefState kUndefined      = Expr::kUndefined;
  static const DefState kDefnessUnknown = Expr::kDefnessUnknown;

 private:
  DefState def_state_;  // defined/undefined/unknown
  bool modified_;         // variable has been modified
  // The "known_value_" member must not be used unless def_state_ is kDefined.
  // (It is not always set to NULL when the Version is marked non-kDefined.
  // When def_state_ is kDefnessUnknown it might represent the value that
  // this variable will have if it turns out to be defined, and we might
  // take advantage of that in the future.  But that would require making
  // certain that it is correct even when def_state_ is not kDefined).
  Expr* known_value_;     // value variable is known to have, if any
};


void Version::SetUnknown() {
  modified_ = true;
  def_state_ = kDefnessUnknown;
  known_value_ = NULL;
}


void Version::SetValue(Expr* expr, DefState def_state) {
  modified_ = true;
  def_state_ = def_state;
  known_value_ = expr;
}


void Version::JoinAlternatives(Version one, Version two) {
  // Code leading to exactly one of two alternatives will be executed.
  if (one.modified_ || two.modified_) {
    modified_ = true;
    known_value_ = JoinExpr(one.known_value_, two.known_value_);
  }

  if (one.def_state_ == two.def_state_)
    def_state_ = one.def_state_;
  else if (one.modified_ || two.modified_)
    def_state_ = kDefnessUnknown;
}


void Version::JoinUnconditional(Version other) {
  // Code leading to the other version will be unconditionally executed.
  if (other.modified_) {
    // The variable was modified, use the new version.
    *this = other;
  } else if (def_state_ != other.def_state_) {
    // The variable was not modified but its definedness changed; update it.
    // The only possible change for a variable that was not modified
    // is from unknown to defined, because of a use that did not trap.
    // But in the case of a use that is known to always trap (making this
    // code unreachable), we still mark the variable as defined to avoid
    // emitting redundant warnings.
    // So we also treat undefined to defined as a valid change.
    assert(def_state_ == kDefnessUnknown || def_state_ == kUndefined);
    assert(other.def_state_ == kDefined);
    def_state_ = other.def_state_;
  }
}


bool Version::JoinConditional(Version other) {
  // Code leading to the other version will be conditionally executed.
  // If the variable was modified and the value differs, it is now unknown.
  bool change = false;
  if (other.modified_) {
    if (!modified_) {
      change = true;
      modified_ = true;
    }
    Expr* new_known_value = JoinExpr(known_value_, other.known_value_);
    if (new_known_value != known_value_) {
      known_value_ = new_known_value;
      change = true;
    }
    if (def_state_ != other.def_state_ && def_state_ != kDefnessUnknown) {
      def_state_ = kDefnessUnknown;
      change = true;
    }
  }
  return change;
}


// ----------------------------------------------------------------------------

// Represents the current version of all variables in the current context for
// which the version may differ from an earlier context.  This is used where
// we want to save the previous state rather than unconditionally updating it.
// Version scopes are joined by joining corresponding variable versions.
// As with Version, calls to the join methods update the "this" scope.

class VersionScope {
 public:
  VersionScope(VersionScope* parent, int locals_level) :
    parent_(parent),
    locals_level_(locals_level),
    dead_end_((parent != NULL) ? parent->dead_end() : false) { }
  bool dead_end() const  { return dead_end_; }
  void clear_dead_end()  { dead_end_= false; }
  void set_dead_end()  { dead_end_= true; }
  const Version* Find(VarDecl* var);
  Version* MutableFind(VarDecl* var);
  void UpdateValue(VarDecl* var, Expr* expr, Version::DefState def_state);
  void SetUnknown(VarDecl* var);
  void SetDefined(VarDecl* var);
  void SetUndefined(VarDecl* var);
  // Change all variables with known values that are modified in some nested
  // function to unknown.
  void SetUnknownAtCall(Function* fun);

  void JoinAlternatives(VersionScope* one, VersionScope* two);
  void MergeAlternativePath(VersionScope* other);
  void JoinUnconditional(VersionScope* other);
  bool JoinConditional(VersionScope* other);
  void Clear()  { map_.clear(); }
  VersionScope* parent() const  { return parent_; }

 private:
  VersionScope* parent_;      // the "enclosing" scope
  int locals_level_;          // level of the current function's local variables
  bool dead_end_;        // the location in the program for which this scope
                         // would represent the state is known to be unreachable
  typedef hash_map<VarDecl*, Version> SVSMap;
  typedef SVSMap::iterator Iterator;
  SVSMap map_;                // the variable-to-version map for this scope
};


const Version* VersionScope::Find(VarDecl* var) {
  for (VersionScope* vs = this; vs != NULL; vs = vs->parent()) {
    Iterator it = vs->map_.find(var);
   if (it != vs->map_.end()) {
     // Ignore outer-scope variables modified after init, because we
     // do not know where we are in outer scope execution.
     if (!var->modified_after_init() || var->level() == locals_level_)
       return &it->second;
     else
       return NULL;
   }
  }
  return NULL;
}


Version* VersionScope::MutableFind(VarDecl* var) {
  // If it exists at this scope, we can change it.
  Iterator it = map_.find(var);
  if (it != map_.end())
    return &it->second;

  // Otherwise create it, copying from nearest copy if any.
  const Version* nearest = parent()->Find(var);
  if (nearest != NULL) {
    // Found it, copy to the map at top of scope so we can change it.
    pair<Iterator, bool> p = map_.insert(make_pair(var, *nearest));
    assert(p.second);
    return &p.first->second;
  }

  // Not found, create a default copy at top of scope.
  pair<Iterator, bool> p =
    map_.insert(make_pair(var, Version(var, locals_level_)));
  assert(p.second);
  return &p.first->second;
}


void VersionScope::UpdateValue(VarDecl* var, Expr* expr,
                               Version::DefState def_state) {
  assert(var->modified_after_init());
  if (var->level() == locals_level_)
    MutableFind(var)->SetValue(expr, def_state);
  else
     assert(var->modified_at_call());
}


void VersionScope::SetUnknown(VarDecl* var) {
  assert(var->modified_after_init());
  if (var->level() == locals_level_)
    MutableFind(var)->SetUnknown();
  else
    assert(var->modified_at_call());
}


void VersionScope::SetDefined(VarDecl* var) {
  if (var->level() == locals_level_)
    MutableFind(var)->set_def_state(Version::kDefined);
}


void VersionScope::SetUndefined(VarDecl* var) {
  if (var->level() == locals_level_)
    MutableFind(var)->set_def_state(Version::kUndefined);
}


void VersionScope::SetUnknownAtCall(Function* fun) {
  // TODO: be a little more selective; at least deal with intrinsics
  List<VarDecl*>* locals = fun->locals();
  for (int i = 0; i < locals->length(); i++) {
    VarDecl* var = locals->at(i);
    if (var->modified_at_call())
      SetUnknown(var);
  }
  List<VarDecl*>* params = fun->params();
  for (int i = 0; i < params->length(); i++) {
    VarDecl* var = params->at(i);
    if (var->modified_at_call())
      SetUnknown(var);
  }
}


void VersionScope::MergeAlternativePath(VersionScope* other_path) {
  // Update our map to reflect changes captured by executing one of two paths,
  // one of which is the current path.
  // This scope's parent must also be a parent of the other scope, but it
  // need not be an immediate parent.
  // Unlike the "Join" methods, we must consider the map entries
  // for the entire path of the other scope, back to the common parent.
  // And unlike "JoinAlternatives", the result updates the first of the two
  // scopes, rather than a common parent scope.
  //
  // The method is to generate a collapsed Version of the other path, then
  // merge the maps using the same technique as in JoinAlternatives,
  // except that the result updates the current VersionScope.

  // If other path is a dead end, just ignore it.
  if (other_path->dead_end())
    return;

  // Generate collapsed Version of the path.
  VersionScope other(parent(), locals_level_);
  for (VersionScope* vs = other_path; vs != parent(); vs = vs->parent()) {
    assert(vs->parent() != NULL);  // fail if not descended from our parent
    for (Iterator it = vs->map_.begin();
         it != vs->map_.end(); ++it)
      other.map_.insert(*it);  // only inserts if not already present
  }

  // If this path is a dead end, use the other instead.
  if (dead_end()) {
    *this = other;
    return;
  }

  // Normal case: neither is a dead end.
  // For variables present in first map but not second, make first conditional.
  // For variables in both, merge.
  for (Iterator it1 = map_.begin(); it1 != map_.end(); ++it1) {
    // Initialize result with nearest copy or default.
    VarDecl* var = it1->first;
    const Version* common = (parent() != NULL) ? parent()->Find(var) : NULL;
    Version result = (common != NULL) ? *common : Version(var, locals_level_);
    // Look for variable in other map and join as needed to result.
    Iterator it2 = other.map_.find(var);
    if (it2 == other.map_.end())
      result.JoinConditional(it1->second);  // In first but not second.
    else
      result.JoinAlternatives(it1->second, it2->second);  // In both.
    // Put result back in this scope.
    it1->second = result;
  }
  // For variables present in second map but not first, make 2nd conditional.
  for (Iterator it2 = other.map_.begin(); it2 != other.map_.end(); ++it2) {
    VarDecl* var = it2->first;
    Iterator it1 = map_.find(var);
    if (it1 == map_.end()) {
      // Initialize result with nearest copy or default.
      const Version* common = (parent() != NULL) ? parent()->Find(var) : NULL;
      Version result = (common != NULL) ? *common : Version(var, locals_level_);
      result.JoinConditional(it2->second);
      // Put result in this scope.
      map_.insert(make_pair(var, result));
    }
  }
}


void VersionScope::JoinAlternatives(VersionScope* one, VersionScope* two) {
  // Update our map to reflect changes captured by executing one of two paths.
  // This scope must be the immediate parent of both of the other scopes.
  assert(one->parent() == this);
  assert(two->parent() == this);

  if (!one->dead_end() && !two->dead_end()) {
    // Normal case: neither is a dead end.
    // For variables present in first map but not second, make first conditional.
    for (Iterator it1 = one->map_.begin(); it1 != one->map_.end(); ++it1) {
      VarDecl* var = it1->first;
      if (two->map_.find(var) == two->map_.end())
        MutableFind(var)->JoinConditional(it1->second);
    }
    // For variables present in second map but not first, make 2nd conditional.
    // For variables in both, merge.
    for (Iterator it2 = two->map_.begin(); it2 != two->map_.end(); ++it2) {
      VarDecl* var = it2->first;
      Iterator it1 = one->map_.find(var);
      if (it1 == one->map_.end())
        MutableFind(var)->JoinConditional(it2->second);
      else
        MutableFind(var)->JoinAlternatives(it1->second, it2->second);
    }
  } else if (one->dead_end() && two->dead_end()) {
    // Both are dead ends, use neither.
    set_dead_end();
  } else if (one->dead_end()) {
    // First part is a dead end, just use second.
    JoinUnconditional(two);
  } else {
    // Second part is a dead end, just use first.
    JoinUnconditional(one);
  }
}


void VersionScope::JoinUnconditional(VersionScope* other) {
  // This scope must be the immediate parent of the other scope.
  assert(other->parent() == this);
  if (other->dead_end())
    set_dead_end();
  if (!dead_end()) {
    // Update our map to reflect changes captured by unconditional execution.
    // If we did not have a map entry, create one first.
    for (Iterator oit = other->map_.begin(); oit != other->map_.end(); ++oit)
      MutableFind(oit->first)->JoinUnconditional(oit->second);
  }
}


bool VersionScope::JoinConditional(VersionScope* other) {
  // This scope must be the immediate parent of the other scope.
  assert(other->parent() == this);
  // Update our map to reflect changes captured by conditional execution.
  // If we did not have a map entry, create one first.
  bool change = false;
  if (!other->dead_end()) {
    for (Iterator oit = other->map_.begin(); oit != other->map_.end(); ++oit)
      change |= MutableFind(oit->first)->JoinConditional(oit->second);
  }
  return change;
}


// ----------------------------------------------------------------------------

// Value propagation.
// This visitor processes the function bodies, accumulating version data
// on a stack of VersionScope objects and applying it to replace variable
// references with propagated values, to warn about unnecessary calls to
// def(), and to flag variables where possible as known defined or known
// undefined for the benefit of the code generator (TBD).
//
// The main parts are:
//   - A visitor class for handling functions, which processes statements
//     and joins version values as implied by control flow.
//   - Three visitor classes used to extract version data from expressions:
//     - Targets of REST and __undefine() are updated.
//     - Non-lvalue variables are updated, redundant def() is checked and
//       the version of the lvalue is updated with a new value.
//     - Except in non-static declarations and return (which have quiet
//       trap handlers) mark all referenced variables as known defined.
//      The ordering is important and the code is conservative; that is,
//      no assumptions are made about the order of evaluation within an
//      expression or between the lvalue and rvalue parts of an assignment.
//   - The VersionScope stack that supports the above.

class PropagateValuesVisitor : public NodeVisitor {
 public:
  PropagateValuesVisitor(Analyzer* analyzer) :
    analyzer_(analyzer),
    ignore_undefs_(analyzer->ignore_undefs()),
    values_(NULL),
    break_values_(NULL),
    continue_values_(NULL),
    break_continue_parent_(NULL),
    lookahead_(false),
    undefined_variable_visitor_(this),
    defined_variable_visitor_(this) { }

 private:
  // These classes are used to declare data members that make use of this
  // class (like Java inner classes).  They have forwarding functions where
  // necessary to access members of the enclosing object (instance of this
  // class).

  // Handle REST and ___undefine() targets.
  class UndefinedVariableVisitor : public NodeVisitor {
   public:
    UndefinedVariableVisitor(PropagateValuesVisitor* outer) : outer_(outer)  { }
   private:
    // For most nodes just visit the child nodes.
    virtual void DoNode(Node* x) {
      x->VisitChildren(this);
    }
    virtual void DoFunction(Function* x)  { }

    virtual void DoCall(Call* x);
    virtual void DoSaw(Saw* x);
    virtual void DoStatExpr(StatExpr* x);

    Function* current_fun() const { return outer_->current_fun(); }
    void SetUndefined(VarDecl* var) const  { outer_->SetUndefined(var); }
    void SetUnknown(VarDecl* var) const  { outer_->SetUnknown(var); }
    void SetUnknownAtCall(Function* f) const  { outer_->SetUnknownAtCall(f); }

    PropagateValuesVisitor* outer_;
  };


  // Substitute known values, check def() and update known values.
  class SubstitutionVisitor : public ConstantFoldingVisitor  {
   public:
    SubstitutionVisitor(PropagateValuesVisitor* outer) :
      ConstantFoldingVisitor(outer->analyzer()->proc()),
      outer_(outer),
      emit_undef_warnings_(true),
      def_state_(Version::kDefined) { }
    void Reset()
      { emit_undef_warnings_ = true; def_state_ = Version::kDefined; }
    Version::DefState def_state() const  { return def_state_; }
    void merge_def_state(Version::DefState new_state);
    // Only "&&" and "||" processing explicitly sets the def state.
    void set_def_state(Version::DefState new_state)  { def_state_ = new_state; }
  private:
    // Analyze functions as we encounter them.
    virtual Expr* VisitFunction(Function* x) {
      return outer_->VisitFunction(x);
    }

    // Substitute version if known and accumulate definedness state.
    virtual Expr* VisitVariable(Variable* x);
    virtual Expr* VisitTempVariable(TempVariable* x);
    // Special handling for DEF, DEBUG, ADDRESSOF and UNDEFINE.
    virtual Expr* VisitCall(Call* x);
    // Statement expressions are handled like calls to anonymous functions.
    virtual Expr* VisitStatExpr(StatExpr* x);

    // These nodes may trap and so be undefined even if all their operands are
    // defined.  So check if they were folded, possibly check the operands,
    // then if result is still not known to be defined, set state to unknown.
    virtual Expr* VisitBinary(Binary* x);
    virtual Expr* VisitComposite(Composite* x);
    virtual Expr* VisitConversion(Conversion* x);
    virtual Expr* VisitRuntimeGuard(RuntimeGuard* x);
    virtual Expr* VisitIndex(Index* x);
    virtual Expr* VisitNew(New* x);
    virtual Expr* VisitSaw(Saw* x);
    virtual Expr* VisitSlice(Slice* x);

    // These nodes are not expected to trap if all operands are defined,
    // and so the definedness state does not need to be updated.
    // Composite, Dollar, Function, Regex, Selector, Intrinsic, Literal,
    // TempVariable

    // Overrides the pure virtual in ConstantFoldingVisitor.
    virtual void Warning(const FileLine* fileline, const char* fmt, ...);

    // Forwarding.
    Proc* proc() const  { return outer_->analyzer()->proc(); }
    int locals_level() const  { return outer_->locals_level(); }
    bool lookahead() const  { return outer_->lookahead(); }
    bool ignore_undefs() const  { return outer_->ignore_undefs(); }
    Version GetVersion(VarDecl* var) const  { return outer_->GetVersion(var); }
    void SetDefined(VarDecl* var) const  { outer_->SetDefined(var); }
    void SetUndefined(VarDecl* var) const  { outer_->SetUndefined(var); }
    ScopedFlag SetIgnoreUndefs(bool v)  { return outer_->SetIgnoreUndefs(v); }

    PropagateValuesVisitor* outer_;
    bool emit_undef_warnings_;       // emit warnings about undefined variables
    Version::DefState def_state_;
  };


  // Mark variables required to be defined as known to be defined.
  class DefinedVariableVisitor : public NodeVisitor {
   public:
    DefinedVariableVisitor(PropagateValuesVisitor* outer) : outer_(outer)  { }
  private:
    // For most nodes just visit the child nodes.
    virtual void DoNode(Node* x) {
      x->VisitChildren(this);
    }
    virtual void DoFunction(Function* x)  { }

    virtual void DoBinary(Binary* x);
    virtual void DoVariable(Variable* x);
    virtual void DoCall(Call* x);

    // Forwarding.
    int locals_level() const  { return outer_->locals_level(); }
    bool ignore_undefs() const { return outer_->ignore_undefs(); }
    void SetDefined(VarDecl* var) const  { outer_->SetDefined(var); }

    PropagateValuesVisitor* outer_;
  };

  // Accessors.
  Analyzer* analyzer() const  { return analyzer_; }
  VersionScope* values()  { return values_; }
  int locals_level() const  { return current_fun_->level(); }
  Function* current_fun() const  { return current_fun_; }
  bool ignore_undefs() const  { return ignore_undefs_; }
  bool global_ignore_undefs() { return analyzer_->ignore_undefs(); }
  bool lookahead() const  { return lookahead_; }

  ScopedFlag SetLookahead(bool v)  { return ScopedFlag(&lookahead_, v); }
  ScopedFlag SetIgnoreUndefs(bool v)  { return ScopedFlag(&ignore_undefs_, v); }

  // Forwarding to top of version scope stack.
  void set_dead_end() const  { values_->set_dead_end(); }
  void UpdateValue(VarDecl* v, Expr* e, Version::DefState def_state) const  {
    values_->UpdateValue(v, e, def_state);
  }
  void SetUnknown(VarDecl* v) const  { values_->SetUnknown(v); }
  void SetDefined(VarDecl* v) const  { values_->SetDefined(v); }
  void SetUndefined(VarDecl* v) const  { values_->SetUndefined(v); }
  void SetUnknownAtCall(Function* fun) const
    { values_->SetUnknownAtCall(fun); }
  void MergeAlternativePath(VersionScope* other) const
    { values_->MergeAlternativePath(other); }
  void JoinAlternatives(VersionScope* one, VersionScope* two) const
    { values_->JoinAlternatives(one, two); }
  void JoinUnconditional(VersionScope* other) const
    { values_->JoinUnconditional(other); }
  bool JoinConditional(VersionScope* other) const
    { return values_->JoinConditional(other); }

  // Version scope stack.
  Version GetVersion(VarDecl* var) const;
  void PushScope(VersionScope* values) {
    assert(values->parent() == values_);
    values_ = values;
  }
  void PopScope(VersionScope* values) {
    assert(values == values_);
    values_ = values_->parent();
  }

  // Visit methods.  For most nodes just visit the child nodes.
  // Should never be called for expressions.
  virtual void DoNode(Node* x)  {
    assert(x->AsExpr() == NULL);
    x->VisitChildren(this);
  }

  // Visit functions and statexprs as encountered (but only once).
  virtual Function* VisitFunction(Function* x);
  virtual StatExpr* VisitStatExpr(StatExpr* x);

  virtual Statement* VisitAssignment(Assignment* x);
  virtual Statement* VisitContinue(Continue* x);
  virtual Statement* VisitBreak(Break* x);
  virtual Statement* VisitIncrement(Increment* x);
  virtual Statement* VisitLoop(Loop* x);
  virtual Statement* VisitWhen(When* x);
  virtual Statement* VisitIf(If* x);
  virtual Statement* VisitSwitch(Switch* x);
  virtual Statement* VisitExprStat(ExprStat* x);
  virtual Statement* VisitResult(Result* x);
  virtual Statement* VisitReturn(Return* x);
  virtual Statement* VisitEmit(Emit* x);
  virtual VarDecl* VisitVarDecl(VarDecl* x);


  Analyzer* analyzer_;
  bool ignore_undefs_;
  Function* current_fun_;            // current function
  VersionScope* values_;             // top of stack of version scopes
  VersionScope* break_values_;       // possible values at break statements
  VersionScope* continue_values_;    // possible values at continue statements
  VersionScope* break_continue_parent_;  // for break and continue merges
  bool lookahead_;                   // suppress subsitution and warnings

  // The UndefinedVariable and DefinedVariable visitors have no state
  // and so are allocated once here.  The Substitution visitor does have
  // state and so is allocated in each statement visitor.
  UndefinedVariableVisitor undefined_variable_visitor_;
  DefinedVariableVisitor defined_variable_visitor_;
};


// ----------------------------------------------------------------------------

Version::Version(VarDecl* var, int locals_level) {
  if (!var->modified_after_init()) {
    // Variable is not modified after its declaration.
    // The level does not matter.  (The main reason we special case non-local
    // variables is that the sequence of modifications is not tracked.)
    // Note that (contrary to user-defined statics added to $main's VersionScope
    // at declaration and always defined), predeclared statics will not be
    // in any VersionScope and will appear undefined, so we correct this here.
    if (var->is_param() || (var->is_static() && var->owner() == NULL)) {
      def_state_ = kDefined;
      modified_ = true;
      known_value_ = NULL;
    } else if (var->init() != NULL) {
      def_state_ = kDefnessUnknown;
      modified_ = true;
      known_value_ = var->init();
    } else {
      def_state_ = kUndefined;
      modified_ = false;
      known_value_ = NULL;
    }
  } else if (var->level() < locals_level) {
    // Outer scope variable for which we do not know the value at
    // entry to this function (including outer-scope parameters).
    def_state_ = kDefnessUnknown;
    modified_ = true;
    known_value_ = NULL;
  } else if (var->is_param()) {
    // Local parameters are always defined at entry to a function.
    def_state_ = kDefined;
    modified_ = true;
    known_value_ = NULL;
  } else if (var->init() == NULL) {
    // Uninitialized local variable is undefined at entry.
    def_state_ = kUndefined;
    modified_ = false;
    known_value_ = NULL;
  } else {
    // Initialized local variable has known value at entry
    // but may not be defined.
    def_state_ = kDefnessUnknown;
    modified_ = false;
    known_value_ = var->init();
    if (known_value_->AsLiteral() != NULL || known_value_->AsFunction() != NULL)
      def_state_ = kDefined;
  }
}


// Values are given in the class, but we have to have definitions too.
const Version::DefState Version::kDefined;
const Version::DefState Version::kUndefined;
const Version::DefState Version::kDefnessUnknown;


Version PropagateValuesVisitor::GetVersion(VarDecl* var) const {
  // Return an existing version, if any.
  const Version* version = values_->Find(var);
  if (version != NULL)
    return *version;
  else
    return Version(var, locals_level());
}


// ----------------------------------------------------------------------------


void PropagateValuesVisitor::UndefinedVariableVisitor::DoCall(Call* x) {
  Intrinsic* intrinsic = x->fun()->AsIntrinsic();
  if (intrinsic != NULL) {
    if (intrinsic->kind() == Intrinsic::UNDEFINE) {
      // Note that ___undefine() has return type void, so cannot be embedded
      // in an expression.  So there is no ordering problem.
      assert(x->args()->length() == 1);
      Variable* var = x->args()->at(0)->AsVariable();
      assert(var != NULL);
      outer_->UpdateValue(var->var_decl(), NULL, Version::kUndefined);
    } else {
      x->VisitChildren(this);
    }
  } else {
    x->VisitChildren(this);
    SetUnknownAtCall(current_fun());
  }
}


void PropagateValuesVisitor::UndefinedVariableVisitor::DoSaw(Saw* x) {
  x->VisitChildren(this);

  // Mark the REST variables as unknown.
  for (int i = 0; i < x->args()->length(); i++) {
    if (x->flags()->at(i) == Saw::REST) {
      Variable* var = Analyzer::RootVar(x->args()->at(i));
      assert(var != NULL);
      SetUnknown(var->var_decl());
    }
  }
}


void PropagateValuesVisitor::UndefinedVariableVisitor::DoStatExpr(StatExpr* x) {
  x->VisitChildren(this);
  SetUnknownAtCall(current_fun());
}


// ----------------------------------------------------------------------------


void PropagateValuesVisitor::SubstitutionVisitor::merge_def_state(
    Version::DefState new_state) {
  if (def_state_ == Version::kUndefined || new_state == Version::kUndefined)
    def_state_ = Version::kUndefined;
  else if (def_state_ == Version::kDefnessUnknown ||
           new_state == Version::kDefnessUnknown)
    def_state_ = Version::kDefnessUnknown;
  else
    def_state_ = Version::kDefined;
}


void PropagateValuesVisitor::SubstitutionVisitor::Warning(
    const FileLine* fileline, const char* fmt, ...) {
  // Do not generate warnings in lookahead mode, else get more than one.
  // (Doing constant folding in lookahead mode is otherwise harmless
  // and is easier than suppressing it.)
  if (!lookahead()) {
    va_list args;
    va_start(args, fmt);
    outer_->analyzer()->Errorv(fileline, true, fmt, &args);
    va_end(args);
  }
}


Expr* PropagateValuesVisitor::SubstitutionVisitor::VisitVariable(Variable* x) {
  // Each Variable node must be used in only one place; catch problems.
  if (!lookahead())
    x->set_subst_visited();
  // Ignore variable if we do not use its value.
  if (!x->is_rvalue())
    return x;
  Version version = GetVersion(x->var_decl());
  merge_def_state(version.def_state());

  // During lookahead, propagate definedness but not value; no warnings.
  if (lookahead())
    return x;

  // Note that it is possible that the variable has been modified yet we know
  // that it is undefined, e.g. we are in the false part of "if (def(x))".
  if (version.def_state() == Version::kUndefined) {
    if (emit_undef_warnings_)
      Warning(x->file_line(),
              "variable %N will always have an undefined value at this point",
              x);
    return x;
  }

  if (version.def_state() == Version::kDefined)
    x->set_is_defined();

  if (x->is_lvalue()) {
    // Indirect target of assignment, or direct or indirect target
    // of increment; no substitution.
    // (A direct target of assignment or saw REST is not marked as an rvalue.)
    // TODO: any known value of an incremented variable is lost;
    // will need it for constant folding, at least to update the associated
    // version value.
  } else if (version.modified() && version.def_state() == Version::kDefined &&
             version.known_value() != NULL) {
    // Only substitute values when not in lookahead.
    Expr* value = version.known_value();
    // Return propagated value instead of variable (literals only for now)
    // We must be careful about reusing expressions with side effects!
    if (value->AsFunction() != NULL)
      return value;
    if (value->AsLiteral() != NULL) {
      assert(value->AsLiteral()->val() != NULL);
      return value;
    }
  }
  return x;
}


Expr* PropagateValuesVisitor::SubstitutionVisitor::VisitTempVariable(TempVariable* x) {
  // During lookahead, neither check whether defined nor propagate values.
  if (lookahead())
    return x;
  assert(x->is_rvalue() && !x->is_lvalue());
  if (!x->subst_visited()) {
    // TempVariable nodes are used more than once so we have to be careful
    // not to visit their initializers repeatedly.
    x->set_subst_visited();
    x->VisitChildren(this);
  }
  // TempVariable does not need Version; its value is always known.
  // Return initializer instead of TempVariable (literals only for now)
  // We must be careful about reusing expressions with side effects!
  // Only substitute values when not in lookahead.
  if (!lookahead()) {
    Expr* value = x->init();
    if (value->AsFunction() != NULL)
      return value;
    if (value->AsLiteral() != NULL) {
      assert(value->AsLiteral()->val() != NULL);
      return value;
    }
  }
  return x;
}


Expr* PropagateValuesVisitor::SubstitutionVisitor::VisitCall(Call* x) {
  // TODO: When analyzing a function set the merged DefState for its
  // return value and use that here if we know which function is being called.
  // Take care with recursion: we can see a call before we have seen all of
  // the return statements that affect its definedness.
  Intrinsic* intrinsic = x->fun()->AsIntrinsic();
  if (intrinsic != NULL) {
    // Special cases:
    // - Some intrinsics (def, __undefine, __addressof) take reference
    //   parameters and so we must not propagate values to their arguments.
    // - No intrinsics ever modify outer-scope variables and so we should
    //   not mark any as having an unknown value because of this call.
    Intrinsic::Kind kind = intrinsic->kind();
    if (kind == Intrinsic::DEF) {
      SubstitutionVisitor visitor(outer_);
      visitor.emit_undef_warnings_ = false;
      x->VisitChildren(&visitor);
      Expr* arg = x->args()->at(0);
      if (visitor.def_state() != Version::kDefnessUnknown) {
        const char* state = (visitor.def_state() == Version::kDefined) ?
                            "defined" : "undefined";
        Warning(x->file_line(), "unnecessary def(): argument "
                "has value (%N) which is known to be %s", arg, state);
      }
      // The use of def() does not affect the defness of surrounding code.
      return x;
    // TODO: consider whether to handle DEBUG and/or ADDRESSOF
    // differently, especially DEBUG("ref", xxx).
    } else if (kind == Intrinsic::UNDEFINE) {
      // Not really an intrinsic, and its argument need not be defined.
      return x;
    }
  }

  // Visit children and try to fold, then handle the result if it changed.
  Expr* folded = ConstantFoldingVisitor::VisitCall(x);
  if (folded != x) {
    assert(folded->AsLiteral() != NULL);
    return folded;  // no need to update definedness
  }

  // Update the definedness state.
  bool can_fail;
  if (intrinsic != NULL) {
    // For intrinsics, we know whether the result can be undefined.
    can_fail = intrinsic->can_fail();
  } else if (x->fun()->AsFunction() != NULL) {
    // For other functions we have merged state from the return statements.
    // (Except recursive calls, where we must assume undefined is possible.)
    can_fail = x->fun()->AsFunction()->might_rtn_undef();
  } else {
    // We do not know the exact function; assume it can fail.
    can_fail = true;
  }
  merge_def_state(can_fail ? Version::kDefnessUnknown : Version::kDefined);
  return x;
}


Expr* PropagateValuesVisitor::SubstitutionVisitor::VisitBinary(Binary* x) {
  if (x->op() == Binary::LAND || x->op() == Binary::LOR) {
    // Short circuited logical operators "&&" and "||" define sequence points
    // and so need fine-grain handling of definedness.
    // Check for short-circuit with def().
    x->VisitLeft(this);
    Version::DefState left_def_state = def_state();
    Variable* def_var = NULL;
    bool negated;
    if (IsDefOfVariable(x->left(), &def_var, &negated)) {
      // After "def(x) &&" or "!def(x) ||" x is known to be defined
      // After "def(x) ||" or "!def(x) &&" x is known to be undefined
      VersionScope right_values(outer_->values(), locals_level());
      outer_->PushScope(&right_values);
      if (negated ^ (x->op() == Binary::LOR))
        SetUndefined(def_var->var_decl());
      else
        SetDefined(def_var->var_decl());
      // Now we have to redo the undefined visitor, just in case,
      // because our knowledge of the variable could be affected by
      // a function call in the right operand
      x->VisitRight(&outer_->undefined_variable_visitor_);
      x->VisitRight(this);
      outer_->PopScope(&right_values);
      // No need to do "JoinConditional" as there is no new persistent state.
      // Note that since "&&" and "||" are sequence points, we could have had
      // more fine-grain version state for the other visitors as well, but
      // there would be little value.  We could, for example, handle
      //   x: int;  // modified by f()
      //   if (flag && f() && x == 0)  // must not warn
      //   if (flag && x == 0 && f())  // can warn about x undefined
      // where without the joins - which require reordering the visits -
      // we cannot warn about x being undefined in the second case.
    } else {
      // Left operand is not "def(var)", just use fine-grain versions.
      // TODO: This case shows a design flaw with separate visitors
      // for undefined/substitute/undefined.  If we run the undefined visitor
      // across the whole expression first, we miss cases like this:
      //   x: int;               # modified in "f()"
      //   if (x == 0 && f()) ;  # fail to detect that "x" is undefined
      // because we lose state on "x" before examining the left operand.
      // Consider other ways of combining and ordering the visitors.
      // Note that if we only do the undefined visitor here, then we must
      // do a JoinConditional to merge its results to the outer scope;
      // currently we rely on the fact that the visitor is applied prematurely
      // to the whole subexpression before we get here, so that result
      // gets joined.
      VersionScope right_values(outer_->values(), locals_level());
      outer_->PushScope(&right_values);
      // Locally, we know the right side is not executed unless the left side
      // variables are defined; so temporarily suppress ignore_undefs.
      {
        ScopedFlag scoped_flag = SetIgnoreUndefs(false);
        x->VisitLeft(&outer_->defined_variable_visitor_);
      }
      x->VisitRight(&outer_->undefined_variable_visitor_);
      // The presence of an undefined variable in the right side does not
      // make the entire expression unconditionally undefined here.
      // So save the def state; and when visiting the right side makes the
      // accumulated def state become undefined, put it back to unknown.
      x->VisitRight(this);
      if (def_state() == Version::kUndefined &&
          left_def_state != Version::kUndefined)
        set_def_state(Version::kDefnessUnknown);
      outer_->PopScope(&right_values);
    }
    // Handle folding separately for && and ||, so we still get warnings
    // about problems in the right operand even if it will never be
    // evaluated.
    // TODO: both here and in "if" statements we might consider
    // emitting a warning about constant values.
    if (x->left()->AsLiteral() != NULL) {
      bool left = x->left()->as_bool()->val();
      if (left == (x->op() == Binary::LOR)) {
        set_def_state(left_def_state);
        return x->left();   // "true || z" or "false && z", return left operand
      } else {
        return x->right();  // "true && z" or "false || z", return right operand
      }
    }
    return x;
  } else {
    // Not && nor ||, visit children and try to fold.
    Expr* folded = ConstantFoldingVisitor::VisitBinary(x);
    if (folded != x) {
      assert(folded->AsLiteral() != NULL);
      return folded;  // no need to update definedness
    }
    // Only "/" and "%" can yield less defined results then their operands.
    // Check for constant non-zero divisors.
    if (x->op() == Binary::DIV || x->op() == Binary::MOD) {
      Literal* r = x->right()->AsLiteral();
      if (r == NULL) {
        // Divisor is not a constant - result could be undefined.
        merge_def_state(Version::kDefnessUnknown);
      } else if ((x->type()->is_int() && r->val()->as_int()->val() == 0) ||
          (x->type()->is_float() && r->val()->as_float()->val() == 0.0)) {
        // Divisor is a constant zero, result known to be undefined.
        // (Else divisor is a constant non-zero, no effect on definedness.)
        merge_def_state(Version::kUndefined);
      }
    }
    return x;
  }
}


Expr* PropagateValuesVisitor::SubstitutionVisitor::VisitComposite(
    Composite* x) {
  // TODO: constant folding should handle string and bytes
  // composites where all the elements are constant.
  x->VisitChildren(this);
  // Composites creating arrays, bytes, maps and tuples are always OK
  // if their elements are OK.  Composites creating strings can fail
  // with a "illegal unicode character" error.
  if (x->type()->is_string())
    merge_def_state(Version::kDefnessUnknown);
  return x;
}


Expr* PropagateValuesVisitor::SubstitutionVisitor::VisitConversion(
    Conversion* x) {
  // Try to fold, then handle the result if it changed.
  Expr* folded = ConstantFoldingVisitor::VisitConversion(x);
  if (folded != x) {
    assert(folded->AsLiteral() != NULL);
    return folded;  // no need to update definedness
  }
  merge_def_state(Version::kDefnessUnknown);
  return x;
}


Expr* PropagateValuesVisitor::SubstitutionVisitor::VisitRuntimeGuard(
    RuntimeGuard* x) {
  // Try to fold, then handle the result if it changed.
  Expr* folded = ConstantFoldingVisitor::VisitRuntimeGuard(x);
  if (folded != x) {
    assert(folded->AsLiteral() != NULL);
    return folded;  // no need to update definedness
  }
  merge_def_state(Version::kDefnessUnknown);
  return x;
}


Expr* PropagateValuesVisitor::SubstitutionVisitor::VisitIndex(Index* x) {
  // If the length temp is used, mark it as defined.
  if (x->length_temp() != NULL)
    SetDefined(x->length_temp()->AsVariable()->var_decl());
  // Try to fold, then handle the result if it changed.
  Expr* folded = ConstantFoldingVisitor::VisitIndex(x);
  if (folded != x) {
    assert(folded->AsLiteral() != NULL);
    return folded;  // no need to update definedness
  }
  merge_def_state(Version::kDefnessUnknown);
  return x;
}


Expr* PropagateValuesVisitor::SubstitutionVisitor::VisitNew(New* x) {
  // Try to fold, then handle the result if it changed.
  Expr* folded = ConstantFoldingVisitor::VisitNew(x);
  if (folded != x) {
    assert(folded->AsLiteral() != NULL);
    return folded;  // no need to update definedness
  }
  merge_def_state(Version::kDefnessUnknown);
  return x;
}


Expr* PropagateValuesVisitor::SubstitutionVisitor::VisitSaw(Saw* x) {
  // No folding for saw().
  merge_def_state(Version::kDefnessUnknown);
  return x;
}


Expr* PropagateValuesVisitor::SubstitutionVisitor::VisitSlice(Slice* x) {
  // If the length temp is used, mark it as defined.
  if (x->length_temp() != NULL)
    SetDefined(x->length_temp()->AsVariable()->var_decl());
  // Try to fold, then handle the result if it changed.
  Expr* folded = ConstantFoldingVisitor::VisitSlice(x);
  if (folded != x) {
    assert(folded->AsLiteral() != NULL);
    return folded;  // no need to update definedness
  }
  merge_def_state(Version::kDefnessUnknown);
  return x;
}


Expr* PropagateValuesVisitor::SubstitutionVisitor::VisitStatExpr(StatExpr* x) {
  Expr* result = outer_->VisitStatExpr(x);
  // Currently "result" is *not* like return, so if it did not fail and
  // we are not ignoring undefs, the statement expression must be defined.
  merge_def_state(ignore_undefs() ? Version::kDefnessUnknown
                                  : Version::kDefined);
  return result;
}


// ----------------------------------------------------------------------------


void PropagateValuesVisitor::DefinedVariableVisitor::DoBinary(Binary* x) {
  x->VisitLeft(this);
  // For short-ciruiting logical operators the right operand is not guaranteed
  // to be executed, so we cannot infer anything about definedness of the
  // variables in the right operand from the absence of a trap when executing
  // the full expression.
  if (x->op() != Binary::LAND && x->op() != Binary::LOR)
    x->VisitRight(this);
}


void PropagateValuesVisitor::DefinedVariableVisitor::DoVariable(Variable* x) {
  // We do not look at the is_lvalue() and is_rvalue() flag because all
  // variables will have been checked except as noted in DoCall().
  if (!ignore_undefs())
    SetDefined(x->var_decl());
}


void PropagateValuesVisitor::DefinedVariableVisitor::DoCall(Call* x) {
  Intrinsic* intrinsic = x->fun()->AsIntrinsic();
  if (intrinsic != NULL) {
    // Special cases: undefined variables within def, __undefine,  __addressof
    // and DEBUG("ref",x) do not cause traps and so we must not infer that
    // they are defined.
    Intrinsic::Kind kind = intrinsic->kind();
    if (kind == Intrinsic::DEF || kind == Intrinsic::ADDRESSOF ||
        kind == Intrinsic::DEBUG || kind == Intrinsic::UNDEFINE) {
      return;  //  ignore any args
    }
  }
  x->VisitChildren(this);
}


// ----------------------------------------------------------------------------

Function* PropagateValuesVisitor::VisitFunction(Function* x) {
  // We could see it a second time because of value propagation; if so, ignore.
  if (x->analysis_started())
    return x;
  x->set_analysis_started();

  // We want to use global ignore_undefs setting for any function since both
  // static and non-static functions can be invoked in a non-static context.
  ScopedFlag scoped_flag = SetIgnoreUndefs(global_ignore_undefs());

  // Changes in this scope are not propagated to enclosing scopes;
  // any effects on on variables in enclosing scopes are handled with
  // the modified_at_call flag.
  // Variables in enclosing scopes are not propagaged to this scope
  // if they are modified after initialization; see VersionScope::Find().
  Function* previous_current_fun = current_fun_;
  current_fun_ = x;
  VersionScope scope(values(), x->level());
  PushScope(&scope);

  // Visit the body, including any nested functions.
  // For the main, nested functions are what appear to be top-level functions.
  x->VisitChildren(this);

  // Do not join results.  (Note: "current_fun_" is wrong here, but not used.)
  PopScope(&scope);
  x->set_analysis_done();
  current_fun_ = previous_current_fun;
  return x;
}


StatExpr* PropagateValuesVisitor::VisitStatExpr(StatExpr* x) {
  // We could see it a second time because of value propagation; if so, ignore.
  if (x->analysis_started())
    return x;
  x->set_analysis_started();

  // Because there is no ordering guarantee within expressions,
  // changes in this scope are not propagated to enclosing scopes.
  // We mark any variables in the current scope that are modified in
  // a statement expression as unknown at the beginning of the containing
  // expression.  This is the same as is done for calls to functions that
  // modify variables in the scope containing the function.
  // The effect should be the same as treating a statement expression as
  // if it were a function literal that is immediately called.
  VersionScope scope(values(), locals_level());
  PushScope(&scope);

  // Visit the body, including any nested StatExprs.
  x->VisitChildren(this);

  // Do not join results.
  PopScope(&scope);
  return x;
}


Statement* PropagateValuesVisitor::VisitAssignment(Assignment* x) {
  // Ordering matters: must not update the state of the root lvalue variable
  // to account for the assignment until after we complete all other uses.

  x->VisitChildren(&undefined_variable_visitor_);

  // If the LHS is a variable, setting its entire definedness state;
  // else merge the evaluation state into the root variable state.
  Variable* lvar = Analyzer::RootVar(x->lvalue());
  SubstitutionVisitor substitution_visitor(this);
  x->VisitChildren(&substitution_visitor);
  Expr* rhs_or_null = (lvar == x->lvalue()) ? x->rvalue() : NULL;
  // Note that with --noignore_undefs the root variable state will be set
  // to kDefined below, overwriting the state we set here.
  UpdateValue(lvar->var_decl(), rhs_or_null, substitution_visitor.def_state());

  x->VisitChildren(&defined_variable_visitor_);
  return x;
}


Statement* PropagateValuesVisitor::VisitContinue(Continue* x) {
  if (continue_values_ == NULL)
    continue_values_ = new VersionScope(break_continue_parent_, locals_level());
  continue_values_->MergeAlternativePath(values_);
  // The current scope has no impact on the rest of the loop body.
  set_dead_end();
  return x;
}


Statement* PropagateValuesVisitor::VisitBreak(Break* x) {
  if (x->stat()->AsLoop() != NULL) {
    // In the outermost loop we could discard values at a "break" in
    // lookahead mode because they would not affect anything in the next
    // iteration of the loop.  But when processing an inner loop as part
    // of lookahead for an outer loop, changes may affect subsequent
    // iterations of the outer loop and so we cannot ignore them.
    if (break_values_ == NULL)
      break_values_ = new VersionScope(break_continue_parent_, locals_level());
    break_values_->MergeAlternativePath(values_);
    // The current scope has no impact on the rest of the loop body.
    set_dead_end();
  }
  return x;
}


Statement* PropagateValuesVisitor::VisitIncrement(Increment* x) {
  // Ordering matters: must not update the state of the root lvalue variable
  // to account for the increment until after we complete all other uses.

  x->VisitChildren(&undefined_variable_visitor_);

  // Collecting definedness locally.
  // The root variable will be defined iff the expression is defined.
  SubstitutionVisitor substitution_visitor(this);
  x->VisitChildren(&substitution_visitor);

  Variable* lvar = Analyzer::RootVar(x->lvalue());
  Expr* value = NULL;
  // For simple variables, can do constant folding for value propagation only.
  // Regular constant folding cannot handle increment, so we have to fetch
  // the value from the variable Version here.
  if (lvar == x->lvalue()) {
    Version version = GetVersion(lvar->var_decl());
    if (version.modified() && version.def_state() == Version::kDefined &&
        version.known_value() != NULL) {
      IntVal* intval = version.known_value()->as_int();
      if (intval != NULL)
        value = Literal::NewInt(analyzer()->proc(), x->file_line(), NULL,
                              intval->val() + x->delta());
    }
  }

  // We can always set the definedness of the root variable.
  // If we did constant folding, we can also set the value.
  // But Increment is a Statement, and we have to generate code for it,
  // so we do not return a replacement Node - always the original.
  // TODO: consider replacing it with an Assignment when folding?
  UpdateValue(lvar->var_decl(), value, substitution_visitor.def_state());

  x->VisitChildren(&defined_variable_visitor_);
  return x;
}


Statement* PropagateValuesVisitor::VisitLoop(Loop* x) {
  // At each break or continue we record a potential alternate
  // value set for the end of the loop.
  VersionScope* saved_break_values = break_values_;
  VersionScope* saved_continue_values = continue_values_;
  VersionScope* saved_break_continue_parent_ = break_continue_parent_;
  break_values_ = NULL;
  continue_values_ = NULL;

  // This is a bit messy.
  // We have to account for the fact that variables referenced in condition,
  // loop body and "after" may depend on the state prior to execution of
  // the loop and/or the state changes that happened in the previous loop.
  // We handle this by processing a conditional "previous" execution of the
  // loop.  Value propagation, constant folding and warnings are disabled while
  // visiting a "previous" loop using the "lookahead" flag.
  //
  // Continue and break statements are handled by accumulating the merged
  // possible state as of any continue statement and as of any break
  // statement; and merging these alternative states at the end of the
  // loop body (for continue) or at the end of the entire loop (for break).

  // The "before" part is not dependent on later assignments.
  x->VisitBefore(this);

  // Normally we can never see a variable before its declaration has been
  // seen; so the first time we see the use of a variable, we can rely on
  // its having the state it has after initialization.
  // But if the "after" statement is a declaration, we may process a use
  // before the declaration and mistakenly mark it as known defined.
  // Prevent this by inserting an entry in the current scope explicitly
  // marking a variable declared in the "after" statement as undefined.
  if (x->after() != NULL && x->after()->AsVarDecl() != NULL)
    SetUndefined(x->after()->AsVarDecl());

  SubstitutionVisitor substitution_visitor(this);

  // Account for one or more possible previous execution of "cond", "body"
  // and "after".  Note that no executions of the loop prior to the final
  // one may have executed a "break".
  //
  // In the general case merging a single lookahead iteraion with the
  // final iteration would not be sufficient.  For example:
  //   a: int = 1;
  //   b: int = 2;
  //   c: int = 2;
  //   d: int = 2;
  //   for (i: int = 0; i < 3; i++) {
  //     d = c;
  //     c = b;
  //     b = a;
  //   }
  // Before the loop and after the first and second iterations d is 2.
  // But after the third iteration d is 1.
  // So if we did value propagation in lookahead and merged it with value
  // propagation in the second pass over the loop, we would incorrectly
  // conclude that d is always 2.
  //
  // We do not do value propagation in lookahead; we only record
  // the definedness state.  But the corresponding problem can occur with
  // definedness:
  //   a: int;
  //   b: int = 1;
  //   c: int = 1;
  //   d: int = 1;
  //   for (i: int = 0; i < 3; i++) {
  //     d = c;
  //     c = b;
  //     b = a;
  //   }
  // Under "--ignore_undefs", merging just two visits to the loop
  // (one lookahead, one final) would result in a Version for "d"
  // indicating that it is always defined at the end of the loop.
  // So we must repeat the lookahead until all possible states have
  // been considered.  Since each repetition captures all possible
  // states, and they are all joined conditionally, the result is
  // the complete set of possible states as we begin the final loop,
  // including skipping the loop entirely.
  // Once we have this, we can safely do value propagation and constant
  // folding within the loop.

  // Repeat until we get no change.
  // (Would not work with value propagation and constant folding:
  //  "i = i + 1" would be different each time.)
  // Proof that this loop terminates:
  //  - Only VarDecls that are already in the current VersionScope or
  //    one of its parents can ever be present in the current VersionScope.
  //  - VarDecls are never removed from the current VersionScope.
  //  - Therefore the set of VarDecls in the current VersionScope will
  //    eventually stop changing.
  //  - The JoinCondition operation may set the "modified_" member of a
  //    Version in this scope, but never clear it.
  //  - The only change that the JoinCondition operation may make to the
  //    "def_state_" member of Version is to set it to kDefnessUnknown.
  //    Once set, the member is not changed again.
  //  - Since constant folding is disabled in lookahead mode, the known_value_
  //    member of a Version will not change from one loop to the next.
  //  - Since all changes to each Version in the current VersionScope are
  //    irreversible and the set of Version values in the scope converges,
  //    the entire state of the VersionScope converges and the final call
  //    to JoinConditional will eventually return false indicating no change.
 
  // It is thought that under "--noignore_undefs" only one visit of the loop
  // in lookahead mode is necessary because definedness does not propagate
  // through assignment when there is undef checking; any successful
  // assignment implies the lvalue is defined.

  while (true) {
    ScopedFlag scoped_flag = SetLookahead(true);
    VersionScope previous_loop(values(), locals_level());
    PushScope(&previous_loop);
    if (x->sym() != DO) {
      x->VisitCond(&undefined_variable_visitor_);
      x->VisitCond(&substitution_visitor);
      x->VisitCond(&defined_variable_visitor_);
    }

    // Ideally we would account for the loop body being unconditionally
    // executed at the start of do-while, so we could catch variables that
    // are undefined early in the loop but defined later.  But that doesn't
    // work if they are referenced in conditional code.
    // Similarly we would like to catch "while (b) b = f();" when "b" is
    // initially undefined, but we would have to skip conditional code
    // as in "while (!first_loop && b) ...".  Seems too hard for now.
    break_continue_parent_ = values()->parent();
    x->VisitBody(this);
    if (continue_values_ != NULL) {
      MergeAlternativePath(continue_values_);
      delete continue_values_;
      continue_values_ = NULL;
    }
    x->VisitAfter(this);
    if (x->sym() == DO) {
      x->VisitCond(&undefined_variable_visitor_);
      x->VisitCond(&substitution_visitor);
      x->VisitCond(&defined_variable_visitor_);
    }
    if (break_values_ != NULL) {
      MergeAlternativePath(break_values_);
      delete break_values_;
      break_values_ = NULL;
    }

    PopScope(&previous_loop);
    if (!JoinConditional(&previous_loop))
      break;
  }

  // Account for a possible execution of "cond", "body" and "after".
  // But to correctly capture the state as of the loop exit, we must consider
  // that the loop exited with either a break statement or a false condition.

  VersionScope final_loop(values(), locals_level());
  PushScope(&final_loop);
  break_continue_parent_ = values()->parent();
  if (x->sym() != DO) {
    ScopedFlag scoped_flag = SetLookahead(true);
    substitution_visitor.Reset();
    x->VisitCond(&undefined_variable_visitor_);
    x->VisitCond(&substitution_visitor);
    x->VisitCond(&defined_variable_visitor_);
  }
  x->VisitBody(this);
  // Join any values accumulated at continue statements before the condition.
  if (continue_values_ != NULL) {
    MergeAlternativePath(continue_values_);
    delete continue_values_;
    continue_values_ = NULL;
  }
  x->VisitAfter(this);

  PopScope(&final_loop);
  JoinConditional(&final_loop);
  VersionScope final_cond(values(), locals_level());
  if (break_values_ != NULL)
    PushScope(&final_cond);
  substitution_visitor.Reset();
  x->VisitCond(&undefined_variable_visitor_);
  x->VisitCond(&substitution_visitor);
  x->VisitCond(&defined_variable_visitor_);
  // The loop exits with either a break or a false condition.
  if (break_values_ != NULL) {
    PopScope(&final_cond);
    JoinAlternatives(&final_cond, break_values_);
    delete break_values_;
    break_values_ = NULL;
  }

  delete break_values_;
  delete continue_values_;
  break_values_ = saved_break_values;
  continue_values_ = saved_continue_values;
  break_continue_parent_ = saved_break_continue_parent_;
  return x;
}


Statement* PropagateValuesVisitor::VisitWhen(When* x) {
  // The "cond" and "body" parts are both represented in "rewritten",
  // which contains a Loop of its own and so does not require special
  // treatment here.
  // TODO: do the rewrite in the analyzer pass and never let
  // a When node get this far, let alone to codegen.
  x->VisitRewritten(this);
  return x;
}


Statement* PropagateValuesVisitor::VisitIf(If* x) {
  // Condition is always executed.
  SubstitutionVisitor substitution_visitor(this);
  x->VisitCond(&undefined_variable_visitor_);
  x->VisitCond(&substitution_visitor);
  x->VisitCond(&defined_variable_visitor_);

  // Special-case "if (def(x))" and "if (!def(x))"
  Variable* def_var = NULL;
  bool negated;

  // Collect the "then" part
  VersionScope then_values(values(), locals_level());
  PushScope(&then_values);
  if (IsDefOfVariable(x->cond(), &def_var, &negated)) {
    if (negated)
      SetUndefined(def_var->var_decl());
    else
      SetDefined(def_var->var_decl());
  }
  x->VisitThen(this);
  PopScope(&then_values);
  if (x->else_part()->AsEmpty() != NULL) {
    // No "else", just conditional.
    JoinConditional(&then_values);
  } else {
    // Collect the "else" part and join.
    VersionScope else_values(values(), locals_level());
    PushScope(&else_values);
    if (def_var != NULL) {
      if (negated)
        SetDefined(def_var->var_decl());
      else
        SetUndefined(def_var->var_decl());
    }
    x->VisitElse(this);
    PopScope(&else_values);
    JoinAlternatives(&then_values, &else_values);
  }
  return x;
}


Statement* PropagateValuesVisitor::VisitSwitch(Switch* x) {
  // Treated as if rewritten from:
  //   switch(condition) {
  //     case1label1:
  //     case1label2:
  //       case1statement;
  //     case2label1:
  //     case2label2:
  //       case2statement;
  //     default:
  //       defaultstatement;
  // to:
  //    temp = condition;
  //    if (case1label1 == temp || case1label2 == temp)
  //      case1statement;
  //    else
  //      if (case2label1 == temp || case2label2 == temp)
  //        case2statement;
  //      else
  //        defaultstatement;

  // Condition is always executed.
  SubstitutionVisitor substitution_visitor(this);
  x->VisitTag(&undefined_variable_visitor_);
  x->VisitTag(&substitution_visitor);
  x->VisitTag(&defined_variable_visitor_);

  // Loop through the labels.  At the top of the inner loop the current
  // scope contains the map to be used for the complete "if" for this label,
  // which is also the "else" part of the previous "if" (except the first).
  // Account for the label execution and push the new "else" part.
  for (int i = 0; i < x->cases()->length(); i++) {
    Case* the_case = x->cases()->at(i);
    for (int j = 0; j < the_case->labels()->length(); j++) {
      // Only the first label is executed unconditionally.
      // (Should not really need to use undefined_variable_visitor here,
      // since labels are expressions and so cannot undefine variables.)
      if (j > 0)
        PushScope(new VersionScope(values(), locals_level()));
      substitution_visitor.Reset();
      SubstitutionVisitor substitution_visitor(this);
      the_case->VisitLabel(&undefined_variable_visitor_, j);
      the_case->VisitLabel(&substitution_visitor, j);
      the_case->VisitLabel(&defined_variable_visitor_, j);
    }
    // Each label after the first in a case has its own scope because its
    // versions do not propagate to the statement; pop the extra scopes.
    for (int j = 1; j < the_case->labels()->length(); j++) {
      VersionScope* label_values = values();
      PopScope(label_values);
      delete label_values;
    }
    PushScope(new VersionScope(values(), locals_level()));
  }
  // The last "else" gets the default statement.
  x->VisitDefaultCase(this);
  // In reverse order, process each "then" and join.
  for (int i = x->cases()->length() - 1; i >= 0; --i) {
    Case* the_case = x->cases()->at(i);
    VersionScope* else_part = values();
    PopScope(else_part);
    VersionScope then_part(values(), locals_level());
    PushScope(&then_part);
    the_case->VisitStat(this);
    PopScope(&then_part);
    JoinAlternatives(&then_part, else_part);
    delete else_part;
  }
  return x;
}


Statement* PropagateValuesVisitor::VisitExprStat(ExprStat* x) {
  SubstitutionVisitor substitution_visitor(this);
  x->VisitExpr(&undefined_variable_visitor_);
  x->VisitExpr(&substitution_visitor);
  x->VisitExpr(&defined_variable_visitor_);
  return x;
}


Statement* PropagateValuesVisitor::VisitResult(Result* x) {
  SubstitutionVisitor substitution_visitor(this);
  x->VisitExpr(&undefined_variable_visitor_);
  x->VisitExpr(&substitution_visitor);
  x->VisitExpr(&defined_variable_visitor_);
  return x;
}


Statement* PropagateValuesVisitor::VisitReturn(Return* x) {
  // TODO: compute whether always defined & flag function if all rtns defined
  // so we need not check the result at each call.
  x->VisitResult(&undefined_variable_visitor_);
  // Special case: no warnings on "return x;" where "x" is an uninitialized
  // variable in this scope.
  bool skip_substitution = false;
  if (x->has_result()) {
    Variable* var = x->result()->AsVariable();
    if (var != NULL) {
      VarDecl* vardecl = var->var_decl();
      if (vardecl->init() == NULL && !vardecl->modified_after_init() &&
          vardecl->owner() == current_fun())
        skip_substitution = true;
    }
  }
  if (skip_substitution) {
    current_fun()->set_might_rtn_undef();
  } else {
    SubstitutionVisitor substitution_visitor(this);
    x->VisitResult(&substitution_visitor);
    if (substitution_visitor.def_state() != Version::kDefined)
      current_fun()->set_might_rtn_undef();
  }
  // No defined values visitor: return statements have silent traps
  set_dead_end();
  return x;
}


Statement* PropagateValuesVisitor::VisitEmit(Emit* x) {
  SubstitutionVisitor substitution_visitor(this);
  // Emits have implicit internal assignments which we must account for when
  // there are formats, since they reference the assigned-to variables.
  // So we must account for the assignments after the indices and value are
  // visited and before the index and element formats are visited.

  x->VisitOutput(&undefined_variable_visitor_);
  x->VisitIndices(&undefined_variable_visitor_);
  x->VisitValue(&undefined_variable_visitor_);
  x->VisitWeight(&undefined_variable_visitor_);
  x->VisitOutput(&substitution_visitor);
  x->VisitIndices(&substitution_visitor);
  x->VisitValue(&substitution_visitor);
  x->VisitWeight(&substitution_visitor);

  // Implicit assignments to index and value variables.
  // Since the variables are local temporaries which will not be used unless
  // their initializers are defined, we can safely just say that the variables
  // are known to be defined.
  List<VarDecl*>* index_decls = x->index_decls();
  List<Expr*>* indices = x->indices();
  for (int i = 0; i < index_decls->length(); i++)
    values_->MutableFind(index_decls->at(i))->SetValue(indices->at(i),
                                                       Version::kDefined);
  values_->MutableFind(x->elem_decl())->SetValue(x->value(), Version::kDefined);

  x->VisitIndexFormat(&undefined_variable_visitor_);
  x->VisitElemFormat(&undefined_variable_visitor_);
  x->VisitIndexFormat(&substitution_visitor);
  x->VisitElemFormat(&substitution_visitor);

  // Implicit assignments do not affect marking referenced variables as defined.
  x->VisitChildren(&defined_variable_visitor_);
  return x;
}


VarDecl* PropagateValuesVisitor::VisitVarDecl(VarDecl* x) {
  // Output variables don't have init, but being static they are always defined
  if (x->init() == NULL && !x->type()->is_output())
    return x;

  SubstitutionVisitor substitution_visitor(this);
  x->VisitInit(&undefined_variable_visitor_);

  if (x->is_static()) {
    // Statics (incl. output vars) must be defined even under --ignore_undefs.
    // So we really shouldn't encounter any undefined values here anyway.
    // (An exception is evaluating function bodies since statically defined
    // functions can be called in a non-static context, but that will be
    // handled in VisitFunction).
    ScopedFlag scoped_flag = SetIgnoreUndefs(false);
    if (x->type()->is_output()) {
      // Special case: parameter is evaluated at init.
      // TODO: clone and set as the initializer.
      // Check NSupport::OpenO and issue of evaluating param twice.
    }
    x->VisitInit(&substitution_visitor);
    x->VisitInit(&defined_variable_visitor_);  // not really necessary
    values_->MutableFind(x)->SetValue(x->init(), Version::kDefined);
  } else {
    // Non-statics may or may not be defined: silent trap.
    // So we can update their state, but we cannot say that any variables
    // referenced in the initializer are now known to be defined.
    // Collecting definedness locally, just for initializer.
    x->VisitInit(&substitution_visitor);
    values_->MutableFind(x)->SetValue(x->init(),
                                      substitution_visitor.def_state());
  }

  return x;
}


// ----------------------------------------------------------------------------


static Expr* JoinExpr(Expr* one, Expr* two) {
  if (one == NULL || two == NULL)
    return NULL;
  Literal* lit1 = one->AsLiteral();
  Literal* lit2 = two->AsLiteral();
  if (lit1 != NULL && lit2 != NULL) {
    assert(lit1->type()->IsEqual(lit2->type(), false));
    if (lit1->val()->IsEqual(lit2->val()))
      return one;
  }
  return NULL;
}


static bool IsDefOfVariable(Expr* expr, Variable** var, bool* negated) {
  *var = NULL;
  *negated = false;
  // TODO: Add a "Not" expression node so we don't have to do this.
  if (expr->AsBinary() != NULL) {
    Binary* node = expr->AsBinary();
    if (node->op() == Binary::EQL && node->left() == SymbolTable::bool_f()) {
      expr = node->right();
      *negated = true;
    }
  }
  if (expr->AsCall() != NULL) {
    Call* call = expr->AsCall();
    if (call->fun()->AsIntrinsic() != NULL &&
        call->fun()->AsIntrinsic()->kind() == Intrinsic::DEF) {
      Expr* arg = call->args()->at(0);
      // For selectors, test the tuple (field defined iff tuple defined).
      // TODO: constant fold definedness.
      while (arg->AsSelector() != NULL)
        arg = arg->AsSelector()->var();
      *var = arg->AsVariable();
      return (*var != NULL);
    }
  }
  return false;
}

// ----------------------------------------------------------------------------


void Analyzer::PropagateValues() {
  // Value propagation, constant folding and definedness propagation.
  PropagateValuesVisitor visitor(this);
  symbol_table_->main_function()->Visit(&visitor);
}


// ----------------------------------------------------------------------------


}  // namespace sawzall
