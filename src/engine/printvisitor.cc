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
#include "engine/printvisitor.h"
#include "engine/treevisitor.h"
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"

DEFINE_bool(print_tree, false, "generate tree output (default is source code)");
DEFINE_bool(print_proto_clauses, false,
            "print proto clauses instead of expanded generated code");

namespace sawzall {

// helper for debugging: comments aid assocation of break
// statement with the while, for, or switch statement it breaks.
static char* debug_tag(void* p) {
  static char tag[64];
  if (FLAGS_debug_whens)
    F.snprint(tag, sizeof tag, "  # %p", p);
  else
    tag[0] = '\0';
  return tag;
}

// Implementation of PrintNodeVisitor

PrintNodeVisitor::PrintNodeVisitor(Fmt::State* f, int indent)
   : f_(f), n_(0), indent_(indent) {
}


// print N tabs; N is argument ("%t", N)
int PrintNodeVisitor::TabFmt(Fmt::State* f) {
  int n = FMT_ARG(f, int);
  const char* tab = FLAGS_print_tree? ". " : "\t";
  for (int i = 0; i < n; i++)
    F.fmtprint(f, tab);
  return 0;
}


// prettyprint Node; width is indent ("%4N", nodep)
int PrintNodeVisitor::NodeFmt(Fmt::State* f) {
  Node *n = FMT_ARG(f, Node*);
  if (n == NULL)
    return 0;

  if (FLAGS_print_tree) {
    TreeNodeVisitor p(f, f->width);
    n->Visit(&p);
    return p.n();
  } else {
    PrintNodeVisitor p(f, f->width);
    n->Visit(&p);
    return p.n();
  }
}


// convenient wrapper for all printing
void PrintNodeVisitor::P(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  n_ += F.fmtvprint(f_, fmt, &args);
  va_end(args);
}


// print parenthesized argument list
int PrintNodeVisitor::ArgSzlListFmt(Fmt::State* f) {
  List<Expr*> *x = FMT_ARG(f, List<Expr*>*);
  int n = 0;
  if (FLAGS_print_tree) {
    int width = f->width;
    F.fmtprint(f, "%tList<Expr*>\n", width);
    for (int i = 0; i < x->length(); i++)
      n += F.fmtprint(f, "%*N", width + 1, x->at(i));
  } else {
    n += F.fmtprint(f, "(");

    for (int i = 0; i < x->length(); i++) {
      if (i > 0)
        n += F.fmtprint(f, ", ");
      n += F.fmtprint(f, "%N", x->at(i));
    }

    n += F.fmtprint(f, ")");
  }
  return n;
}


void PrintNodeVisitor::DoTypeDecl(TypeDecl* x) {
  TypeName* tname = x->tname();
  Type* type = tname->type();
  // Only use the full type definition if it was used in the declaration.
  if (x->print_expanded())
    P("%ttype %s = %#T;\n", indent_, tname->name(), type);
  else
    P("%ttype %s = %T;\n", indent_, tname->name(), type);
}


void PrintNodeVisitor::DoVarDecl(VarDecl* x) {
  if (x->tuple() == NULL)  // (static) decls within tuples not on separate lines
    P("%t", indent_);
  // tables are implicitly static, skip explicit keyword
  if (x->is_static() && !x->type()->is_output())
    P("static ");
  P("%s: ", x->name());
  if (x->AsQuantVarDecl() != NULL)
    P("%s ", x->AsQuantVarDecl()->KindAsString());
  if (x->init() != NULL) {
    // function definitions use a different syntax including printing
    // the full function type even if the type has a name
    if (x->init()->AsFunction() != NULL)
      P("%#T %*N", x->type(), indent_+1, x->init()->AsFunction()->body());
    else
      P("%T = %N", x->type(), x->init());
  } else {
    P("%T", x->type());
  }
  if (x->tuple() == NULL)
    P(";\n");
}


void PrintNodeVisitor::DoEmpty(Empty* x) {
  P("%t;\n", indent_);
}


void PrintNodeVisitor::DoExprStat(ExprStat* x) {
  P("%t%N;\n", indent_, x->expr());
}


void PrintNodeVisitor::DoIf(If* x) {
  P("%tif (%N)\n", indent_, x->cond());
  P("%*N", indent_+1, x->then_part());
  if (x->else_part() != NULL && x->else_part()->AsEmpty() == NULL)
    P("%telse\n%*N", indent_, indent_+1, x->else_part());
}


void PrintNodeVisitor::ForPart(Statement* stat) {
  if (stat != NULL) {
    Assignment* ass = stat->AsAssignment();
    if (ass != NULL) {
      P("%N = %N", ass->lvalue(), ass->rvalue());
      return;
    }
    Increment* inc = stat->AsIncrement();
    if (inc != NULL) {
      assert(inc->delta() == 1 || inc->delta() == -1);
      P("%N%s", inc->lvalue(), (inc->delta() > 0 ? "++" : "--"));
      return;
    }
    VarDecl* decl = stat->AsVarDecl();
    if (decl != NULL) {
      P("%s: %T", decl->name(), decl->type());
      if (decl->init())
        P(" = %N", decl->init());
      return;
    }
    ExprStat* expr = stat->AsExprStat();
    if (expr != NULL) {
      P("%N", expr->expr());
    }
  }
}


void PrintNodeVisitor::DoLoop(Loop* x) {
  switch (x->sym()) {
    case DO:
      P("%tdo\n", indent_);
      P("%*N", indent_+1, x->body());
      P("%twhile (%N);%s\n", indent_, x->cond(), debug_tag(x));
      break;

    case FOR:
      // We know the before and after are VarDecls or Assignments; handle them
      // specially to avoid spurious newlines and semicolons
      P("%tfor (", indent_);
      if (x->before() != NULL)
        ForPart(x->before());
      P("; ");
      if (x->cond() != NULL)
        P("%N", x->cond());
      P("; ");
      if (x->after() != NULL)
        ForPart(x->after());
      P(")%s\n%*N", debug_tag(x), indent_+1, x->body());
      break;

    case WHILE:
      P("%twhile (%N)%s\n", indent_, x->cond(), debug_tag(x));
      P("%*N", indent_+1, x->body());
      break;

    default:
      ShouldNotReachHere();
  }
}


void PrintNodeVisitor::DoBreak(Break* x) {
    P("%tbreak;%s\n", indent_, debug_tag(x->stat()));
}


void PrintNodeVisitor::DoContinue(Continue* x) {
    P("%tcontinue;%s\n", indent_, debug_tag(x->loop()));
}


void PrintNodeVisitor::DoWhen(When* x) {
  P("%twhen (", indent_);
  if (!x->qvars()->is_empty()) {
    P("\n");
    for (int i = 0; i < x->qvars()->num_entries(); i++) {
      QuantVarDecl *var = x->qvars()->entry_at(i)->AsQuantVarDecl();
      // When rewriting will introduce variables here; suppress them.
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


void PrintNodeVisitor::DoEmit(Emit* x) {
  P("%temit %N", indent_, x->output());

  for (int i = 0; i < x->indices()->length(); i++)
    P("[%N]", x->indices()->at(i));

  P(" <- %N", x->value());

  if (x->weight() != NULL)
    P(" weight %N", x->weight());

  P(";\n");
}


void PrintNodeVisitor::DoAssignment(Assignment* x) {
  P("%t%N = %N;\n", indent_, x->lvalue(), x->rvalue());
}


void PrintNodeVisitor::DoIncrement(Increment* x) {
  assert(x->delta() == 1 || x->delta() == -1);
  P("%t%N%s;\n", indent_, x->lvalue(), (x->delta() > 0 ? "++" : "--"));
}


void PrintNodeVisitor::DoResult(Result* x) {
  P("%tresult %N;\n", indent_, x->expr());
}


void PrintNodeVisitor::DoReturn(Return* x) {
  if (x->has_result())
    P("%treturn %N;\n", indent_, x->result());
  else
    P("%treturn;\n", indent_);
}


void PrintNodeVisitor::DoSwitch(Switch* x) {
  P("%tswitch (%N) {%s\n", indent_, x->tag(), debug_tag(x));
  indent_++;
  List<Case*>* cases = x->cases();
  for (int i = 0; i < cases->length(); i++) {
    Case* case_ = cases->at(i);
    P("%tcase ", indent_);
    List<Expr*>* labels = case_->labels();
    for (int i = 0; i < labels->length(); i++) {
      if (i > 0)
        P(", ");
      P("%N", labels->at(i));
    }
    P(":\n");
    indent_++;
    P("%t%N", indent_, case_->stat());
    indent_--;
  }
  // default
  P("%tdefault:\n", indent_);
  indent_++;
  P("%t%N", indent_, x->default_case());
  indent_--;
  indent_--;
  P("%t}\n", indent_);
}


void PrintNodeVisitor::DoBlock(Block* x) {
  // put braces at previous indent level.
  if (!x->is_program())
    P("%t{\n", indent_-1);

  for (int i = 0; i < x->length(); i++) {
    int is_block = x->at(i)->AsBlock() != NULL;  // nested blocks
    P("%*N", indent_ + is_block, x->at(i));
  }

  if (!x->is_program())
    P("%t}\n", indent_-1);
}


void PrintNodeVisitor::DoProto(Proto* x) {
  // Skip proto clauses that generated no statements because they were
  // empty or ignored (multiple inclusion)
  if (x->length() == 0)
    return;

  if (FLAGS_print_proto_clauses)
    P("%tproto %q\n", indent_, x->file());
  else
    x->VisitChildren(this);
}


void PrintNodeVisitor::DoSlice(Slice* x) {
  P("%N[%N : %N]", x->var(), x->beg(), x->end());
}


void PrintNodeVisitor::DoStatExpr(StatExpr* x) {
  P("?%*N", indent_+1, x->body());
}


void PrintNodeVisitor::DoBadExpr(BadExpr* x) {
  P("BadExpr(%N)", x->node());
}


void PrintNodeVisitor::DoSelector(Selector* x) {
  P("%N.%N", x->var(), x->field());
}


void PrintNodeVisitor::DoRuntimeGuard(RuntimeGuard* x) {
  P("%N", x->expr());  // don't print guard - internal use only
}


void PrintNodeVisitor::DoIndex(Index* x) {
  P("%N[%N]", x->var(), x->index());
}


void PrintNodeVisitor::DoBinary(Binary* x) {
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
  const char *op = Binary::Op2String(x->op());
  if (f_->r == 'P')
    P("(%P %s %P)", x->left(), op, x->right());
  else
    P("%P %s %P", x->left(), op, x->right());
}


void PrintNodeVisitor::DoUnary(Binary* x) {
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
  if (f_ ->r == 'P')
    P("(%s%P)", op, x->right());
  else
    P("%s%P", op, x->right());
}


void PrintNodeVisitor::DoLiteral(Literal* x) {
  if (x->is_anonymous())
    if (x->type()->is_time())  // remain compatible with old code
      P("%lluT", x->as_time()->val());
    else
      P("%V", NULL, x->val());  // no proc, but we only need one for functions
  else
    P("%s", x->name());
}


void PrintNodeVisitor::DoDollar(Dollar* x) {
  P("$");
}


void PrintNodeVisitor::DoFunction(Function* x) {
  if ((f_->flags & Fmt::FmtSharp) == 0 && x->name() != NULL) {
    P("%s", x->name());
  } else {
    // print the full type, even if it has a name
    // (not necessary, but it makes the function easier to read)
    P("%#T %*N", x->type(), indent_+1, x->body());
  }
}


void PrintNodeVisitor::DoCall(Call* x) {
  P("%N(", x->fun());
  const List<Expr*> *args = x->args();
  int count =  x->source_arg_count();
  assert(count <= args->length());
  for (int i = 0; i < count; i++) {
    if (i > 0)
      P(", ");
    P("%N", args->at(i));
  }
  P(")");
}


void PrintNodeVisitor::DoConversion(Conversion* x) {
  Expr* src = x->src();
  Type* type = x->type();
  P("convert(%T, %N", type, src);

  // add any explicit parameters
  const List<Expr*> *params = x->params();
  int count =  x->source_param_count();
  assert(count <= params->length());
  for (int i = 0; i < count; i++)
    P(", %N", params->at(i));
  P(")");
}


void PrintNodeVisitor::DoNew(New* x) {
  assert(x->length() != NULL);
  if (x->init() != NULL)
    P("new(%T, %N, %N)", x->type(), x->length(), x->init());
  else
    P("new(%T, %N)", x->type(), x->length());
}


void PrintNodeVisitor::DoRegex(Regex* x) {
  if (x->base() != NULL)
    P("regex(%T, %N)", x->arg(), x->base());
  else
    P("regex(%T)", x->arg());
}


void PrintNodeVisitor::DoSaw(Saw* x) {
  assert(x->args()->length() == x->flags()->length());
  P("%s(", Saw::Kind2String(x->kind()));
  if (x->kind() == Saw::SAWN)
    P("%N, ", x->count());
  P("%N", x->str());
  for (int i = 0; i < x->args()->length(); i++)
    P(", %s%N", Saw::Flag2String(x->flags()->at(i)), x->args()->at(i));
  P(")");
}


void PrintNodeVisitor::DoComposite(Composite* x) {
  if (x->has_conversion())
    P("convert(%T, ", x->type());
  P("{");

  for (int i = 0; i < x->length(); i++) {
    if (i > 0) {
      if ((i & 1) && x->has_pairs())
        P(": ");
      else
        P(", ");
    }
    P("%N", x->at(i));
  }

  // make sure we print empty maps properly
  if (x->length() == 0 && x->has_pairs())
    P(":");

  P("}");
  if (x->has_conversion())
    P(")");
}


void PrintNodeVisitor::DoVariable(Variable* x) {
  // for a variable declared as a static in a tuple, print the qualifier
  TupleType* tuple = x->var_decl()->tuple();
  if (tuple != NULL) {
    P("%T", tuple);
    P(".");
  }
  P("%s", x->name());
}


void PrintNodeVisitor::DoTempVariable(TempVariable* x) {
  // should never be printed more than once in practice
  P("%N", x->init());
}


void PrintNodeVisitor::DoField(Field* x) {
  P("%s", x->name());
}


void PrintNodeVisitor::DoIntrinsic(Intrinsic* x) {
  P("%s", x->name());
}


void PrintNodeVisitor::DoTypeName(TypeName* x) {
  P("%s", x->name());
}


// Implementation of PrintTypeVisitor

PrintTypeVisitor::PrintTypeVisitor(Fmt::State* f, int indent)
   : f_(f), n_(0), indent_(indent), in_auto_proto_tuple_(false) {
}


int PrintTypeVisitor::TypeFmt(Fmt::State* f) {
  Type *t = FMT_ARG(f, Type*);
  if (t == NULL)
    return 0;

  // Width is initial indent, e.g. %2T type ==> %t%T, indent, type
  if (FLAGS_print_tree) {
    TreeTypeVisitor p(f, f->width);
    t->Visit(&p);
    return p.n();
  } else {
    PrintTypeVisitor p(f, f->width);
    t->Visit(&p);
    return p.n();
  }
}


void PrintTypeVisitor::P(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  n_ += F.fmtvprint(f_, fmt, &args);
  va_end(args);
}


void PrintTypeVisitor::DoArrayType(ArrayType *t) {
  if ((f_->flags & Fmt::FmtSharp) == 0 && t->type_name() != NULL) {
    if (t->enclosing_tuple() != NULL)
      P("%T.", t->enclosing_tuple());
    P("%s", t->type_name()->name());
  } else {
    f_->flags &= ~Fmt::FmtSharp;
    P("array of ");
    DoField(t->elem());
  }
}


void PrintTypeVisitor::DoBadType(BadType *t) {
  P("BadType");
}


void PrintTypeVisitor::DoBasicType(BasicType *t) {
  assert(t->type_name() != NULL);
  P("%s", t->type_name()->name());
}


void PrintTypeVisitor::DoFunctionType(FunctionType* t) {
  if ((f_->flags & Fmt::FmtSharp) == 0 && t->type_name() != NULL) {
    if (t->enclosing_tuple() != NULL)
      P("%T.", t->enclosing_tuple());
    P("%s", t->type_name()->name());
  } else {
    f_->flags &= ~Fmt::FmtSharp;
    List<Field*>* params = t->parameters();
    P("function(");
    for (int i = 0; i < params->length(); i++) {
      Field* param = params->at(i);
      // TODO: consider displaying the optional parameters too, perhaps with
      // special syntax, e.g. "f(int[, int])", to improve error messages.
      // Although this is not valid Sawzall syntax, we should never generate the
      // type of an intrinsic function for "--print_source" so it should be OK.
      if (param->has_value())
        break;
      if (i > 0)
        P(", ");
      if (param->has_name())
        P("%s: ", param->name());
      P("%T", param->type());
    }
    P(")");
    if (t->has_result())
      P(": %T", t->result_type());
  }
}


void PrintTypeVisitor::DoIncompleteType(IncompleteType* t) {
  P("incomplete");
}


void PrintTypeVisitor::DoMapType(MapType* t) {
  if ((f_->flags & Fmt::FmtSharp) == 0 && t->type_name() != NULL) {
    if (t->enclosing_tuple() != NULL)
      P("%T.", t->enclosing_tuple());
    P("%s", t->type_name()->name());
  } else {
    f_->flags &= ~Fmt::FmtSharp;
    P("map [");
    DoField(t->index());
    P("] of ");
    DoField(t->elem());
  }
}


void PrintTypeVisitor::DoOutputType(OutputType *t) {
  if ((f_->flags & Fmt::FmtSharp) == 0 && t->type_name() != NULL) {
    if (t->enclosing_tuple() != NULL)
      P("%T.", t->enclosing_tuple());
    P("%s", t->type_name()->name());
  } else {
    f_->flags &= ~Fmt::FmtSharp;
    P("table %s", t->kind()->name());
    if (t->param() != NULL)
      P("(%N)", t->param());

    List<VarDecl*>* index_decls = t->index_decls();
    for (int i = 0; i < index_decls->length(); i++) {
      VarDecl* index_decl = index_decls->at(i);
      P("[");
      if (index_decl->name() != NULL)
        P("%s: ", index_decl->name());
      P("%T", index_decl->type());
      P("]");
    }

    P(" of ");
    if (t->elem_decl()->name() != NULL)
      P("%s: ", t->elem_decl()->name());
    P("%T", t->elem_decl()->type());

    if (t->weight() != NULL) {
      P(" weight ");
      DoField(t->weight());
    }

    if (t->index_format_args() != NULL)
      P(" %s%A", (t->is_proc() ? "proc" : "file"), t->index_format_args());

    if (t->elem_format_args() != NULL)
      P(" format%A", t->elem_format_args());
  }
}


void PrintTypeVisitor::DoField(Field* f) {
  // Only used for fields within a tuple type.
  // There is no corresponding declaration node, but print as if there were.
  if (!f->is_anonymous())
    P("%s: ", f->name());
  // If we have a recursive reference involving an unnamed type (which is not
  // supposed to happen), this will loop.
  P("%T", f->type());
  if (f->has_value())
    P(" = %N", f->value());
  // don't print tags for fields of the automatic proto tuple types
  if (f->has_tag() && !in_auto_proto_tuple_)
    P(" @ %d", f->tag());
  if (f->pb_type() != PBTYPE_UNKNOWN)
    P(" : %s", protocolbuffers::ProtoBufferTypeName(f->pb_type()));
}


void PrintTypeVisitor::DoTupleType(TupleType *t) {
  // The '#' flag forces the contents to be printed, including static and
  // type declarations.
  bool fmtsharp = (f_->flags & Fmt::FmtSharp) != 0;
  f_->flags &= ~Fmt::FmtSharp;
  in_auto_proto_tuple_ = t->is_auto_proto();
  // If named and no sharp flag, just print the name.
  if (t->type_name() != NULL && !fmtsharp) {
    if (t->enclosing_tuple() != NULL)
      P("%T.", t->enclosing_tuple());
    P("%s", t->type_name()->name());
  } else {
    if (t->is_auto_proto())
      P("proto ");
    else if (t->is_message())
      P("parsedmessage ");
    // Note that we use the scope, not the field list.
    P("{");
    const char* comma = "";
    for (int i = 0; i < t->scope()->num_entries(); i++) {
      Object* obj = t->scope()->entry_at(i);
      if (obj->AsField() != NULL) {
        // normal tuple field
        P(comma);
        // DoField called directly (vs through P()), making it possible for
        // DoField to access in_auto_proto_tuple_. P() causes a new printvisitor
        // to be created via static method NodeFmt() making it not possible for
        // new printvisitor to access previous one.
        DoField(obj->AsField());
        comma = ", ";
      } else if (obj->AsVarDecl() != NULL) {
        // static declaration at tuple scope
        P("%s%N", comma, obj->AsVarDecl());
        comma = ", ";
      } else if (obj->AsTypeName() != NULL) {
        // type declaration at tuple scope
        TypeName* tname = obj->AsTypeName();
        Type* type = tname->type();
        // Only use the full type definition if it was used in the declaration.
        if (type->type_name() == tname)
          P("%stype %s = %#T", comma, tname->name(), type);
        else
          P("%stype %s = %T", comma, tname->name(), type);
        comma = ", ";
      } else {
        ShouldNotReachHere();
      }
    }
    P("}");
  }
}

}  // namespace sawzall
