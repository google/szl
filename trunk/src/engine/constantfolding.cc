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

// Constant folding.

#include <assert.h>

#include "engine/globals.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "utilities/strutils.h"

#include "engine/tracer.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/parser.h"
#include "engine/convop.h"
#include "engine/intrinsic.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/analyzer.h"
#include "engine/constantfolding.h"


// TODO: treat composites with all-literal values as literals
// so they are easily handled by len(), Index, etc. for folding purposes.

namespace sawzall {

static int kMaxFoldedNewLength = 100;  // arbitrary


Expr* ConstantFoldingVisitor::VisitBinary(Binary* x) {
  x->VisitChildren(this);
  Expr* leftnode = x->left();
  Expr* rightnode = x->right();
  if (leftnode->AsLiteral() == NULL || rightnode->AsLiteral() == NULL)
    return x;
  Type* lefttype = leftnode->type();
  Type* righttype = rightnode->type();
  if (lefttype->is_bool()) {
    assert(righttype->is_bool());
    bool left = leftnode->as_bool()->val();
    bool right = rightnode->as_bool()->val();
    bool result;
    switch(x->op()) {
    // comparison
    case Binary::EQL:
      return (left == right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::NEQ:
      return (left != right) ? SymbolTable::bool_t() : SymbolTable::bool_f();

    // logical
    case Binary::LAND:
    case Binary::AND:
      result = left & right;
      break;
    case Binary::LOR:
    case Binary::OR:
      result = left | right;
      break;

    // no other bool operations
    default:
      assert(false);
      return x;
    }
    return Literal::NewBool(proc_, x->file_line(), NULL, result);
  } else if (lefttype->is_int()) {
    assert(righttype->is_int());
    szl_int left = leftnode->as_int()->val();
    szl_int right = rightnode->as_int()->val();
    szl_int result;
    switch(x->op()) {
    // arithmetic
    case Binary::ADD:
      result = left + right;
      break;
    case Binary::SUB:
      result = left - right;
      break;
    case Binary::MUL:
      result = left * right;
      break;
    case Binary::DIV:
      if (right == 0) {
        Warning(x->file_line(), "divide by zero");
        return x;
      } else {
        result = left / right;
      }
      break;
    case Binary::MOD:
      if (right == 0) {
        Warning(x->file_line(), "divide by zero");
        return x;
      } else {
        result = left % right;
      }
      break;

    // comparison
    case Binary::EQL:
      return (left == right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::NEQ:
      return (left != right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::LSS:
      return (left <  right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::LEQ:
      return (left <= right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::GTR:
      return (left >  right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::GEQ:
      return (left >= right) ? SymbolTable::bool_t() : SymbolTable::bool_f();

    // bit manipulation
    case Binary::BAND:
      result = left & right;
      break;
    case Binary::BOR:
      result = left | right;
      break;
    case Binary::BXOR:
      result = left ^ right;
      break;
    case Binary::SHL:
      result = left << (right & 0x3f);
      break;
    case Binary::SHR:
      result = static_cast<uint64>(left) >> (right & 0x3f);
      break;

    // no other int operations
    default:
      assert(false);
      return x;
    }
    return Literal::NewInt(proc_, x->file_line(), NULL, result);
  } else if (lefttype->is_uint()) {
    assert(righttype->is_uint());
    szl_uint left = leftnode->as_uint()->val();
    szl_uint right = rightnode->as_uint()->val();
    szl_uint result;
    switch(x->op()) {
    // arithmetic
    case Binary::ADD:
      result = left + right;
      break;
    case Binary::SUB:
      result = left - right;
      break;
    case Binary::MUL:
      result = left * right;
      break;
    case Binary::DIV:
      if (right == 0) {
        Warning(x->file_line(), "divide by zero");
        return x;
      } else {
        result = left / right;
      }
      break;
    case Binary::MOD:
      if (right == 0) {
        Warning(x->file_line(), "divide by zero");
        return x;
      } else {
        result = left % right;
      }
      break;

    // comparison
    case Binary::EQL:
      return (left == right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::NEQ:
      return (left != right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::LSS:
      return (left <  right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::LEQ:
      return (left <= right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::GTR:
      return (left >  right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::GEQ:
      return (left >= right) ? SymbolTable::bool_t() : SymbolTable::bool_f();

    // bit manipulation
    case Binary::BAND:
      result = left & right;
      break;
    case Binary::BOR:
      result = left | right;
      break;
    case Binary::BXOR:
      result = left ^ right;
      break;
    case Binary::SHL:
      result = left << (right & 0x3f);
      break;
    case Binary::SHR:
      result = left >> (right & 0x3f);
      break;

    // no other uint operations
    default:
      assert(false);
      return x;
    }
    return Literal::NewUInt(proc_, x->file_line(), NULL, result);
  } else if (lefttype->is_float()) {
    assert(righttype->is_float());
    szl_float left = leftnode->as_float()->val();
    szl_float right = rightnode->as_float()->val();
    szl_float result;
    switch(x->op()) {
    // arithmetic
    case Binary::ADD:
      result = left + right;
      break;
    case Binary::SUB:
      result = left - right;
      break;
    case Binary::MUL:
      result = left * right;
      break;
    case Binary::DIV:
      if (right == 0.0) {
        Warning(x->file_line(), "divide by zero");
        return x;
      } else {
        result = left / right;
      }
      break;

    // comparison
    case Binary::EQL:
      return (left == right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::NEQ:
      return (left != right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::LSS:
      return (left <  right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::LEQ:
      return (left <= right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::GTR:
      return (left >  right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::GEQ:
      return (left >= right) ? SymbolTable::bool_t() : SymbolTable::bool_f();

    // no other float operations
    default:
      assert(false);
      return x;
    }
    return Literal::NewFloat(proc_, x->file_line(), NULL, result);
  } else if (lefttype->is_time()) {
    assert(righttype->is_time());
    szl_time left = leftnode->as_time()->val();
    szl_time right = rightnode->as_time()->val();
    szl_time result;
    switch(x->op()) {
    // arithmetic
    case Binary::ADD:
      result = left + right;
      break;
    case Binary::SUB:
      result = left - right;
      break;

    // comparison
    case Binary::EQL:
      return (left == right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::NEQ:
      return (left != right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::LSS:
      return (left <  right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::LEQ:
      return (left <= right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::GTR:
      return (left >  right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::GEQ:
      return (left >= right) ? SymbolTable::bool_t() : SymbolTable::bool_f();

    // no other time operations
    default:
      assert(false);
      return x;
    }
    return Literal::NewTime(proc_, x->file_line(), NULL, result);
  } else if (lefttype->is_fingerprint()) {
    assert(righttype->is_fingerprint());
    szl_fingerprint left = leftnode->as_fingerprint()->val();
    szl_fingerprint right = rightnode->as_fingerprint()->val();
    szl_fingerprint result;
    switch(x->op()) {
    // arithmetic
    case Binary::ADD:
      result = FingerprintCat(left, right);
      break;

    // comparison
    case Binary::EQL:
      return (left == right) ? SymbolTable::bool_t() : SymbolTable::bool_f();
    case Binary::NEQ:
      return (left != right) ? SymbolTable::bool_t() : SymbolTable::bool_f();

    // no other fingerprint operations
    default:
      assert(false);
      return x;
    }
    return Literal::NewFingerprint(proc_, x->file_line(), NULL, result);
  } else if (lefttype->is_string()) {
    assert(righttype->is_string());
    StringVal* left = leftnode->as_string();
    StringVal* right = rightnode->as_string();
    if (x->op() == Binary::ADD) {
      // concatenation
      StringVal* result = Factory::NewString(proc_,
          left->length() + right->length(),
          left->num_runes() + right->num_runes());
      memcpy(result->base(), left->base(), left->length());
      memcpy(result->base() + left->length(), right->base(), right->length());
      return Literal::New(proc_, x->file_line(), NULL, result);
    } else {
      // must be comparison
      int diff = memcmp(left->base(), right->base(),
                        MinInt(left->length(), right->length()));
      if (diff == 0)
        diff = left->length() - right->length();
      switch(x->op()) {
      case Binary::EQL:
        return (diff == 0) ? SymbolTable::bool_t() : SymbolTable::bool_f();
      case Binary::NEQ:
        return (diff != 0) ? SymbolTable::bool_t() : SymbolTable::bool_f();
      case Binary::LSS:
        return (diff <  0) ? SymbolTable::bool_t() : SymbolTable::bool_f();
      case Binary::LEQ:
        return (diff <= 0) ? SymbolTable::bool_t() : SymbolTable::bool_f();
      case Binary::GTR:
        return (diff >  0) ? SymbolTable::bool_t() : SymbolTable::bool_f();
      case Binary::GEQ:
        return (diff >= 0) ? SymbolTable::bool_t() : SymbolTable::bool_f();

      // no other string operations
      default:
        assert(false);
        return x;
      }
    }
  } else if (lefttype->is_bytes()) {
    assert(righttype->is_bytes());
    BytesVal* left = leftnode->as_bytes();
    BytesVal* right = rightnode->as_bytes();
    if (x->op() == Binary::ADD) {
      // concatenation
      BytesVal* result = Factory::NewBytes(proc_,
          left->length() + right->length());
      memcpy(result->base(), left->base(), left->length());
      memcpy(result->base() + left->length(), right->base(), right->length());
      return Literal::New(proc_, x->file_line(), NULL, result);
    } else {
      // must be comparison
      int diff = memcmp(left->base(), right->base(),
                        MinInt(left->length(), right->length()));
      if (diff == 0)
        diff = left->length() - right->length();
      switch(x->op()) {
      case Binary::EQL:
        return (diff == 0) ? SymbolTable::bool_t() : SymbolTable::bool_f();
      case Binary::NEQ:
        return (diff != 0) ? SymbolTable::bool_t() : SymbolTable::bool_f();
      case Binary::LSS:
        return (diff <  0) ? SymbolTable::bool_t() : SymbolTable::bool_f();
      case Binary::LEQ:
        return (diff <= 0) ? SymbolTable::bool_t() : SymbolTable::bool_f();
      case Binary::GTR:
        return (diff >  0) ? SymbolTable::bool_t() : SymbolTable::bool_f();
      case Binary::GEQ:
        return (diff >= 0) ? SymbolTable::bool_t() : SymbolTable::bool_f();

      // no other bytes operations
      default:
        assert(false);
        return x;
      }
    }
  } else {
    // no other literal types
    assert(false);
    return x;
  }
}


Expr* ConstantFoldingVisitor::VisitCall(Call* x) {
  // TODO: inline small functions and some intrinsics
  x->VisitChildren(this);
  // If it's a foldable intrinsic and all args are literals, call it.
  Intrinsic* intrinsic = x->fun()->AsIntrinsic();
  if (intrinsic == NULL || !intrinsic->can_fold())
    return x;
  // TODO: special handling for assert() with known value?
  const List<Expr*>* args = x->args();
  for (int i = 0; i < args->length(); i++)
    if (args->at(i)->AsLiteral() == NULL)
      return x;

  // Find the target, mapping overloaded intrinsics as needed.
  Intrinsic::CFunction target = Intrinsics::TargetFor(proc_, intrinsic, args);

  // Callable.  Fake a stack and call it.
  List<Val*> stack(proc_);
  for (int i = 0; i < args->length(); i++)
    stack.Append(args->at(i)->AsLiteral()->val());
  Val** sp = &stack.at(0);
  const char* error;
  assert(intrinsic->kind() != Intrinsic::MATCHPOSNS);
  assert(intrinsic->kind() != Intrinsic::MATCHSTRS);
  if (intrinsic->kind() == Intrinsic::MATCH) {
    // Compile the regular expression here and quietly fail to fold
    // if it does not compile - the code generator will emit an error later.
    string s = args->at(0)->as_string()->cpp_str(proc_);
    void* r = CompileRegexp(s.c_str(), &error);
    if (r == NULL)
      return x;
    error = Intrinsics::Match(proc_, sp, r);
    FreeRegexp(r);
  } else if (intrinsic->can_fail()) {
    error = (*(Intrinsic::CFunctionCanFail)(target))(proc_, sp);
  } else {
    (*(Intrinsic::CFunctionCannotFail)(target))(proc_, sp);
    error = NULL;
  }

  // If got an error, emit it as a warning and do not fold.
  if (error != NULL) {
    Warning(x->file_line(), error);
    return x;
  }
  // No error, must have only the result remaining on the fake stack.
  assert(sp == &stack.last());
  return Literal::New(proc_, x->file_line(), NULL, *sp);
}


Expr* ConstantFoldingVisitor::VisitConversion(Conversion* x) {
  x->VisitChildren(this);
  if (x->src()->AsLiteral() == NULL)
    return x;
  const List<Expr*>* params = x->params();
  for (int i = 0; i < params->length(); i++)
    if (params->at(i)->AsLiteral() == NULL)
      return x;
  if (!x->type()->is_basic())
    return x;

  // Fake a stack and call the conversion code.
  List<Val*> stack(proc_);
  stack.Append(x->src()->AsLiteral()->val());
  for (int i = 0; i < params->length(); i++)
    stack.Append(params->at(i)->AsLiteral()->val());
  Val** sp = &stack.at(0);
  const char* error = ConvOp::ConvertBasic(proc_, x->op(), sp, x->type());
  // If got an error, emit it as a warning and do not fold.
  if (error) {
    Warning(x->file_line(), error);
    return x;
  }
  // No error, must have only the result remaining on the fake stack.
  assert(sp == &stack.last());
  return Literal::New(proc_, x->file_line(), NULL, *sp);
}


Expr* ConstantFoldingVisitor::VisitDollar(Dollar* x) {
  // Either the length temp or the array is used, but not both.
  if (x->length_temp() != NULL)
    x->VisitLengthTemp(this);
  else
    x->VisitArray(this);

  if (x->array()->AsLiteral() == NULL)
    return x;
  Val* val = x->array()->AsLiteral()->val();
  int result;
  if (x->array()->type()->is_string()) {
    result = val->as_string()->num_runes();
  } else if (x->array()->type()->is_bytes()) {
    result = val->as_bytes()->length();
  } else {
    assert(false);
    return x;
  }
  return Literal::NewInt(proc_, x->file_line(), NULL, result);
}


Expr* ConstantFoldingVisitor::VisitRuntimeGuard(RuntimeGuard* x) {
  x->VisitChildren(this);
  if (x->guard()->AsLiteral() == NULL)
    return x;
  Val* val = x->guard()->AsLiteral()->val();
  if (val->as_bool()->val()) {
    return x->expr();
  } else {
    Warning(x->file_line(), x->msg());
    return x;
  }
}


Expr* ConstantFoldingVisitor::VisitIndex(Index* x) {
  // Should not be called for increment or store cases.
  x->VisitChildren(this);
  if (x->var()->AsLiteral() == NULL || x->index()->AsLiteral() == NULL)
    return x;
  Val* var = x->var()->AsLiteral()->val();
  szl_int index = x->index()->AsLiteral()->val()->as_int()->val();
  if (x->var()->type()->is_string()) {
    StringVal* s = var->as_string();
    if (index < 0 || index >= s->num_runes()) {
      Warning(x->file_line(),
              "index out of bounds (index = %lld, string length = %d)",
              index, s->length());
      return x;
    }
    return Literal::NewInt(proc_, x->file_line(), NULL, s->at(index));
  } else if (x->var()->type()->is_bytes()) {
    BytesVal* b = var->as_bytes();
    if (index < 0 || index >= b->length()) {
      Warning(x->file_line(),
              "index out of bounds (index = %lld, bytes length = %d)",
              index, b->length());
      return x;
    }
    return Literal::NewInt(proc_, x->file_line(), NULL, b->at(index));
  } else {
    assert(false);  // there are no array literals
    return x;
  }
}


Expr* ConstantFoldingVisitor::VisitNew(New* x) {
  x->VisitChildren(this);
  if (x->type()->is_string()) {
    if (x->init()->AsLiteral() == NULL || x->length()->AsLiteral() == NULL)
      return x;
    szl_int init = x->init()->AsLiteral()->val()->as_int()->val();
    szl_int nrunes = x->length()->AsLiteral()->val()->as_int()->val();
    Rune rune = init;
    if (rune != init) {
      // At runtime the value would be silently truncated; so unlike the
      // other warnings, this one does not describe an error that would occur
      // if the code were to be executed, but just a result that may be
      // unexpected.
      Warning(x->file_line(),
              "truncated value in new(string): %lld truncated to %d",
              init, rune);
    }
    if (!IsValidUnicode(rune)) {
      Warning(x->file_line(),
              "illegal unicode character U+%x creating new string", rune);
      return x;
    }
    if (nrunes < 0) {
      Warning(x->file_line(), "negative length in new(string): %lld", nrunes);
      return x;
    }
    if (nrunes > kMaxFoldedNewLength)
      return x;
    char buf[UTFmax];
    int w = runetochar(buf, &rune);
    StringVal* result = Factory::NewString(proc_, nrunes * w, nrunes);
    char* p = result->base();
    for (int i = 0; i < nrunes; i++) {
      memmove(p, buf, w);
      p += w;
    }
    return Literal::New(proc_, x->file_line(), NULL, result);
  } else if (x->type()->is_bytes()) {
    if (x->init()->AsLiteral() == NULL || x->length()->AsLiteral() == NULL)
      return x;
    szl_int init = x->init()->AsLiteral()->val()->as_int()->val();
    szl_int length = x->length()->AsLiteral()->val()->as_int()->val();
    if (init < 0 || init > 0xFF)
      Warning(x->file_line(),
              "truncated value in new(bytes): %lld truncated to %lld",
              init, (init & 0xFF));
    if (length < 0) {
      Warning(x->file_line(), "negative length in new(bytes): %lld", length);
      return x;
    }
    if (length > kMaxFoldedNewLength)
      return x;
    BytesVal* result = Factory::NewBytes(proc_, length);
    memset(result->base(), init, length);
    return Literal::New(proc_, x->file_line(), NULL, result);
  } else {
    return x;  // do not try to fold for arrays and maps
  }
}


Expr* ConstantFoldingVisitor::VisitSlice(Slice* x) {
  x->VisitChildren(this);
  if (x->var()->AsLiteral() == NULL || x->beg()->AsLiteral() == NULL ||
      x->end()->AsLiteral() == NULL)
    return x;
  Val* var = x->var()->AsLiteral()->val();
  szl_int beg = x->beg()->AsLiteral()->val()->as_int()->val();
  szl_int end = x->end()->AsLiteral()->val()->as_int()->val();
  if (x->type()->is_string()) {
    StringVal* s = var->as_string();
    if (beg < 0 || end > s->length() || beg > end)
      Warning(x->file_line(),
         "index out of bounds (indices = [%lld:%lld], string length = %d)",
         beg, end, s->length());
    s->intersect_slice(&beg, &end, s->num_runes());
    int num_runes = end - beg;
    beg = s->byte_offset(proc_, beg);
    end = s->byte_offset(proc_, end);
    StringVal* result = SymbolTable::string_form()->NewSlice(proc_,
                                     s, beg, end - beg, num_runes);
    return Literal::New(proc_, x->file_line(), NULL, result);
  } else if (x->type()->is_bytes()) {
    BytesVal* b = var->as_bytes();
    if (beg < 0 || end > b->length() || beg > end)
      Warning(x->file_line(),
         "index out of bounds (indices = [%lld:%lld], bytes length = %d)",
         beg, end, b->length());
    b->intersect_slice(&beg, &end, b->length());
    BytesVal* result = SymbolTable::bytes_form()->NewSlice(proc_,
                                    b, beg, end - beg);
    return Literal::New(proc_, x->file_line(), NULL, result);
  } else {
    assert(false);  // there are no array literals
    return x;
  }
}


// Replace static variable references with a folded version of their init
// expressions since this is the final value of the variable.
Expr* StaticVarFoldingVisitor::VisitVariable(Variable* x) {
  if (!x->is_static())
    return x;
  assert(x->var_decl()->init() != NULL);
  return x->var_decl()->init()->Visit(this);
}

}  // namespace sawzall
