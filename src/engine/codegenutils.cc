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
#include <assert.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "utilities/strutils.h"

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
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "public/sawzall.h"
#include "engine/compiler.h"
#include "engine/convop.h"
#include "engine/intrinsic.h"
#include "engine/codegenutils.h"


namespace sawzall {

static void Error(const char* error_msg, int* error_count) {
  assert(error_msg != NULL);
  fprintf(stderr, "szl: error: %s\n", error_msg);
  (*error_count)++;
}


// ----------------------------------------------------------------------------
// Syntax tree traversal
// ----------------------------------------------------------------------------

// CanCall() determines whether evaluating this node may call
// a szl function or call the GC.
// Used by native code generation to determine when a Sawzall value might
// be subject to having its reference count decremented by a trap handler
// or inspected by GC.
// TODO: if static analysis could determine that a szl function
// could never allocate memory, can we use that here?
// TODO: this seems incomplete; use a visitor?

bool TempVariable::CanCall(bool is_lhs) const {
  // probably not encountered here, but be safe
  if (!initialized())
    return true;  // do not try to traverse x->init()
  else
    return Variable::CanCall(is_lhs);
}


bool Variable::CanCall(bool is_lhs) const {
  return is_lhs;  // loadVu requires memory allocation, loadV does not
}


bool Selector::CanCall(bool is_lhs) const {
  if (is_lhs)
    return true;  // floadVu requires memory allocation

  // floadV does not require memory allocation, but check if variable does
  return var()->CanCall(is_lhs);
}


bool Index::CanCall(bool is_lhs) const {
  // only safe to return false for xloadV
  return is_lhs || !var()->type()->is_array();
}


bool Binary::CanCall(bool is_lhs) const {
  if (cmp_begin < opcode() && opcode() < cmp_end)
    ;  // no memory allocation required for comparison
  else if (op() == Binary::LAND || op() == Binary::LOR)
    ;  // no memory allocation required for logical and/or
  else
    return true;  // op itself may require memory allocation

  return left()->CanCall(false) || right()->CanCall(false);
}


// ----------------------------------------------------------------------------
// Whether evaluating this node may cause a trap
// even if any operands are defined.


bool Conversion::CanCauseTrap(bool is_lvalue) const {
  // All array to map conversions can trap and some of the others can also trap.
  // For non-map conversions key_op defaults to "noconv" which cannot fail.
  return (kind() == kArrayToMapConv ||
          ConversionCanFail(op()) || ConversionCanFail(key_op()));
}


bool Binary::CanCauseTrap(bool is_lvalue) const {
  // Special-case divide and mod; other binary ops do not cause traps.
  if (opcode() == div_int || opcode() == div_uint ||
      opcode() == mod_int || opcode() == mod_uint) {
    IntVal* v = right()->as_int();
    return (v == NULL || v->val() == 0);  // not a literal, or literal zero
  } else if (opcode() == div_float) {
    FloatVal* v = right()->as_float();
    return (v == NULL || v->val() == 0.0);  // not a literal, or literal zero
  } else {
    return false;
  }
}


bool Call::CanCauseTrap(bool is_lvalue) const {
  if (fun()->AsFunction() != NULL) {
    // If static analysis says cannot return undef, use that.
    return fun()->AsFunction()->might_rtn_undef();
  } else if (fun()->AsIntrinsic() != NULL) {
    // Intrinsics are explicitly marked as to whether they can fail.
    return fun()->AsIntrinsic()->can_fail();
  } else {
    // Unknown user function (e.g. closure); if returns value, assume can fail.
    return !type()->is_void();
  }
}


// ----------------------------------------------------------------------------
// Whether evaluating this node may cause a trap, including operands.


class CanTrapVisitor : public NodeVisitor {
 public:
  CanTrapVisitor(Expr* lvalue) : can_trap_(false), lvalue_(lvalue)  { }
  bool can_trap() const  { return can_trap_; }

 private:
  virtual void DoNode(Node* x) {
    assert(x->AsExpr() != NULL);
    if (!can_trap_) {
      if (x->AsExpr()->CanCauseTrap(x == lvalue_))
        can_trap_ = true;
      else
        x->VisitChildren(this);
    }
  }

  // Do not visit the Field child of a Selector; it is not an Expr.
  // TODO: consider not visiting Field nodes (or TypeName) at all.
  virtual void DoSelector(Selector* x)  { x->VisitVar(this); }

  // Do not visit any children of a Function.
  virtual void DoFunction(Function* x)  { }

  virtual void DoCall(Call* x) {
    if (!can_trap_) {
      // Special cases, "def()" and ___undefine() can never fail,
      // even if operand is undefined.
      assert(x != lvalue_);
      if (x->CanCauseTrap(false))
        can_trap_ = true;
      else if (x->fun()->AsIntrinsic() == NULL ||
               (x->fun()->AsIntrinsic()->kind() != Intrinsic::DEF &&
                x->fun()->AsIntrinsic()->kind() != Intrinsic::UNDEFINE))
        x->VisitChildren(this);
    }
  }

  bool can_trap_;    // result
  Expr* lvalue_;     // immediate target of assignment in an Assignment node
};


bool Node::CanTrap() {
  CanTrapVisitor visitor(NULL);
  Visit(&visitor);
  return visitor.can_trap();
}


// Most statements handle traps on individual expressions and so we
// should never see CanTrap() called on the entire statement.
// The statements that allow CanTrap are VarDecl and Emit
// (no visitor needed, so these are handled in their CanTrap methods)
// and Assignment below).
// If CanTrap() is called on any other statement it will be caught by
// the assert in CanTrapVisitor::DoNode().

bool Assignment::CanTrap() {
  // Pass lvalue() to change behavior of CanCauseTrap() for that node only.
  // We cannot use Visit() here, it would fail in CanTrapVisitor::DoNode().
  CanTrapVisitor visitor(lvalue());
  VisitChildren(&visitor);
  return visitor.can_trap();
}


// ----------------------------------------------------------------------------


// Compute the variable denoted by an lvalue. This is the variable
// to be undefined if the lvalue is used in an assignment and the
// value to be assigned to is undefined.
Variable* UndefVar(Expr* lvalue) {
  if (lvalue->AsVariable() != NULL)
    return lvalue->AsVariable();
  if (lvalue->AsIndex() != NULL)
    return UndefVar(lvalue->AsIndex()->var());
  if (lvalue->AsSlice() != NULL)
    return UndefVar(lvalue->AsSlice()->var());
  if (lvalue->AsSelector() != NULL)
    return UndefVar(lvalue->AsSelector()->var());
  ShouldNotReachHere();  // otherwise we don't have an lvalue
  return NULL;
}


// ----------------------------------------------------------------------------
// Variable offset allocation

// Note: ComputeStaticOffsets is similar to ComputeLocalOffsets.
// They should be factored out, eventually (i.e., offset allocation for statics should
// be done exactly as for locals, by creating a dummy "static" function). We left
// the code for parameter handling in here to make the similarity obvious.
//
// Returns the combined size of allocated globals
size_t ComputeStaticOffsets(List<VarDecl*>* vars, int offset, bool do_params) {
  size_t total_size = 0;
  for (int i = 0; i < vars->length(); i++) {
    VarDecl* var = vars->at(i);
    assert(var->is_static());
    if (var->is_param() == do_params) {
      size_t type_size = var->type()->size();
      const size_t size = Align(type_size, sizeof(Val*));
      var->set_offset(offset);
      offset += size;
      total_size += size;
    }
  }
  return total_size;
}


// Returns the combined size of allocated locals
size_t ComputeLocalOffsets(List<VarDecl*>* vars, int offset, bool do_params, bool positive) {
  size_t total_size = 0;
  for (int i = 0; i < vars->length(); i++) {
    VarDecl* var = vars->at(i);
    assert(var->is_local());
    if (var->is_param() == do_params) {
      assert(var->type()->size() == sizeof(Val*));  // 1 slot per variable
      const size_t size = Align(var->type()->size(), sizeof(Val*));
      if (positive) {
        var->set_offset(offset);
        offset += size;
      } else {
        offset -= size;
        var->set_offset(offset);
      }
      total_size += size;
    }
  }
  return total_size;
}


// ----------------------------------------------------------------------------
// Opcode selection

Opcode VariableAccess(Type* var_type, bool is_load, bool is_lhs, int delta) {
  if (delta != 0) {
    // ++ or -- operation, not a load or store
    assert(Align(var_type->size(), sizeof(Val*)) == sizeof(Val*));
    return inc64;
  }
  assert(is_load || !var_type->is_output());  // tables stores are illegal
  return is_load ? (is_lhs ? loadVu : loadV) : storeV;
}


Opcode SelectorAccess(Type* field_type, bool is_load, bool is_lhs, int delta) {
  if (delta != 0)
    return finc64;
  return is_load ? (is_lhs ? floadVu : floadV) : fstoreV;
}


Opcode IndexedAccess(Type* array_type, bool is_load, bool is_lhs, int delta) {
  Opcode iop = illegal;
  Opcode lop = illegal;
  Opcode sop = illegal;
  if (array_type->is_array()) {
    iop = xinc64;
    lop = is_lhs ? xloadVu : xloadV;
    sop = xstoreV;
  } else if (array_type->is_bytes()) {
    iop = xinc8;
    lop = xload8;
    sop = xstore8;
  } else if (array_type->is_string()) {
    iop = xincR;
    lop = xloadR;
    sop = xstoreR;
  } else {
    ShouldNotReachHere();
  }
  if (delta != 0) {
    return iop;
  } else {
    return is_load ? lop : sop;
  }
}


Opcode MappedKey(MapType* map_type, bool is_load, bool is_lhs, int delta,
                 Proc* proc, int* error_count) {
  Type* index_type = map_type->index_type();
  if (delta != 0) {
    if (!is_lhs) {
      Error(proc->PrintString("internal error: inc/dec of map[%T] of %T not lhs",
                              index_type, map_type->elem_type()),
            error_count);
      return illegal;
    }
    is_load = true;  // element is read before update
  }
  Opcode lop = mloadV;  // mloadVu not necessary; we use mloadV to load the map
  Opcode sop = minsertV;
  return is_load ? lop : sop;
}


Opcode MappedValue(MapType* map_type, bool is_load, bool is_lhs, int delta,
                   Proc* proc, int* error_count) {
  Type* elem_type = map_type->elem_type();
  if (delta != 0) {
    if (!is_lhs) {
      Error(proc->PrintString("internal error: inc/dec of map[%T] of %T not lhs",
                              map_type->index_type(), elem_type),
            error_count);
      return illegal;
    }
    if (elem_type->is_int())
      return minc64;
    Error(proc->PrintString("can only inc/dec integers, not map[%T] of %T",
                            map_type->index_type(), elem_type),
          error_count);
    return illegal;
  }
  Opcode lop = is_lhs ? mindexVu : mindexV;
  Opcode sop = mstoreV;
  return is_load ? lop : sop;
}


// ----------------------------------------------------------------------------
// Regex compilation and regex patterns


// Returns the compiled regex for pattern x, if possible (returns NULL otherwise)
void* CompiledRegexp(Expr* x, Proc* proc, int* error_count) {
  void* r = NULL;
  // for now we only allow string literals as static patterns
  StringVal* v = x->as_string();
  if (v != NULL) {
    string s = v->cpp_str(proc);
    const char* error;
    r = CompileRegexp(s.c_str(), &error);
    if (r == NULL) {
      Error(proc->PrintString("could not compile regular expression %q: %s",
                              s.c_str(), error),
            error_count);
      // ok to return NULL
    } else {
      proc->RegisterRegexp(r);
    }
  }
  return r;
}


// Returns the pattern for regex x, if possible (returns "" otherwise)
const char* RegexPattern(Regex* x, Proc* proc, int* error_count) {
  if (x->arg()->is_int()) {  // hex or octal or decimal
    int base = 0;
    if (x->base() != NULL) {
      assert(x->base()->type()->is_int() && x->base()->AsLiteral() != NULL);
      base = x->base()->AsLiteral()->val()->as_int()->val();
    }
    switch(base) {
    case 0:
      return "([-+]?(0x[[:xdigit:]]+|0[0-7]+|[[:digit:]]+))";
    case 8:
      return "([-+]?[0-7]+)";
    case 10:
      return "([-+]?[[:digit:]]+)";
    case 16:
      return "([-+]?(0x)?[[:xdigit:]]+)";
    default:
      Error(proc->PrintString("%L: regex(%T) with base %d unimplemented",
                              x->file_line(), x->arg(), base),
                              error_count);
      return "";
    }
  } else if (x->arg()->is_float()) {  // floating point number
    return "([-+]?(([[:digit:]]+(\\.[[:digit:]]*)?|\\.[[:digit:]]+)([eE][-+]?[[:digit:]]+)?))";
  }

  Error(proc->PrintString("%L: regex(%T) unimplemented",
                          x->file_line(), x->arg()),
        error_count);

  return "";
}

}  // namespace sawzall
