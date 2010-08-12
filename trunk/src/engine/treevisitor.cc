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
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/protocolbuffers.h"
#include "engine/treevisitor.h"
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"

DEFINE_bool(print_rewritten_source, false, "print rewritten program source; "
            "output is a descriptive approximation only");

namespace sawzall {

// A couple of classes to manage indentation by the constructor/destructor pattern.
class NodeIndent {
 public:
  NodeIndent(TreeNodeVisitor* nodevisitor)
    : nodevisitor_(nodevisitor) {
    nodevisitor_->in();
  }

  ~NodeIndent() {
    nodevisitor_->out();
  }
 private:
  TreeNodeVisitor* nodevisitor_;
};


class TypeIndent {
 public:
  TypeIndent(TreeTypeVisitor* typevisitor)
    : typevisitor_(typevisitor) {
    typevisitor_->in();
  }

  ~TypeIndent() {
    typevisitor_->out();
  }
 private:
  TreeTypeVisitor* typevisitor_;
};


// Helper for debugging: comments aid assocation of break
// statement with the while, for, or switch statement it breaks.
static char* debug_tag(void* p) {
  static char tag[64];
  if (FLAGS_debug_whens)
    F.snprint(tag, sizeof tag, "  # %p", p);
  else
    tag[0] = '\0';
  return tag;
}


// Implementation of TreeNodeVisitor

TreeNodeVisitor::TreeNodeVisitor(Fmt::State* f, int indent)
 : f_(f),
   n_(0),
   indent_(indent) {
}


// convenient wrapper for all printing
void TreeNodeVisitor::P(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  n_ += F.fmtvprint(f_, fmt, &args);
  va_end(args);
}


void TreeNodeVisitor::DoTypeDecl(TypeDecl* x) {
  TypeName* tname = x->tname();
  Type* type = tname->type();
  P("%tTypeDecl\n", indent_);
  P("%t%q\n", indent_ + 1, tname->name());
  // Only use the full type definition if it was used in the declaration.
  if (type->type_name() == tname)
    P("%*#T", indent_ + 1, type);
  else
    P("%*T", indent_ + 1, type);
}


void TreeNodeVisitor::DoVarDecl(VarDecl* x) {
  P("%tVarDecl\n", indent_);
  NodeIndent indent(this);
  if (x->is_static())
    P("%tstatic\n", indent_);
  P("%t%q\n", indent_, x->name());
  if (x->AsQuantVarDecl() != NULL)
    P("%tQuantVar%q\n", indent_, x->AsQuantVarDecl()->KindAsString());
  if (x->init() != NULL) {
    // function definitions use a different syntax including printing
    // the full function type even if the type has a name
    if (x->init()->AsFunction() != NULL) {
      P("%*#T", indent_, x->type());
      P("%*N", indent_, x->init());
    } else {
      P("%*T", indent_, x->type());
      P("%*N", indent_, x->init());
    }
  } else {
    P("%*T", indent_, x->type());
  }
}


void TreeNodeVisitor::DoEmpty(Empty* x) {
  P("%tEmpty\n", indent_);
}


void TreeNodeVisitor::DoExprStat(ExprStat* x) {
  P("%tExprStat\n", indent_);
  P("%*N", indent_ + 1, x->expr());
}


void TreeNodeVisitor::DoIf(If* x) {
  P("%tIf\n", indent_);
  NodeIndent indent(this);
  P("%*N", indent_, x->cond());
  P("%*N", indent_, x->then_part());
  if (x->else_part() != NULL)
    P("%*N", indent_, x->else_part());
}


void TreeNodeVisitor::ForPart(Statement* stat) {
  if (stat == NULL) {
    DoEmpty(NULL);
  } else {
    P("%*N", indent_, stat);
  }
}


void TreeNodeVisitor::DoLoop(Loop* x) {
  P("%tLoop\n", indent_);
  NodeIndent indent(this);
  switch (x->sym()) {
    case DO: {
      P("%tdo %s\n", indent_, debug_tag(x));
      NodeIndent indent(this);
      P("%*N", indent_, x->body());
      P("%*N", indent_, x->cond());
      break;
    }

    case FOR: {
      P("%tfor %s\n", indent_, debug_tag(x));
      NodeIndent indent(this);
      ForPart(x->before());
      if (x->cond() != NULL)
        P("%*N", indent_, x->cond());
      else
        DoEmpty(NULL);
      ForPart(x->after());
     P("%*N", indent_, x->body());
      break;
    }

    case WHILE: {
      P("%twhile %s\n", indent_, debug_tag(x));
      NodeIndent indent(this);
      P("%*N", indent_, x->cond());
      P("%*N", indent_, x->body());
      break;
    }

    default:
      ShouldNotReachHere();
  }
}


void TreeNodeVisitor::DoBreak(Break* x) {
    P("%tBreak %s\n", indent_, debug_tag(x->stat()));
}


void TreeNodeVisitor::DoContinue(Continue* x) {
    P("%tContinue %s\n", indent_, debug_tag(x->loop()));
}


void TreeNodeVisitor::DoWhen(When* x) {
  if (FLAGS_print_rewritten_source) {
    P("%tWhen\n", indent_);
    NodeIndent indent(this);
    P("%*N", indent_, x->rewritten());
  } else {
    P("%twhen (", indent_);
    if (!x->qvars()->is_empty()) {
      P("\n");
      for (int i = 0; i < x->qvars()->num_entries(); i++) {
        QuantVarDecl* var = x->qvars()->entry_at(i)->AsQuantVarDecl();
        if (var != NULL)
          P("%t%s: %s %T;\n",
            indent_+1, var->name(), var->KindAsString(), var->type());
      }
      P("%t", indent_+1);
    }
    P("%N", x->cond());
    if (!x->qvars()->is_empty())
      P("\n%t", indent_);
    P(")\n%*N", indent_+1, x->body());
  }
}


void TreeNodeVisitor::DoEmit(Emit* x) {
  P("%tEmit\n", indent_);
  NodeIndent indent(this);
  P("%ttable:\n", indent_);
  P("%*N", indent_ + 1, x->output());
  if (x->indices()->length() > 0) {
    P("%tindices:\n", indent_);
    P("%tList<Expr*>\n", indent_ + 1);
    for (int i = 0; i < x->indices()->length(); i++)
      P("%*N", indent_ + 2, x->indices()->at(i));
  }
  P("%tvalue:\n", indent_);
  P("%*N", indent_ + 1, x->value());
  if (x->weight() != NULL) {
    P("%tweight:\n", indent_);
    P("%*N", indent_ + 1, x->weight());
  }
}


void TreeNodeVisitor::DoAssignment(Assignment* x) {
  P("%tAssignment\n", indent_);
  NodeIndent indent(this);
  P("%*N", indent_, x->lvalue());
  P("%*N", indent_, x->rvalue());
}


void TreeNodeVisitor::DoIncrement(Increment* x) {
  assert(x->delta() == 1 || x->delta() == -1);
  P("%tIncrement\n", indent_);
  NodeIndent indent(this);
  P("%*N", indent_, x->lvalue());
  P("%t%s\n", indent_, (x->delta() > 0 ? "++" : "--"));
}


void TreeNodeVisitor::DoProto(Proto* x) {
  P("%tProto\n", indent_);
  NodeIndent indent(this);
  P("%t%q\n", indent_, x->file());
  for (int i = 0; i < x->length(); i++)
    P("%*N", indent_, x->at(i));
}


void TreeNodeVisitor::DoResult(Result *x) {
  P("%tResult\n", indent_);
  NodeIndent indent(this);
  P("%*N", indent_, x->expr());
}


void TreeNodeVisitor::DoReturn(Return* x) {
  P("%tReturn\n", indent_);
  if (x->has_result())
    P("%*N", indent_, x->result());
}


void TreeNodeVisitor::DoSwitch(Switch* x) {
  P("%tSwitch %s\n", indent_, debug_tag(x));
  NodeIndent indent(this);
  P("%*N", indent_, x->tag());
  List<Case*>* cases = x->cases();
  for (int i = 0; i < cases->length(); i++) {
    Case* case_ = cases->at(i);
    P("%tcase\n", indent_);
    P("%*A", indent_ + 1, case_->labels());
    P("%*N", indent_ + 1, case_->stat());
  }
  P("%tdefault:\n", indent_);
  P("%*N", indent_ + 1, x->default_case());
}


void TreeNodeVisitor::DoBlock(Block* x) {
  P("%tBlock\n", indent_);
  NodeIndent indent(this);
  for (int i = 0; i < x->length(); i++)
    P("%*N", indent_, x->at(i));
}


void TreeNodeVisitor::DoSlice(Slice* x) {
  P("%tSlice\n", indent_);
  NodeIndent indent(this);
  P("%*N", indent_, x->var());
  P("%*N", indent_, x->beg());
  P("%*N", indent_, x->end());
}


void TreeNodeVisitor::DoStatExpr(StatExpr* x) {
  P("%tStatExpr\n", indent_);
  NodeIndent indent(this);
  P("%*N", indent_, x->body());
}


void TreeNodeVisitor::DoBadExpr(BadExpr* x) {
  P("%tBadExpr\n", indent_);
}


void TreeNodeVisitor::DoSelector(Selector* x) {
  P("%tSelector\n", indent_);
  NodeIndent indent(this);
  P("%*N", indent_, x->var());
  P("%*N", indent_, x->field());
}


void TreeNodeVisitor::DoRuntimeGuard(RuntimeGuard* x) {
  P("%tRuntimeGuard\n", indent_);
  P("%*N", indent_ + 1, x->expr());  // don't print guard - internal use only
}


void TreeNodeVisitor::DoIndex(Index* x) {
  P("%tIndex\n", indent_);
  NodeIndent indent(this);
  P("%*N", indent_, x->var());
  P("%*N", indent_, x->index());
}


void TreeNodeVisitor::DoBinary(Binary* x) {
  // is this a unary?
  switch (x->op()) {
    case Binary::EQL: {
      BoolVal* left = x->left()->as_bool();
      if (left != NULL && left->val() == false) {
        DoUnary(x);
        return;
      }
      break;
    }
    case Binary::SUB: {
      IntVal* left = x->left()->as_int();
      if (left != NULL && left->val() == 0) {
        DoUnary(x);
        return;
      }
      FloatVal* fleft = x->left()->as_float();
      if (fleft != NULL && fleft->val() == 0.0) {
        DoUnary(x);
        return;
      }
      break;
    }
    case Binary::BXOR: {
      IntVal* left = x->left()->as_int();
      if (left != NULL && left->val() == ~0) {
        DoUnary(x);
        return;
      }
      break;
    }
    default:
      break;  // only needed to silence g++ -Wall
  }
  P("%tBinary\n", indent_);
  NodeIndent indent(this);
  P("%t%s\n", indent_, Binary::Op2String(x->op()));
  P("%*N", indent_, x->left());
  P("%*N", indent_, x->right());
}


void TreeNodeVisitor::DoUnary(Binary* x) {
  const char *op = "unary?";
  switch (x->op()) {
    // unaries
    case Binary::EQL:
      op = "!";
      break;
    case Binary::SUB:
      op = "-";
      break;
    case Binary::BXOR:
      op = "~";
      break;
    default:
      ShouldNotReachHere();
  }
  P("%tUnary\n", indent_);
  NodeIndent indent(this);
  P("%t%s\n", indent_, Binary::Op2String(x->op()));
  P("%*N", indent_, x->right());
}


void TreeNodeVisitor::DoLiteral(Literal* x) {
  P("%tLiteral\n", indent_);
  NodeIndent indent(this);
  if (x->is_anonymous())
    P("%t%V\n", indent_, NULL, x->val());  // no proc, but we only need one for functions
  else
    P("%t%q\n", indent_, x->name());
}


void TreeNodeVisitor::DoDollar(Dollar* x) {
  P("%tDollar\n", indent_);
  NodeIndent indent(this);
  P("%*N", indent_, x->array());
}


void TreeNodeVisitor::DoFunction(Function* x) {
  P("%tFunction (Node)\n", indent_);
  NodeIndent indent(this);
  P("%*#T", indent_, x->type());
  P("%*N", indent_, x->body());
}


void TreeNodeVisitor::DoCall(Call* x) {
  P("%tCall\n", indent_);
  NodeIndent indent(this);
  P("%*N", indent_, x->fun());
  P("%*A", indent_, x->args());
}


void TreeNodeVisitor::DoConversion(Conversion* x) {
  P("%tConversion\n", indent_);
  NodeIndent indent(this);
  P("%*T", indent_, x->type());
  P("%*N", indent_, x->src());
  P("%*A", indent_, x->params());
}


void TreeNodeVisitor::DoNew(New* x) {
  P("%tNew\n", indent_);
  NodeIndent indent(this);
  assert(x->length() != NULL);
  P("%*T", indent_, x->type());
  P("%*N", indent_, x->length());
  if (x->init() != NULL)
    P("%*N", indent_, x->init());
}


void TreeNodeVisitor::DoRegex(Regex* x) {
  P("%tRegex\n", indent_);
  NodeIndent indent(this);
  P("%*T", indent_, x->arg());
  if (x->base() != NULL)
    P("%*N", indent_, x->base());
}


void TreeNodeVisitor::DoSaw(Saw* x) {
  P("%tSaw\n", indent_);
  NodeIndent indent(this);
  assert(x->args()->length() == x->flags()->length());
  P("%t%q\n", indent_, Saw::Kind2String(x->kind()));
  if (x->kind() == Saw::SAWN)
    P("%*N", indent_, x->count());
  P("%*N", indent_, x->str());
  for (int i = 0; i < x->args()->length(); i++) {
    const char* flag = Saw::Flag2String(x->flags()->at(i));
    if (flag[0] != '\0')
      P("%t%s:\n", indent_, flag);
    P("%*N", indent_, x->args()->at(i));
  }
}


void TreeNodeVisitor::DoComposite(Composite* x) {
  P("%tComposite\n", indent_);
  NodeIndent indent(this);
  P("%*T", indent_, x->type());
  for (int i = 0; i < x->length(); i++)
    P("%*N", indent_, x->at(i));
}


void TreeNodeVisitor::DoVariable(Variable* x) {
  P("%tVariable\n", indent_);
  NodeIndent indent(this);
  // for a variable declared as a static in a tuple, print the qualifier
  TupleType* tuple = x->var_decl()->tuple();
  if (tuple != NULL) {
    P("%*T", indent_, tuple);
    P("%t.\n", indent_);
  }
  P("%t%q\n", indent_, x->name());
}


void TreeNodeVisitor::DoTempVariable(TempVariable* x) {
  P("%tTempVariable\n", indent_);
  P("%*N", indent_ + 1, x->init());
}


void TreeNodeVisitor::DoField(Field* x) {
  P("%tField\n", indent_);
  P("%t%q\n", indent_ + 1, x->name());
}


void TreeNodeVisitor::DoIntrinsic(Intrinsic* x) {
  P("%tIntrinsic\n", indent_);
  P("%t%q\n", indent_ + 1, x->name());
}


void TreeNodeVisitor::DoTypeName(TypeName* x) {
  P("%tTypeName\n", indent_);
  P("%t%q\n", indent_ + 1, x->name());
}


// Implementation of TreeTypeVisitor.

TreeTypeVisitor::TreeTypeVisitor(Fmt::State* f, int indent)
 : f_(f),
   n_(0),
   indent_(indent) {
}


void TreeTypeVisitor::P(const char* fmt ...) {
  va_list args;
  va_start(args, fmt);
  n_ += F.fmtvprint(f_, fmt, &args);
  va_end(args);
}


void TreeTypeVisitor::Psharp(const char* fmt ...) {
  va_list args;
  va_start(args, fmt);
  int sharp = f_->flags & Fmt::FmtSharp;
  n_ += F.fmtvprint(f_, fmt, &args);
  f_->flags = sharp;  // print operations clear flags
  va_end(args);
}


void TreeTypeVisitor::DoArrayType(ArrayType *t) {
  Psharp("%tArrayType\n", indent_);
  if ((f_->flags & Fmt::FmtSharp) == 0 && t->type_name() != NULL) {
    if (t->enclosing_tuple() != NULL)
      P("%*T", indent_ + 1, t->enclosing_tuple());
    P("%t%s\n", indent_ + 1, t->type_name()->name());
  } else {
    DoField(t->elem());
  }
}


void TreeTypeVisitor::DoBadType(BadType *t) {
  P("%tBadType\n", indent_);
}


void TreeTypeVisitor::DoBasicType(BasicType *t) {
  assert(t->type_name() != NULL);
  P("%tBasicType\n", indent_);
  P("%t%s\n", indent_ + 1, t->type_name()->name());
}


void TreeTypeVisitor::DoFunctionType(FunctionType* t) {
  Psharp("%tFunction (Type)\n", indent_);
  TypeIndent indent(this);
  if ((f_->flags & Fmt::FmtSharp) == 0 && t->type_name() != NULL) {
    if (t->enclosing_tuple() != NULL)
      P("%*T", indent_, t->enclosing_tuple());
    P("%t%s\n", indent_, t->type_name()->name());
  } else {
    f_->flags &= ~Fmt::FmtSharp;
    List<Field*>* params = t->parameters();
    for (int i = 0; i < params->length(); i++) {
      Field* param = params->at(i);
      if (param->has_value())
        break;
      P("%targ:\n", indent_);
      TypeIndent indent(this);
      if (param->has_name())
        P("%t%q\n", indent_, param->name());
      P("%*T", indent_, param->type());
    }
    if (t->has_result()) {
      P("%tresult:\n", indent_);
      P("%*T", indent_ + 1, t->result_type());
    }
  }
}


void TreeTypeVisitor::DoIncompleteType(IncompleteType* t) {
  P("%tIncompleteType\n", indent_);
}


void TreeTypeVisitor::DoMapType(MapType* t) {
  Psharp("%tMap\n", indent_);
  TypeIndent indent(this);
  if ((f_->flags & Fmt::FmtSharp) == 0 && t->type_name() != NULL) {
    if (t->enclosing_tuple() != NULL)
      P("%*T", indent_, t->enclosing_tuple());
    P("%t%s\n", indent_, t->type_name()->name());
  } else {
    f_->flags &= ~Fmt::FmtSharp;
    P("%tkey:\n", indent_);
    DoField(t->index());
    P("%tvalue:\n", indent_);
    DoField(t->elem());
  }
}


void TreeTypeVisitor::DoOutputType(OutputType *t) {
  Psharp("%tOutputType\n", indent_);
  TypeIndent indent(this);
  if ((f_->flags & Fmt::FmtSharp) == 0 && t->type_name() != NULL) {
    if (t->enclosing_tuple() != NULL)
      P("%*T", indent_, t->enclosing_tuple());
    P("%t%s\n", indent_, t->type_name()->name());
  } else {
    f_->flags &= ~Fmt::FmtSharp;
    P("%t%q\n", indent_, t->kind()->name());
    if (t->param() != NULL) {
      P("%tparameter:\n", indent_, t->param());
      P("%*N", indent_ + 1, t->param());
    }

    List<VarDecl*>* indices = t->index_decls();
    if (indices->length() > 0)
      P("%tindices:\n", indent_);
    for (int i = 0; i < indices->length(); i++) {
      VarDecl* index_decl = indices->at(i);
      if (index_decl->name() != NULL)
        P("%t%q:\n", indent_, index_decl->name());
      P("%*T", indent_ + 1, index_decl->type());
    }

    P("%tvalue-type:\n", indent_);
    VarDecl* elem_decl = t->elem_decl();
    if (elem_decl->name() != NULL)
      P("%t%q:\n", indent_ + 1, elem_decl->name());
    P("%*T", indent_ + 1, elem_decl->type());

    if (t->weight() != NULL) {
      P("%tweight:\n", indent_);
      DoField(t->weight());
    }

    if (t->index_format_args() != NULL) {
      P("%t%s\n", indent_, (t->is_proc() ? "proc:" : "file:"));
      P("%*A", indent_, t->index_format_args());
    }

    if (t->elem_format_args() != NULL) {
      P("%telem-format:\n", indent_);
      P("%*An", indent_, t->elem_format_args());
    }
  }
}


void TreeTypeVisitor::DoField(Field* f) {
  TypeIndent indent(this); // We're always nesting here
  // Only used for fields within a tuple type.
  // There is no corresponding declaration node, but print as if there were.
  if (!f->is_anonymous())
    P("%t%q:\n", indent_, f->name());
  // If we have a recursive reference involving an unnamed type (which is not
  // supposed to happen), this will loop.
  if (f_->flags & Fmt::FmtSharp)
    P("%#*T", indent_, f->type());
  else
    P("%*T", indent_, f->type());
  if (f->has_value()) {
    P("%tvalue:\n", indent_);
    P("%*N", indent_+1, f->value());
  }
  if (f->has_tag())
    P("%ttag: %d\n", indent_, f->tag());
  if (f->pb_type() != PBTYPE_UNKNOWN)
    P("%t : %q\n", indent_, protocolbuffers::ProtoBufferTypeName(f->pb_type()));
}


void TreeTypeVisitor::DoTupleType(TupleType *t) {
  Psharp("%tTupleType\n", indent_);
  TypeIndent indent(this);
  // The '#' flag forces the contents to be printed, even if the tuple is named.
  // The '+' flag (when '#' is present) forces any static and type declarations
  // to be printed.
  bool fmtsharp = (f_->flags & Fmt::FmtSharp) != 0;
  f_->flags &= ~Fmt::FmtSharp;

  // If named and no sharp flag, just print the name.
  if (t->type_name() != NULL && !fmtsharp) {
    if (t->enclosing_tuple() != NULL)
      P("%*T", indent_, t->enclosing_tuple());
    P("%t%q\n", indent_, t->type_name()->name());
  } else {
    if (t->is_message())
      P("%tparsedmessage\n", indent_);
    // Note that we use the scope, not the field list.
    for (int i = 0; i < t->scope()->num_entries(); i++) {
      Object* obj = t->scope()->entry_at(i);
      if (obj->AsField() != NULL) {
        // normal tuple field
        --indent_;  // DoField already indents
        DoField(obj->AsField());
        indent_++;
      } else if (obj->AsVarDecl() != NULL) {
        // static declaration at tuple scope
        P("%*N", indent_, obj->AsVarDecl());
      } else if (obj->AsTypeName() != NULL) {
        // type declaration at tuple scope
        TypeName* tname = obj->AsTypeName();
        Type* type = tname->type();
        // Only use the full type definition if it was used in the declaration.
        P("%t%q\n", indent_, tname->name());
        if (type->type_name() == tname)
          P("%*#T", indent_, type);
        else
          P("%*T", indent_, type);
      } else {
        ShouldNotReachHere();
      }
    }
  }
}

}  // namespace sawzall
