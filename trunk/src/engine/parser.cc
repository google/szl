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
#include <string>

#include "engine/globals.h"
#include "public/commandlineflags.h"
#include "public/logging.h"

#include "engine/tracer.h"
#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/parser.h"
#include "engine/constantfolding.h"
#include "engine/when.h"
#include "engine/ir.h"
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/protocolbuffers.h"


DEFINE_bool(test_function_cloning, false,
            "use cloned copies of all functions for testing purposes");

DEFINE_bool(enable_proto_conversion_hack, false,
            "temporary flag during proto conversion: "
            "allow unit<->int, string<->bytes conversion. "
            "This flag will go away around 2010/07/01.");


namespace sawzall {


Parser::Parser(Proc* proc, Source* source, SymbolTable* table)
  : proc_ (proc),
    table_(table),
    scanner_(proc, source),
    sym_(SCANEOF),
    quants_(proc),
    tlevel_("parser"),
    nonstatic_var_refs_(0),
    scope_stack_(proc),
    function_stack_(proc),
    statexpr_stack_(proc) {
  // put universe scope on top
  scope_stack_.push(SymbolTable::universe());
}


void Parser::Errorv(bool is_warning, const char* fmt, va_list* args) {
  scanner_.Errorv(is_warning, fmt, args);
}


void Parser::Error(const char* fmt ...) {
  va_list args;
  va_start(args, fmt);
  Errorv(false, fmt, &args);
  va_end(args);
}


void Parser::Warning(const char* fmt ...) {
  va_list args;
  va_start(args, fmt);
  Errorv(true, fmt, &args);
  va_end(args);
}


void Parser::MarkLvalue(Expr* expr, bool also_rvalue) {
  Variable* lvar = IR::RootVar(expr);
  if (lvar != NULL) {
    lvar->var_decl()->set_modified_after_init();
    if (lvar->level() != top_level() || InStatExpr())
      lvar->var_decl()->set_modified_at_call();
    lvar->set_is_lvalue();
    // index/slice/selector base is always an rvalue too
    if (lvar == expr && !also_rvalue)
      lvar->clear_is_rvalue();
  }
}


Scope* Parser::OpenScope() {
  Scope* scope = Scope::New(proc_);
  scope_stack_.push(scope);
  return scope;
}


void Parser::ReopenScope(Scope* scope) {
  assert(scope != NULL);
  scope_stack_.push(scope);
}


void Parser::CloseScope(Scope* scope) {
  Scope* actual = scope_stack_.pop();
  assert(actual == scope);
}


Scope* Parser::OpenFunctionScope(Function* fun) {
  Scope* scope = OpenScope();
  // The "false" indicates we are not in an enclosed static declaration.
  FunctionAndFlag fun_and_flag = { fun, statexpr_stack_.length(), false };
  function_stack_.push(fun_and_flag);
  return scope;
}


void Parser::CloseFunctionScope(Scope* scope) {
  function_stack_.pop();
  CloseScope(scope);
}


Object* Parser::ExistingDeclaration(Position* start, szl_string name) {
  Object* obj = NULL;
  for (int i = 0; i < scope_stack_.length() && obj == NULL; i++)
    obj = scope_stack_.nth_top(i)->Lookup(name);
  return obj;
}


Object* Parser::Lookup(Position* start, szl_string name) {
  // the scanner must be positioned at the symbol after the name,
  // and "start" must refer to the name
  Object* obj = ExistingDeclaration(start, name);
  if (obj == NULL && sym_ == PERIOD)
    obj = ParseAndLookupPackageQualifiedIdent(start, name);
  else if (obj == NULL) {
    Error("%s undeclared", name);
    obj = BadExpr::New(proc_, Span(start), NULL)->object();
  }
  if (obj->AsBadExpr() == NULL) {
    if (sym_ == PERIOD && obj->AsTypeName() != NULL)
      obj = ParseStaticSelector(start, obj->AsTypeName());
    VarDecl* var = obj->AsVarDecl();
    if (var != NULL) {
      if (Reachable(var)) {
        // Track the highest level variable that is referenced outside the fct.
        // (Check enclosing functions as well - they supply the context.)
        for (Function* f = top_function();
             f != NULL && f->level() > var->level();
             f = f->owner()) {
          if (var->level() > f->context_level()) {
            f->set_context_level(var->level());
            f->set_nonlocal_variable(var);
          }
        }
      } else {
        // Note that if the variable is not reachable, it must be because we are
        // in a static initializer and the variable is outside and not static.
        Error("non-static variable %s may not be accessed in an initializer "
              "for a static variable", var->name());
        obj = BadExpr::New(proc_, Span(start), NULL)->object();
      }
    }
  }
  assert(obj != NULL);
  Trace(&tlevel_, "lookup %s -> %N", name, obj->node());
  return obj;
}


Object* Parser::ParseAndLookupPackageQualifiedIdent(Position* start,
                                                    szl_string name) {
  Trace t(&tlevel_, "(ParseAndLookupPackageQualifiedIdent");
  // Since we do not have any equivalent of protocol buffer package names,
  // piece together unrecognized selectors and look them up as
  // package-qualified names.
  assert(sym_ == PERIOD);
  string qualified_name = name;
  while (sym_ == PERIOD) {
    Next();  // skip the "."
    if (sym_ != IDENT) {
      Error("identifier expected; found %s", scanner_.PrintSymbol(sym_));
      return BadExpr::New(proc_, Span(start), NULL)->object();
    }
    qualified_name.append(".");
    qualified_name.append(scanner_.string_value());
    Next();  // skip the identifier
    Object* obj = ExistingDeclaration(start, qualified_name.c_str());
    if (obj != NULL)
      return obj;
  }
  Error("%s undeclared", qualified_name.c_str());
  return BadExpr::New(proc_, Span(start), NULL)->object();
}


FileLine* Parser::Span(Position* start) {
  int end = (start->offset == scanner_.offset()) ? scanner_.end_offset()
                                                 : scanner_.last_end_offset();
  return FileLine::New(proc_, start->file_name, start->line, start->offset,
                       end - start->offset);
}


bool Parser::Reachable(VarDecl* var) {
  // determines whether the variable is reachable through the current frame
  // or a frame linked through the static frame pointer
  // this could be done as part of lookup if we had levels available
  // instead, verify that the variable is either static, is declared within
  // the nearest enclosing static decl or we are not in a static decl
  if (var->is_static())
    return true;
  nonstatic_var_refs_++;
  for (int i = 0; i < function_stack_.length(); i++) {
    const FunctionAndFlag& top = function_stack_.nth_top(i);
    if (top.at_static_decl)
      // In the context of parsing the function at this stack level, we are
      // in a static declaration - and so the code currently being parsed
      // is contained in that static declaration initializer (either directly
      // or indirectly, i.e. within a function body that is contained within
      // that static declaration initializer).
      // But the non-static variable we are checking was not found yet and so
      // must be declared outside the static initializer (even though it
      // might be in a static initializer in a function body in an enclosing
      // static initializer).  So the variable is not reachable.
      return false;
    if (var->owner() == top.fun)
      // Either we are not in a static decl initializer, or this non-static
      // variable was declared within the current static initializer (and
      // not outside this static initializer but inside an enclosing one)
      // and so is reachable.
      return true;
  }
  ShouldNotReachHere();
  return false;
}


bool Parser::InStaticDecl() const {
  // see Reachable()
  for (int i = 0; i < function_stack_.length(); i++) {
    const FunctionAndFlag& top = function_stack_.nth_top(i);
    if (top.at_static_decl)
      return true;
  }
  return false;
}


void Parser::Declare(Object* obj) {
  Trace(&tlevel_, "(Declare %s -> %N", obj->name(), obj->node());
  if (top_scope()->Insert(obj)) {
    // collect all local variables
    VarDecl* var = obj->AsVarDecl();
    if (var != NULL && var->is_local())
      top_function()->AddLocal(var);
    // issue a warning if obj is shadowing a predefined object.
    //
    // note: in general, tuple members are not used w/o explicit qualification
    // thus there is generally not a problem with member names matching
    // predefined objects. however, there are some rare and pathological
    // cases where this breaks down (see bug 48090).  for now we
    // ignore those cases as it seems more important to get rid of the
    // unnecessary warnings.
    if (top_scope()->tuple() == NULL && !obj->is_anonymous() &&
        SymbolTable::universe()->Lookup(obj->name()) != NULL)
      Warning("declaration of %q hides the Sawzall predefined identifier %q",
              obj->name(), obj->name());
  } else {
    FileLine* previous = top_scope()->Lookup(obj->name())->node()->file_line();
    Error("redeclaration of %s (previous declaration at %s:%d)",
          obj->name(), previous->file(), previous->line());
  }
}


void Parser::Next() {
  Trace(&tlevel_, "(Next %s", scanner_.PrintSymbol(sym_));
  sym_ = scanner_.Scan();
  // Special check for "result".  To avoid stealing a common word for this
  // new feature, enable its keyword status only inside a ?{} construct.
  // We do it here for historical reasons.
  if (!statexpr_stack_.is_empty() &&
      sym_ == IDENT &&
      strcmp(scanner_.string_value(), "result") == 0)
    sym_ = RESULT;
}


void Parser::Expect(Symbol sym) {
  if (sym_ != sym)
    Error("%Y expected; found %s", sym, scanner_.PrintSymbol(sym_));
  ConsumeOffendingSymbol();
}


void Parser::Verify(Symbol sym) {
  if (sym_ != sym) {
    Error("SzlAssertion failed: %Y expected; found %s", sym, scanner_.PrintSymbol(sym_));
    abort();
  }
  Next();
}


void Parser::ConsumeOffendingSymbol() {
  Next();  // eat problem item
}


szl_string Parser::ParseIdent() {
  szl_string name = "";  // make sure name is != NULL
  if (sym_ == IDENT) {
    name = proc_->CopyString(scanner_.string_value());
    Next();
  } else {
    Error("identifier expected; found %s", scanner_.PrintSymbol(sym_));
    ConsumeOffendingSymbol();
  }
  Trace(&tlevel_, "ident = %s", name);
  return name;
}


szl_string Parser::ParsePackageQualifiedIdent(Position* start, szl_string name) {
  Trace t(&tlevel_, "(PackageQualifiedIdent");
  // To let the protocol compiler declare names in packages we accept
  // selector syntax in declarations, treating "." as part of the name.
  assert(sym_ == PERIOD);
  string qualified_name = name;
  while (sym_ == PERIOD) {
    Next();
    if (sym_ != IDENT) {
      Error("identifier expected; found %s", scanner_.PrintSymbol(sym_));
      break;
    }
    qualified_name.append(".");
    qualified_name.append(scanner_.string_value());
    Next();  // skip the identifier
  }
  return proc_->CopyString(qualified_name.c_str());
}


Field* Parser::ParseField() {
  // Three overlapping cases:
  // 1. ident: type
  // 2. ident (indicating a type)
  // 3. type (not starting with an ident)
  // Since an identifier could be a type name, some analysis is required.
  Trace t(&tlevel_, "(Field");
  szl_string name = NULL;
  Type* type = SymbolTable::bad_type();
  Position start(this);
  if (sym_ == IDENT) {
    // case 1 or 2: could be a field name, or type name for an anonymous field
    Position name_start(this);
    name = ParseIdent();
    if (sym_ == COLON) {
      // case 1: we had a field name before
      Next();  // consume COLON
      type = ParseType(NULL, NULL, false);
    } else {
      // case 2: the name should be a type name
      type = ParseType(NULL, ParseTypeName(&name_start, name), false);
      name = NULL;  // no field name
    }
  } else {
    type = ParseType(NULL, NULL, false);
  }
  // return result
  Field* field = Field::New(proc_, Span(&start), name, type);
  return field;
}


Field* Parser::ParseTupleField() {
  Trace t(&tlevel_, "(TupleField");
  Field* field = ParseField();

  // parse protocol buffer field default value, if any
  // (be lenient, accept always and complain later)
  Expr* val = NULL;
  if (sym_ == ASSIGN) {
    Next();  // consume '='
    val = ParseExpression(field->type());
    if (!val->type()->IsEqual(field->type(), false)) {
      Error("default value %N (%T) must be of type %T", val, val->type(), field->type());
      val = NULL;  // no need for a bad expression (NULL is a legal value)
    }
  }

  // parse protocol buffer tag: @ tag where tag must be an int literal
  // (be lenient, accept always and complain later)
  int tag = 0;
  if (sym_ == AT) {
    Next();  // consume '@'
    if (sym_ == INT) {
      tag = scanner_.int_value();
      if (tag <= 0)
        Error("tag value %d must be > 0", tag);
      Next();
    } else {
      Error("expected int literal in tag expression; found %Y", sym_);
    }
    // tags are only allowed for named fields
    if (field->name() == NULL) {
      Error("tag @ %d not allowed for anonymous field", tag);
      tag = 0;
    }
  }

  // complain if there is a default value without tag (for now)
  if (val != NULL && tag == 0)
    Error("default value %N requires proto tag", val);

  // set default and tag, if any
  if (val != NULL) {
    Literal* lit = val->AsLiteral();
    if (lit != NULL) {
      field->set_value(lit);
    } else if (val->AsBadExpr() == NULL) {
      // don't print for bad values because they already correspond to an error,
      // however this will still trigger for unsupported expressions whether
      // they contain bad values or not
      Warning("default value %N for field %s not yet supported"
              " (value is ignored)", val, field->name());
    }
  }
  if (tag > 0) {
    // For recursive fields the "is_proto" attribute may not be set yet, but
    // the presence of a tag will propagate the proto attribute to the type
    // when its (enclosing) declaration finishes, so no check is needed.
    // (The field might not directly reference an enclosing type, but may have
    // a composite type built from an enclosing type.  For protocol buffers
    // this only happens with arrays so we do not need a more general
    // "test if built from an enclosing type" function just for this case.)
    Field* f = field;
    while (f->type()->is_array())
      f = f->type()->as_array()->elem();
    if (!f->recursive() && !f->type()->is_proto())
      Error("field type %T for field %s must be a proto type",
            field->type(), field->name());
    else
      field->set_tag(tag);
  }

  // parse underlying protocol buffer type, if any
  szl_string pb_type_name = NULL;
  if (sym_ == COLON) {
    Next(); // consume ':'
    if (sym_ == IDENT) {
      pb_type_name = ParseIdent();
    } else {
      Error("expected protocol buffer type identifier; found %Y", sym_);
    }
  }

  if (tag == 0 && pb_type_name != NULL) {
    Error("not a protocol buffer field; underlying type ignored");
    pb_type_name = NULL;
  }

  if (pb_type_name != NULL) {
    ProtoBufferType pb_type =
      protocolbuffers::ParseProtoBufferType(pb_type_name);
    if (pb_type == PBTYPE_UNKNOWN) {
      Error("%s not valid for type of default value in protocol buffer",
            pb_type_name);
    }

    Type* szl_type = field->type();

    if (szl_type->is_array()) {
      // This field was likely generated from a "repeated footype" proto
      // buffer message.  In this situation, pb_type is the underlying
      // type for the elements of the array, not the array itself.
      szl_type = szl_type->as_array()->elem_type();
    }

    if (szl_type->is_basic()) {
      protocolbuffers::TypeCompatibility type_compat =
        protocolbuffers::ComputeTypeCompatibility(pb_type,
                                                   szl_type->as_basic());
      if (type_compat == protocolbuffers::COMPAT_INVALID) {
        Error("%s is not an acceptable underlying type for %T",
              pb_type_name, szl_type);
      } else {
        field->set_pb_type(pb_type);
        if (type_compat == protocolbuffers::COMPAT_MAY_OVERFLOW) {
          // for now: remove the warning - it should only show up if we actually do
          // a reverse conversion - FIX THIS
          // Warning("overflow may occur when doing conversions between proto type %s and szl type %T", pb_type_name, szl_type);
        }
      }
    } else {
      Error("attempted to declare an underlying type for non-basic type %T",
            szl_type);
    }
  }

  // done
  return field;
}


TupleType* Parser::ParseTuple(TypeName* tname, bool is_message) {
  Trace t(&tlevel_, "(Tuple");
  Expect(LBRACE);
  TupleType* enclosing_tuple = scope_stack_.top()->tuple();
  // tuple fields live in a new scope
  Scope* scope = OpenScope();
  TupleType* type = TupleType::NewUnfinished(proc_, scope, tname,
                                             enclosing_tuple);
  scope->set_tuple(type);
  int tag_count = 0;
  int field_count = 0;
  while (sym_ != RBRACE && sym_ != SCANEOF) {
    if (is_message && IsKeyword(sym_) && scanner()->NextSymbolIsColon())
      sym_ = IDENT;
    if (sym_ == TYPE) {
      if (!type->IsFullyNamed())
        Error("type name may not be declared in an unnamed tuple type");
      ParseTypeDecl(false);  // do not create a TypeDecl node nor require semi
    } else if (sym_ == STATIC) {
      if (!type->IsFullyNamed())
        Error("static member may not be declared in an unnamed tuple type");
      Next();
      szl_string name = ParseIdent();
      if (sym_ == COLON) {
        Position start(this);
        VarDecl* decl = ParseDecl(&start, name, true, false);
        decl->set_tuple(type);
      } else {
        Error("variable declaration expected after 'static'");
      }
    } else {
      Field* field = ParseTupleField();
      field_count++;
      // Applying this check at each field is potentially slow; if that ever
      // becomes a problem, we could add a "any recursive fields" flag.
      if (tname != NULL && IR::TupleContainsItself(type, field)) {
        Error("%T contains a field of type %T which contains an instance of "
              "%T (recursive definition)", type, field->type(), type);
        field->set_type(SymbolTable::bad_type());
      }
      if (field->has_tag()) {
        Field* other = scope->LookupByTag(field->tag());
        if (other != NULL)
          Error("tag %d of field %s was also used for field %s", field->tag(),
                field->name(), other->name());
        tag_count++;
      }
      Declare(field);
    }
    // unless we see a '}' we expect a ','
    // (this allows for a trailing ',' at the end)
    if (sym_ != RBRACE)
      Expect(COMMA);
  }
  if ((is_message || tag_count > 0) && (tag_count != field_count)) {
    Error("only some fields contain protocol buffer tags; tags must be consistently defined");
    // assume it's not a proto tuple
    tag_count = 0;
    is_message = false;
  }
  // we're done => create a tuple type
  // note: if tag_count > 0 or field_count == 0 we need to create a proto tuple
  Expect(RBRACE);
  CloseScope(scope);
  bool is_proto = tag_count > 0 || field_count == 0;
  type->Finish(proc_, is_proto, is_message, false);

  // if we attempted to create a proto tuple  but the constructor failed to
  // create the corresponding proto map in the TupleType, is_proto() will be
  // false => report an error in that case
  if (is_proto && !type->is_proto())
    Error("implementation restriction: "
          "proto tuple tags are too large in %T", type);
  return type;
}


ArrayType* Parser::ParseArray(TypeName* tname) {
  Trace t(&tlevel_, "(Array");
  Verify(ARRAY);
  Expect(OF);
  ArrayType* type = ArrayType::NewUnfinished(
      proc_, tname, scope_stack_.top()->tuple());
  Field* elem = ParseField();
  return type->Finish(proc_, elem);
}


MapType* Parser::ParseMap(TypeName* tname) {
  Trace t(&tlevel_, "(Map");
  Verify(MAP);
  Expect(LBRACK);
  MapType* type = MapType::NewUnfinished(
      proc_, tname, scope_stack_.top()->tuple());
  Field* index = ParseField();
  Expect(RBRACK);
  Expect(OF);
  Field* elem = ParseField();
  return type->Finish(proc_, index, elem);
}


List<Expr*>* Parser::FormatArgs(Symbol which, List<Expr*>* args, Scope* scope) {
  Next();
  if (args != NULL)
    Error("duplicate %Y specification", which);
  ReopenScope(scope);
  args = ParseArgList(true);
  CloseScope(scope);
  if (args->length() < 1) {
    Error("no arguments for %Y specification", which);
  } else {
    if (args->at(0)->as_string() == NULL)
      Error("first argument of %Y specification must be a string literal; is %N (type %T)",
        which, args->at(0), args->at(0)->type());
    else  // check the arguments; will call Error() if there's a problem.
      CompatiblePrintArgs(args->at(0)->as_string(), args, 1);
  }
  return args;
}


VarDecl* Parser::ParseDeclInOutputType() {
  Trace t(&tlevel_, "(DeclInOutputType");
  szl_string name;
  Type* type;
  Position start(this);
  if (sym_ == IDENT) {
    name = ParseIdent();
    if (sym_ == COLON) {
      // case 1: we had a field name before
      Next();  // consume COLON
      type = ParseType(NULL, NULL, false);
    } else {
      // case 2: the name should be a type name
      type = ParseType(NULL, ParseTypeName(&start, name), false);
      name = NULL;  // no field name
    }
  } else {
    type = ParseType(NULL, NULL, false);
    name = NULL;  // no field name
  }
  // The owner and level of these declarations are set when they are cloned.
  return VarDecl::New(proc_, Span(&start), name, type, NULL, 0, false, NULL);
}


Type* Parser::ParseOutputType() {
  Trace t(&tlevel_, "(OutputType");
  Verify(TABLE);
  int starting_nonstatic_count = nonstatic_var_refs_;

  // determine table kind
  szl_string kind_str = ParseIdent();
  TableType* kind = SymbolTable::LookupTableType(kind_str);
  if (kind == NULL) {
    Error("table type expected; found '%s'", kind_str);
    return SymbolTable::bad_type();
  }

  // parsing strategy: be lenient and accept the 'largest' output type syntax
  // independent of type - then verify the various constraints and report
  // errors if necessary.

  // parse parameter, if any
  // the parameter can only reference static variables
  bool is_static_context = StaticDeclFlag();  // remember the current setting
  SetStaticDeclFlag(true);  // non-static vars will be rejected
  Expr* param = NULL;
  szl_int evaluated_param = -1;
  if (sym_ == LPAREN) {
    Next();  // consume ')'
    param = ParseExpression(SymbolTable::int_type());
    Expect(RPAREN);
  }
  SetStaticDeclFlag(is_static_context);  // restore the actual context

  // parse index declarations
  Scope* index_scope = OpenScope();
  List<VarDecl*>* index_decls = List<VarDecl*>::New(proc_);
  while (sym_ == LBRACK) {
    Next();
    VarDecl* index_decl = ParseDeclInOutputType();
    index_decls->Append(index_decl);
    if (index_decl->name() != NULL && !index_scope->Insert(index_decl)) {
      FileLine* previous =
          index_scope->Lookup(index_decl->name())->node()->file_line();
      Error("redeclaration of %s (previous declaration at %s:%d)",
            index_decl->name(), previous->file(), previous->line());
    }
    Expect(RBRACK);
  }
  CloseScope(index_scope);

  // parse element declaration
  Expect(OF);
  Scope* elem_scope = OpenScope();
  VarDecl* elem_decl = ParseDeclInOutputType();
  if (elem_decl->name() != NULL)
  elem_scope->Insert(elem_decl);
    CloseScope(elem_scope);

  // parse weight, if any
  Field* weight = NULL;
  if (sym_ == WEIGHT) {
    Next();  // consume 'weight'
    weight = ParseField();
  }

  // parse extra attributes
  bool is_proc = false;
  List<Expr*>* index_format_args = NULL;
  List<Expr*>* elem_format_args = NULL;
  while (sym_ == FILE_ || sym_ == PROC || sym_ == FORMAT) {
    switch (sym_) {
      case PROC:
        if (proc_->mode() & Proc::kSecure)
          Error("cannot use 'proc' modifier in this context");
        is_proc = true;
        // fall through
      case FILE_:
        if (proc_->mode() & Proc::kSecure)
          Error("cannot use 'file' modifier in this context");
        index_format_args = FormatArgs(sym_, index_format_args, index_scope);
        break;
      case FORMAT:
          elem_format_args = FormatArgs(sym_, elem_format_args, elem_scope);
        break;
      default:  // silence g++ -Wall
        ShouldNotReachHere();
        break;
    }
  }

  // verify index type constraints
  for (int i = 0; i < index_decls->length(); i++) {
    VarDecl* index_decl = index_decls->at(i);
    if (index_decl->type()->is_function()) {
      Error("table index type '%T' must not be a function", index_decl->type());
      // note: no need to return with a bad type here,
      // since the table type is consistent
    }
  }

  // verify element type constraints
  if (elem_decl->type()->is_function()) {
    // for now we disallow functions here even though most of the machinery
    // is here (the emitter interface would have to be extended) - it's not
    // clear what the semantics should be
    Error("table element type '%T' must not be a function", elem_decl->type());
    // note: no need to return with a bad type here,
    // since the table type is consistent
  }

  // verify parameter constraints
  if (kind->has_param()) {
    if (param == NULL) {  // no parameter?
      Error("'%s' must have an int parameter", kind_str);
      return SymbolTable::bad_type();
    }
    if (!param->type()->is_int()) {  // is not integer type?
      Error("table parameter must be an integer; type is '%T'", param->type());
      return SymbolTable::bad_type();
    }
    if (param->as_int() == NULL) {  // is not a literal?
      // fold and propagate constants to simplify int constant expressions
      StaticVarFoldingVisitor v(proc_);
      param = param->Visit(&v);
    }
    if (param->as_int() != NULL) {  // is a literal?
      evaluated_param = param->as_int()->val();
      if (evaluated_param < 0) {
        Error("table parameter must be positive; value is '%N'", param);
        return SymbolTable::bad_type();
      }
      if (evaluated_param > kint32max) {  // SzlTypeProto's param is int32
        Error("overflow in table parameter '%N'", param);
        return SymbolTable::bad_type();
      }
    }
  } else {
    // does not accept a parameter
    if (param != NULL) {
      Error("'%s' does not accept a parameter", kind_str);
      return SymbolTable::bad_type();
    }
  }

  // verify weight constraints
  if (kind->has_weight()) {
    // must have a weight specification
    if (weight == NULL) {
      Error("'%s' must have 'weight' specification", kind_str);
      return SymbolTable::bad_type();
    }
    if (weight->type()->is_function()) {
      Error("table weight type '%T' must not be a function", weight->type());
      // no need to return with a bad type here,
      // since the table type is consistent
    }
  } else {
    // does not allow weight specification
    if (weight != NULL) {
      Error("'%s' does not allow 'weight' specification", kind_str);
      return SymbolTable::bad_type();
    }
  }

  // verify file or proc attributes constraints
  if (index_format_args != NULL) {
    // only collections work correctly now
    if (kind != SymbolTable::collection_type()) {
      Error("unimplemented file() or proc() with '%s' output variables",
            kind_str);
      return SymbolTable::bad_type();
    }
    // the element type must be bytes if there is no format attribute
    if (elem_format_args == NULL &&
        !elem_decl->type()->IsEqual(SymbolTable::bytes_type(), false)) {
      Error("element type must be bytes when file() or proc() attribute "
            "specified without format() attribute; is %T", elem_decl->type());
      return SymbolTable::bad_type();
    }
  }

  // done
  bool is_static = (starting_nonstatic_count == nonstatic_var_refs_);
  return OutputType::New(proc_, kind, param, evaluated_param, index_decls,
                         elem_decl, weight, is_proc, index_format_args,
                         elem_format_args, is_static,
                         scope_stack_.top()->tuple());
}


Type* Parser::ParseTypeName(Position* start, szl_string name) {
  Trace t(&tlevel_, "(TypeName %s", name);
  Object* obj = Lookup(start, name);
  assert(obj != NULL);
  if (obj->AsTypeName() != NULL) {
    return obj->type();
  } else {
    Error("%s is not a type", name);
    return SymbolTable::bad_type();
  }
}


Type* Parser::ParseProtoType(TypeName* tname) {
  Verify(PROTO);
  Position start(this);
  Type* type;
  if (sym_ == IDENT) {
    // Look it up.
    Object* obj = Lookup(&start, ParseIdent());
    // accept if it's a type name - we're doing 'proto T' for known type T
    if (obj->node()->AsTypeName() == NULL) {
      if (obj->AsBadExpr() == NULL)
        Error("%s is not a type", obj->name());
      return SymbolTable::bad_type();
    }
    type = obj->type();
  } else {
    // type does not start with an identifier
    type = ParseType(tname, NULL, false);
  }
  assert(type != NULL);
  if (type->is_tuple()) {
    // we don't represent 'proto' with its own node in the syntax tree
    // because we already have a representation for proto tuples -
    // instead we create a new proto tuple from the existing one
    type = type->MakeProto(proc_, NULL);
    if (! type->is_proto())
      Error("cannot convert %T into a proto tuple", type);
  } else {
    Error("tuple type expected; found %T", type);
  }
  return type;
}


Type* Parser::ParseType(TypeName* tname, Type* named_type, bool output_ok) {
  Trace t(&tlevel_, "(Type");
  Type* type = SymbolTable::bad_type();
  if (named_type != NULL)
    type = named_type;
  else
    switch (sym_) {
      case PARSEDMESSAGE:
        Next();  // consume 'parsedmessage'
        type = ParseTuple(tname, true);
        break;
      case LBRACE:
        type = ParseTuple(tname, false);
        break;
      case ARRAY:
        type = ParseArray(tname);
        break;
      case FUNCTION:
        type = ParseFunctionType(tname);
        break;
      case MAP:
        type = ParseMap(tname);
        break;
      case PROTO:
        type = ParseProtoType(tname);
        break;
      case TABLE:
        type = ParseOutputType();
        break;
      case IDENT: {
        Position start(this);
        type = ParseTypeName(&start, ParseIdent());
        break;
      }
      default:
        Error("type expected; %Y found", sym_);
        ConsumeOffendingSymbol();
    }
  // types must be complete
  if (type->is_incomplete()) {
    Error("%T type not allowed in this context (recursive definition?)", type);
    type = SymbolTable::bad_type();

  // make sure output type is ok in this context
  } else if (!output_ok && type->is_output()) {
    Error("output type (%T) not allowed in this context", type);
    type = SymbolTable::bad_type();
  }
  return type;
}


// TODO: consider moving most of this to Function::New().
void Parser::CreateParameters(Function* fun) {
  FunctionType* ftype = fun->ftype();
  for (int i = 0; i < ftype->parameters()->length(); i++) {
    Field* param = ftype->parameters()->at(i);
    assert(param->has_name());       // user defined parameters must be named
    assert(!param->has_value());     // we do not allow optional parameters
    VarDecl* decl = VarDecl::New(proc_, param->file_line(), param->name(),
                       param->type(), top_function(), top_level(), true, NULL);
    fun->AddParam(decl);
    Declare(decl);
  }
}


void Parser::ParseParameter(FunctionType* ftype) {
  Trace t(&tlevel_, "(Parameter");
  Position start(this);
  szl_string name = ParseIdent();
  Expect(COLON);
  Type* type = ParseType(NULL, NULL, true);
  Field* field = Field::New(proc_, Span(&start), name, type);
  ftype->add_parameter(field);
  // TODO Consider making parameter names optional and using ParseField().
}


FunctionType* Parser::ParseFunctionType(TypeName* tname) {
  Trace t(&tlevel_, "(FunctionType");
  Position start(this);
  Verify(FUNCTION);
  FunctionType* ftype = FunctionType::NewUnfinished(
      proc_, tname, scope_stack_.top()->tuple());

  // parse signature: parameters
  Expect(LPAREN);
  if (sym_ != RPAREN) {
    ParseParameter(ftype);
    while (sym_ == COMMA) {
      Next();
      ParseParameter(ftype);
    }
  }
  Expect(RPAREN);

  // parse signature: result type, if any
  if (sym_ == COLON) {
    Position result_start(this);
    Next();
    Type* result_type = ParseType(NULL, NULL, false);
    ftype->set_result(Field::New(proc_, Span(&result_start),
                                 NULL, result_type));
  }

  return ftype->Finish(proc_);
}


TypeDecl* Parser::ParseTypeDecl(bool expect_semi) {
  Trace t(&tlevel_, "(TypeDecl");
  Position start(this);
  Verify(TYPE);
  Position name_start(this);
  szl_string name = ParseIdent();
  bool package_qual = sym_ == PERIOD;
  if (package_qual && top_scope()->tuple() == NULL)
    name = ParsePackageQualifiedIdent(&name_start, name);
  TypeName* tname = TypeName::New(proc_, Span(&name_start), name);
  Expect(ASSIGN);
  if (package_qual && sym_ != PARSEDMESSAGE && sym_ != PROTO)
    Error("package qualifier appears on a non-parsedmessage, non-proto type");
  else
    Declare(tname);
  Type* type = ParseType(tname, NULL, true);
  tname->set_type(type);
  if (expect_semi) {
    Expect(SEMICOLON);
    bool print_expanded = type->type_name() == tname;
    return TypeDecl::New(proc_, Span(&start), tname, print_expanded);
  } else {
    return NULL;
  }
}


VarDecl* Parser::ParseDecl(Position* start, szl_string name,
                           bool is_static, bool expect_semi) {
  Trace t(&tlevel_, "(Decl");
  FileLine* var_fl = Span(start);
  Expect(COLON);
  Position type_start(this);  // function type is effectively part of value

  // Before declaring this variable and parsing its initialization expression,
  // we will need to temporarily set the static context flag and reset it back
  // before returning, so remember the current value.
  bool static_decl_flag = StaticDeclFlag();

  // Non-static variable declarations are not yet supported within static
  // statement expressions.
  bool is_in_static_statexpr = (!statexpr_stack_.is_empty() && InStaticDecl());
  if (!is_static && is_in_static_statexpr)
    Error("unimplemented non-static variable declaration %s "
          "in a static statement expression", name);

  VarDecl* var = NULL;
  Type* type = NULL;
  Expr* expr = NULL;
  if (sym_ == ASSIGN) {
    // implicit type from initializer: 'var := expr'
    Next();  // consume '='
    // Must set context level before we declare the variable and parse
    // the expression.  Since output variables cannot be assigned we do not
    // have to deal with expressions of type output.  (Output variables are
    // implicitly static, but here we must declare the variable before we
    // know its type.  Assuming there are no nontrivial expressions of type
    // output, then for unparenthesized output variable names we could
    // determine the type with a lookahead.  This is probably not worth it.)
    SetStaticDeclFlag(is_static);
    var = VarDecl::New(proc_, var_fl, name, SymbolTable::incomplete_type(),
                       top_function(), top_level(), false, NULL);
    Declare(var);
    if (sym_ == FUNCTION) {
      // Ugly special case.  The old special case function syntax parses
      // a declaration followed by an initializer, almost by accident. If
      // the function is recursive, this means we declare the function
      // before parsing the initializer.  We need that property here, so
      // if we see a function initializer, ignore the '=' and parse as before.
      // The relevant two forms are:
      // f: function() { f(); };  # old
      // f := function() { f(); };  # new
      type = ParseType(NULL, NULL, true);
      // Check that we haven't entered an erroneous state, e.g. x := function();
      if (sym_ != LBRACE)
        Error("%Y expected; found %s", LBRACE, scanner_.PrintSymbol(sym_));
    } else {
      expr = ParseExpression();
      type = expr->type();
      if (type->is_void()) {
        Error("illegal initializer: %N has void type", expr);
        expr = BadExpr::New(proc_, expr->file_line(), expr);
        type = SymbolTable::bad_type();
      }
      if (type->is_incomplete()) {
        Error("illegal initializer: %N has incomplete type", expr);
        expr = BadExpr::New(proc_, expr->file_line(), expr);
        type = SymbolTable::bad_type();
      }
      // Not allowed yet, but add the check so we don't forget.
      if (type->is_output()) {
        Error("illegal initializer: %N has output type; "
              "output variables may not be declared using :=", expr);
        expr = BadExpr::New(proc_, expr->file_line(), expr);
        type = SymbolTable::bad_type();
      }
    }
  } else {
    // explicit type: 'var : type'
    // We need to know whether "type" is an output type before we declare the
    // variable, but we need to declare it early (with an incomplete type)
    // to prevent scoping anomalies.  Since all output types begin with "table"
    // or a typename with output type, we need only examine the next symbol.
    bool is_output_type = false;
    if (sym_ == TABLE) {
      is_output_type = true;
    } else if (sym_ == IDENT) {
      szl_string name = proc_->CopyString(scanner_.string_value());
      Object* obj = ExistingDeclaration(start, name);
      if (obj != NULL && obj->AsTypeName() != NULL && obj->type()->is_output()) {
        is_output_type = true;
        // When it is a type name, check if the type uses nonstatic variables
        // and disallow its use in a static context.  Explicit types with
        // nonstatic variables are caught when the type is parsed below.
        if (InStaticDecl() && !obj->type()->as_output()->is_static())
          Error("output type %s uses a nonstatic variable and so may not ",
                "be used to declare a variable in a static context", name);
      }
    }
    SetStaticDeclFlag(is_static || is_output_type);
    var = VarDecl::New(proc_, var_fl, name, SymbolTable::incomplete_type(),
                       top_function(), top_level(), false, NULL);
    Declare(var);
    // When parsing an explicit output type (TABLE) in a non-static context,
    // we allow references to nonstatic variables even though this variable
    // declaration itself is considered static.
    SetStaticDeclFlag(is_static);
    type = ParseType(NULL, NULL, true);
  }

  if (type->is_output()) {
    ValidateTableName(name);
  }

  if (expr == NULL) {
    if (sym_ == ASSIGN) {
      // initializer exists
      Next();  // consume '='
      // Set function types early so recursive calls are not flagged.
      // (Recursive function defs cannot be parenthesized!)
      if (type->is_function() && sym_ == FUNCTION)
        var->set_type(type);
      expr = ParseExpression(type);
    } else if (sym_ == LBRACE) {
      // function initializer exists
      if (type->is_function()) {
        // Set function types early so recursive calls are not flagged.
        var->set_type(type);
        expr = ParseFunction(&type_start, name, type->as_function());
      } else {
        Error("illegal initializer: %s: %T is not a function variable", name, type);
      }
    }
  }

  if (expr != NULL) {
    if (type->is_output()) {
      Error("cannot initialize output variable %s", var->name());
      expr = BadExpr::New(proc_, expr->file_line(), expr);
    } else {
      // inject automatic conversion if possible, marking it as implicit
      if (!IR::IsCompatibleExpr(proc_, type, expr))
        expr = IR::CreateConversion(this, expr->file_line(), type, expr,
                                    List<Expr*>::New(proc_), true,
                                    true /* implicit */);
      if (!expr->type()->IsEqual(type, false)) {
        Error("type mismatch in initializer: %s: %T = %N (type %T)",
          name, type, expr, expr->type());
        expr = BadExpr::New(proc_, expr->file_line(), expr);
      }
    }

  } else if (var->is_static() && !type->is_output()) {
    // initializer must exist for static variable
    Error("static variable %s must be initialized", name);
  }
  // Set the variable type *after* checking the initializer (except fct def).
  var->set_type(type);
  var->set_init(expr);

  if (expect_semi)
    Expect(SEMICOLON);
  if (var->is_static())
    table_->add_static(var);

  // We must make sure this flag is reset when we return.
  SetStaticDeclFlag(static_decl_flag);
  return var;
}


Composite* Parser::ParseComposite() {
  Trace t(&tlevel_, "(Composite");
  Position start(this);
  bool has_pairs = false;
  Composite* comp = Composite::New(proc_, Span(&start));

  Expect(LBRACE);
  if (sym_ == COLON) {
    // empty paired composite
    Next();  // consume ':'
    has_pairs = true;
  } else {
    // (possibly empty) non-paired  or non-empty paired composite
    while (sym_ != RBRACE && sym_ != SCANEOF) {
      Expr* x = ParseExpression();
      comp->Append(x);
      // in pair-mode we expect a ':', if this is the first ':'
      // we switch to pair mode
      if (has_pairs || (comp->length() == 1 && sym_ == COLON)) {
        Expect(COLON);
        Expr* x = ParseExpression();
        comp->Append(x);
        has_pairs = true;
      }
      // unless we see a '}' we expect a ','
      // (this allows for a trailing ',' at the end)
      if (sym_ != RBRACE)
        Expect(COMMA);
    }
  }
  Expect(RBRACE);
  comp->set_file_line(Span(&start));

  comp->set_has_pairs(has_pairs);
  Trace(&tlevel_, "composite = %N (%T)", comp, comp->type());
  return comp;
}


List<Expr*>* Parser::ParseArgList(bool expect_parens) {
  Trace t(&tlevel_, "(ArgList");
  if (expect_parens)
    Expect(LPAREN);
  List<Expr*>* l = List<Expr*>::New(proc_);
  if (sym_ != RPAREN) {
    l->Append(ParseExpression());
    while (sym_ == COMMA) {
      Next();  // consume COMMA
      // be lenient and accept trailing comma
      // but complain (better error behavior)
      if (sym_ == RPAREN)
        Error("no trailing comma allowed in argument list");
      else
        l->Append(ParseExpression());
    }
  }
  if (expect_parens)
    Expect(RPAREN);
  return l;
}


Expr* Parser::ParseNew(Position* start, Intrinsic* fun) {
  Trace t(&tlevel_, "(New");
  Verify(LPAREN);
  Type* type = ParseType(NULL, NULL, false);
  Expr* length = NULL;
  if (sym_ == COMMA) {
    Next();  // consume COMMA
    length = ParseExpression(SymbolTable::int_type());
  }
  Expr* init = NULL;
  if (sym_ == COMMA) {
    Next();  // consume COMMA
    init = ParseExpression();
  }
  Expect(RPAREN);

  if (type->is_allocatable()) {
    if (type->is_indexable()) {
        if (length == NULL || !length->type()->is_int()) {
          Error("new(%T) requires integer length parameter", type);
          FileLine* fl = (length != NULL) ? length->file_line() : Span(start);
          length = BadExpr::New(proc_, fl, length);
        }
        if (init == NULL || !IR::IsCompatibleExpr(proc_, type->elem_type(), init)) {
          Error("incompatible initial value %N in new()", init);
          FileLine* fl = (init != NULL) ? init->file_line() : Span(start);
          init = BadExpr::New(proc_, fl, init);
        }
      } else if (type->is_map()) {
        if (init != NULL) {
          Error("new(%T) must not have initializer", type);
          init = NULL;
        }
        if (length == NULL)
          length = SymbolTable::int_0();
        else if (!length->type()->is_int()) {
          Error("new(%T, %N): length parameter not integer", type, length);
          length = BadExpr::New(proc_, length->file_line(), length);
        }
      } else {
        // there are no other allocatable types
        ShouldNotReachHere();
      }
  } else {
    Error("type %T cannot be dynamically allocated", type);
    return BadExpr::New(proc_, Span(start), NULL);
  }

  return New::New_(proc_, Span(start), type, length, init);
}


Expr* Parser::ParseConvert(Position* start, Intrinsic* fun) {
  Trace t(&tlevel_, "(Convert");
  Verify(LPAREN);
  Type* type = ParseType(NULL, NULL, false);
  Expect(COMMA);
  // don't provide type as hint for ParseExpression()
  // otherwise compatible composites will already have the
  // correct target type and CreateConversion() will complain
  // (conversion supressed)
  Expr* src = ParseExpression();
  List<Expr*>* params;
  if (sym_ != RPAREN) {
    Expect(COMMA);
    params = ParseArgList(false);
  } else {
    params = List<Expr*>::New(proc_);
  }
  Expect(RPAREN);
  return IR::CreateConversion(this, Span(start), type, src, params,
                              true, false /* not implicit */);
}


Regex* Parser::ParseRegex(Position* start, Intrinsic* fun) {
  Expr* base = NULL;
  Trace t(&tlevel_, "(Regex");
  Verify(LPAREN);
  Type* type = ParseType(NULL, NULL, false);
  if (sym_ == COMMA) {
    Next();
    base = ParseExpression(SymbolTable::int_type());
    if (!type->is_int())
      Error("base in regex() valid only for regex(int)");
    else if (!base->type()->is_int() || base->AsLiteral() == NULL)
      Error("base in regex() is %N (type %T); should be int literal", base, base->type());
  }
  Expect(RPAREN);
  return Regex::New(proc_, Span(start), type, base);
}


Expr* Parser::ParseSaw(Position* start, Intrinsic* fun) {
  Trace t(&tlevel_, "(Saw");
  Verify(LPAREN);
  // determine saw kind
  Saw::Kind kind = Saw::ILLEGAL;
  Expr* count = NULL;
  switch (fun->kind()) {
    case Intrinsic::SAW:
      kind = Saw::SAW;
      count = SymbolTable::int_1();
      break;
    case Intrinsic::SAWN:
      kind = Saw::SAWN;
      // parse count for sawn
      // this used to be evaluated more than once (so we used TempVariable);
      // now we rely on it being evaluated only once.
      count = ParseExpression(SymbolTable::int_type());
      if (!count->type()->is_int()) {
        Error("first argument of sawn is %N (type %T); should be int", count, count->type());
        count = BadExpr::New(proc_, count->file_line(), count);
      }
      // handle errors gracefully - don't just expect a comma
      if (sym_ != RPAREN)
        Expect(COMMA);
      break;
    case Intrinsic::SAWZALL:
      kind = Saw::SAWZALL;
      count = SymbolTable::int_max();
      break;
    default:
      ShouldNotReachHere();
      break;
  }
  // try to handle errors gracefully - the obvious case is not enough args
  if (sym_ == RPAREN) {
    Next();
    Error("%s() is missing string and regular expression arguments", fun->name());
    return BadExpr::New(proc_, Span(start), count);
  }
  // parse string to be sawn
  Expr* str = ParseExpression(SymbolTable::string_type());
  if (!str->type()->is_string()) {
    Error("source argument of %s is %N (type %T); should be string",
      fun->name(), str, str->type());
    str = BadExpr::New(proc_, str->file_line(), str);
  }
  // parse remaining arguments
  bool static_args = true;
  List<Expr*>* args = List<Expr*>::New(proc_);
  List<Saw::Flag>* flags = List<Saw::Flag>::New(proc_);
  bool prev_rest = false;  // previous arg was a 'rest' clause
  while (sym_ != RPAREN && sym_ != SCANEOF) {
    Expect(COMMA);
    if (prev_rest && kind != Saw::SAW)
      Error("'rest' clause must be last entry in %s() call", Saw::Kind2String(kind));
    prev_rest = false;
    Saw::Flag flag = Saw::NONE;
    switch (sym_) {
      case SKIP:
        flag = Saw::SKIP;
        Next();
        break;
      case REST:
        flag = Saw::REST;
        Next();
        prev_rest = true;
        break;
      case SUBMATCH:
        flag = Saw::SUBMATCH;
        Next();
        break;
      default:
        break;  // silence g++ -Wall
    }
    Expr* arg = ParseExpression(SymbolTable::string_type());
    if (!arg->type()->is_string()) {
      Error("argument #%d of %s is %N (type %T); should be string",
        (count == NULL ? 0 : 1) + 1 + args->length() + 1,
        fun->name(), arg, arg->type());
      arg = BadExpr::New(proc_, arg->file_line(), arg);
    } else if (flag == Saw::REST) {
      if (!IR::IsLvalue(arg)) {
        Error("argument %N for 'rest' must be l-value", arg);
        arg = BadExpr::New(proc_, arg->file_line(), arg);
      } else if (IR::IsStaticLvalue(arg)) {
        Error("l-value %N for 'rest' must not be static", arg);
        arg = BadExpr::New(proc_, arg->file_line(), arg);
      } else {
        MarkLvalue(arg, false);
      }
    }
    // check if pattern is static - used for regex caching, can be conservative
    // (for now we only check if it's a constant string literal or not)
    if (arg->as_string() == NULL)
      static_args = false;
    // collect argument
    flags->Append(flag);
    args->Append(arg);
  }
  // sym_ == RPAREN
  Expect(RPAREN);
  if (args->length() < 1) {
    Error("%s needs at least 1 regular expression", fun->name());
    return BadExpr::New(proc_, Span(start), SymbolTable::int_0());
  }
  // create saw node
  return Saw::New(proc_, Span(start), kind, count, str,
                  static_args, args, flags);
}


// Check that args[argno..) form a valid set for the print format fmt.
bool Parser::CompatiblePrintArgs(StringVal* fmt_val, List<Expr*>* args, int argno) {
  string fmt_str = fmt_val->cpp_str(proc_);
  szl_string fmt = fmt_str.c_str();
  while (*fmt != '\0') {
    fmt = utfrune(fmt, '%');
    if (fmt == NULL)  // no more %'s in string
      break;
    // find the verb; depends on UTF-8 so we can use *fmt
    // rather than parsing the runes (all valid format chars are ASCII)
    const char* start = fmt;
    for (fmt++; *fmt != '\0' && utfrune("%bcdeEfgGikopqstTuxX" "*hln", *fmt) == NULL; fmt++)
      ;
    if (*fmt == '\0') {
      Error("unrecognized format specifier %s", start);
      return false;
    }
    // check that engine has enough room to rewrite the format string.
    // worst case: engine will add .* or ll, plus the initial %
    if (fmt - start + 3 >= kMaxFormatLen) {
      Error("format specifier %.*s too long", start - fmt + 1, start);
      return false;
    }

    Rune fmt_rune;
    fmt += chartorune(&fmt_rune, fmt);  // do it right so we print good errors
    Type* type = NULL;
    bool uint_ok = false;  // integer formats accept int or uint expressions
    // NOTE: if you expand this list of supported formats, please update
    // format() doc string in intrinsic.cc
    switch (fmt_rune) {
      default:
        Error("unknown print format character %k", fmt_rune);
        return false;

      case '%':
        // just a literal percent
        continue;

       case '*':
       case 'n':
        Error("format verb %k not available in sawzall programs", fmt_rune);
        return false;

      case 'h':
      case 'l':
        Error("format modifier %k meaningless in sawzall", fmt_rune);
        return false;

      case 'b':
        type = SymbolTable::bool_type();
        break;

      case 'c':
      case 'k':
      case 'i':
      case 'd':
      case 'o':
      case 'u':
      case 'x':
      case 'X':
        type = SymbolTable::int_type();
        uint_ok = true;
        break;

      case 'e':
      case 'E':
      case 'f':
      case 'g':
      case 'G':
        type = SymbolTable::float_type();
        break;

      case 'p':
        type = SymbolTable::fingerprint_type();
        break;

      case 's':
      case 'q':
        type = SymbolTable::string_type();
        break;

      case 't':
        type = SymbolTable::time_type();
        break;

      case 'T':
        type = SymbolTable::bad_type();  // compatible with any type; a bit of a hack.
        break;
    }
    // need an argument of type 'type'
    if (argno >= args->length()) {
      Error("not enough arguments for format string");
      return false;
    }
    Expr* arg = args->at(argno);
    // cannot be void expression (can only happen for 'T' but easier to check here)
    if (arg->type()->is_void()) {
      Error("cannot format value for void expression %N", arg);
      return false;
    }
    if (!IR::IsCompatibleExpr(proc_, type, arg)) {
      // special case for uint: if uint is allowed, make a one-off test before complaining
      if (!(uint_ok && IR::IsCompatibleExpr(proc_, SymbolTable::uint_type(), arg))) {
        Error("print expression %N (type %T) not compatible with format %.*s", arg, arg->type(), fmt-start, start);
        return false;
      } else if (fmt_rune == 'T' && arg->type()->is_incomplete()) {
        // here we cannot resolve an incomplete type from the context
        Error("illegal format argument: %N has incomplete type", arg);
        return false;
      }
    }
    argno++;
  }
  if (argno != args->length()) {
    Error("too many arguments for format string");
    return false;
  }
  return true;
}


// Check sort's array parameter for conformance.
bool Parser::CheckSortSig(Intrinsic* fun, List<Expr*>* args) {
  assert(args != NULL);

  // Check arity.
  if (args->length() < 1) {
    Error("too few arguments to %s()", fun->name());
    return false;
  }
  if (args->length() > 2) {
    Error("too many arguments to %s()", fun->name());
    return false;
  }

  Expr* e = args->at(0);

  // Check array parameter.
  if (e->type()->as_array() == NULL) {
    Error("%s: %N (type %T) not an array type", fun->name(), e, e->type());
    return false;
  }

  Type* et = e->type()->as_array()->elem_type();
  if (!et->is_basic()) {
    Error("%s: %N with element type %T is not sortable", fun->name(), e, et);
    return false;
  }

  // If the optional comparison function is supplied, check it.
  Expr* cmp = NULL;
  if (args->length() == 2)
    cmp = args->at(1);

  if (cmp == NULL)
    return true;

  if (cmp->type()->as_function() == NULL) {
    Error("%s: %N (type %T) not a function type",
          fun->name(), cmp, cmp->type());
    return false;
  }

  FunctionType* ft = cmp->type()->as_function();
  List<Field*>* fparams = ft->parameters();
  if (fparams->length() != 2) {
    Error("%s: the comparison function (%N: %T) takes exactly two arguments",
          fun->name(), cmp, cmp->type());
    return false;
  }
  if (!fparams->at(0)->type()->IsEqual(fparams->at(1)->type(), false)) {
    Error("%s: the comparison function takes two matching arguments",
          fun->name());
    return false;
  }
  if (!ft->result_type()->IsEqual(SymbolTable::int_type(), false)) {
    Error("%s: the comparison function must return an int",
          fun->name());
    return false;
  }

  if (!fparams->at(0)->type()->IsEqual(et, false)) {
    Error("%s: the array element type (%T) doesn't match "
          "the comparison function arguments (%T)",
          fun->name(), et, fparams->at(0)->type());
    return false;
  }

  Warning("%s: comparison function not implemented yet!",
          fun->name());

  return true;
}


bool Parser::IsCompatibleIntrinsicArgList(Intrinsic* fun, List<Expr*>* args) {
  switch (fun->kind()) {
    case Intrinsic::DEBUG:
      if (args->length() > 0 && args->at(0)->as_string() != NULL) {
        // what DEBUG command is it?
        string cmd = args->at(0)->as_string()->cpp_str(proc_);
        if (cmd == "print") {
          if (args->length() > 1 && args->at(1)->as_string() != NULL)
            return CompatiblePrintArgs(args->at(1)->as_string(), args, 2);
          Error("DEBUG \"print\" needs a literal format string");
          return false;
        }
        if (cmd == "ref") {
          if (args->length() == 2)
            return true;
          Error("DEBUG \"ref\" needs a value as 2nd argument");
          return false;
        }
        Error("unknown DEBUG command %q", cmd.c_str());
        return false;
      }
      Error("DEBUG needs a literal string argument");
      return false;

    case Intrinsic::FORMAT: {
      if (args->length() == 0) {
        Error("format() needs at least one argument");
        return false;
      }
      Expr* fmt_arg = args->at(0);
      if (!fmt_arg->type()->is_string()) {  // is not string type?
        Error("format() argument must be a string; type is '%T'",
              args->at(0)->type());
        return false;
      }
      if (fmt_arg->as_string() == NULL) {  // is not a literal string?
        StaticVarFoldingVisitor v(proc_);
        fmt_arg = fmt_arg->Visit(&v);
      }
      if (fmt_arg->as_string() != NULL) {  // is a literal string?
        return CompatiblePrintArgs(fmt_arg->as_string(), args, 1);
      }
      // TODO: support non-static vars.
      Error("format() argument must be a string constant expression");
      return false;
    }

    case Intrinsic::HASKEY: {
      if (args->length() != 2) {
        Error("wrong number of arguments to haskey()");
        return false;
      }
      Expr* m = args->at(0);
      Expr* k = args->at(1);
      if (m->type()->as_map() == NULL) {
        Error("arg 1 of haskey() must be a map; %N is of type %T", m, m->type());
        return false;
      }
      if (!IR::IsCompatibleExpr(proc_, m->type()->as_map()->index_type(), k)) {
        Error("%N (type %T) not correct key type for map %N (type %T)", k, k->type(), m, m->type());
        return false;
      }
      return true;
    }

    case Intrinsic::INPROTO:
    case Intrinsic::CLEARPROTO: {
      string fun_name =
          fun->kind() == Intrinsic::INPROTO ? "inproto()" : "clearproto()";
      if (args->length() != 1) {
        Error("wrong number of arguments to %s", fun_name.c_str());
        return false;
      }
      Expr* x = args->at(0);
      Selector* s = x->AsSelector();
      if (s == NULL) {
        Error("%N not a suitable argument for %s;"
              "field selector (form: tuple.field) expected",
              x, fun_name.c_str());
        return false;
      }
      TupleType* t = s->var()->type()->as_tuple();
      assert(t != NULL);  // because s is a selector
      if (!t->is_proto()) {
        Error("%s expects a proto tuple field; "
              "%N (type %T) is not of proto tuple type",
              fun_name.c_str(), s->var(), s->var()->type());
        return false;
      }
      return true;
    }

    case Intrinsic::UNDEFINE: {
      if (args->length() != 1) {
        Error("wrong number of arguments to ___undefine()");
        return false;
      }
      Expr* x = args->at(0);
      Variable* v = x->AsVariable();
      if (v == NULL) {
        Error("%N not a suitable argument for ___undefine(); variable expected", x);
        return false;
      }
      if (v->is_static()) {
        Error("cannot undefine static variable %N (%T)", v, v->type());
        return false;
      }
      if (v->var_decl()->AsQuantVarDecl() != NULL) {
        Error("cannot undefine quantifier %N (%s %T)",
              v, v->var_decl()->AsQuantVarDecl()->KindAsString(), v->type());
        return false;
      }
      MarkLvalue(v, false);
      return true;
    }

    case Intrinsic::ADDRESSOF: {
      if (args->length() != 1) {
        Error("wrong number of arguments to ___addressof()");
        return false;
      }
      return true;
    }

    case Intrinsic::HEAPCHECK: {
      if (args->length() != 0) {
        Error("wrong number of arguments to ___heapcheck()");
        return false;
      }
      return true;
    }

    // len() and fingerprintof() are promiscuous, fingerprintof() even more so
    case Intrinsic::FINGERPRINTOF:
    case Intrinsic::LEN: {
      if (args->length() != 1) {
        Error("wrong number of arguments to %s()", fun->name());
        return false;
      }
      Expr* e = args->at(0);
      if (fun->kind() == Intrinsic::FINGERPRINTOF) {
        if (e->type()->is_basic64())
          return true;
        if (e->type()->is_tuple()) {
          return true;
        }
      }
      if (IR::IsCompatibleExpr(proc_, SymbolTable::string_type(), e))
        return true;
      if (IR::IsCompatibleExpr(proc_, SymbolTable::bytes_type(), e))
        return true;
      // for composites, try for an array or map
      if (e->AsComposite() != NULL && e->type()->is_incomplete())
        IR::DetermineCompositeType(proc_, e->AsComposite(), false);
      if (e->type()->as_array() != NULL)
        return true;
      if (e->type()->as_map() != NULL)
        return true;
      Error("%N (type %T) not a suitable argument for %s()", e, e->type(), fun->name());
      return false;
    }

    // sort[x](array [, cmp]) is polymorphic
    case Intrinsic::SORTX:
    case Intrinsic::SORT:
      return CheckSortSig(fun, args);

    case Intrinsic::DEF: {
      if (args->length() != 1) {
        Error("wrong number of arguments to def()");
        return false;
      }
      Expr* x = args->at(0);
      if (x->type()->is_void()) {
        Error("argument to def() is not a value");
        return false;
      }
      return true;
    }

    case Intrinsic::KEYS: {
      if (args->length() != 1) {
        Error("wrong number of arguments to keys()");
        return false;
      }
      Expr* x = args->at(0);
      if (!x->type()->is_map()) {
        Error("keys() must be applied to a map; %N is of type %T", x, x->type());
        return false;
      }
      return true;
    }

    case Intrinsic::LOOKUP: {
      if (args->length() != 3) {
        Error("wrong number of arguments to lookup()");
        return false;
      }
      Expr* m = args->at(0);
      Expr* k = args->at(1);
      Expr* v = args->at(2);
      if (!m->type()->is_map()) {
        Error("arg 1 of lookup() must be a map; %N is of type %T", m, m->type());
        return false;
      }
      if (!IR::IsCompatibleExpr(proc_, m->type()->as_map()->index_type(), k)) {
        Error("%N (type %T) not correct key type for map %N (type %T)", k, k->type(), m, m->type());
        return false;
      }
      if (!IR::IsCompatibleExpr(proc_, m->type()->as_map()->elem_type(), v)) {
        Error("%N (type %T) not correct value type for map %N (type %T)", v, v->type(), m, m->type());
        return false;
      }
      return true;
    }

    case Intrinsic::ABS: {
      if (args->length() != 1) {
        Error("wrong number of arguments to abs()");
        return false;
      }
      Expr* x = args->at(0);
      if (IR::IsCompatibleExpr(proc_, SymbolTable::int_type(), x))
        return true;
      if (IR::IsCompatibleExpr(proc_, SymbolTable::float_type(), x))
        return true;
      Error("%N (type %T) not a suitable argument for %s", x, x->type(), fun->name());
      return false;
    }
    default:
      Error("unimplemented: check arguments to %s", fun->name());
      return false;
  }
}


Function* Parser::ParseFunction(Position* start, szl_string name,
                                FunctionType* ftype) {
  Trace t(&tlevel_, "(Function");
  Function* fun = Function::New(proc_, Span(start), name,
                                ftype, top_function(), top_level() + 1);
  int old_function_count = table_->functions()->length();

  // parse function body
  Scope* function_scope = OpenFunctionScope(fun);
  // add parameters to scope
  CreateParameters(fun);
  // parse function block
  fun->set_body(ParseBlock(NULL, NULL, false));
  fun->body()->SetLineCounter();
  fun->set_file_line(Span(start));
  CloseFunctionScope(function_scope);

  // TODO remove local_functions_ ?
  top_function()->add_local_function(fun);
  table_->add_function(fun);

  // Test cloning by cloning every top-level function and replacing it
  // with its clone.  The current function and any enclosing functions
  // were added to the list in the symbol table; remove them so we do not
  // generate duplicate code.  Duplicate code is harmless except there are
  // a few warnings that would be duplicated so some tests would fail.
  if (FLAGS_test_function_cloning && error_count() == 0 &&
      fun->owner()->owner() == NULL) {
    // Non-top-level functions are cloned and added to the symbol table
    // when their enclosing functions are cloned.
    CloneMap cmap(proc_, table_, top_function(), NULL);
    fun = fun->AlwaysClone(&cmap);
    // Delete original copy of any function that was cloned.
    // (e.g. static functions are not cloned.)
    List<Function*>* list = table_->functions();
    int updated_function_count = old_function_count;
    for (int i = old_function_count; i < list->length(); i++) {
      if (cmap.Find(list->at(i)) == NULL)
        list->at(updated_function_count++) = list->at(i);
    }
    table_->functions()->Truncate(updated_function_count);
  }

  return fun;
}


Expr* Parser::ParseOperand(Position* start, szl_string name,
                           Indexing* indexing) {
  Trace t(&tlevel_, "(Operand");
  // 'format' is a keyword, but here we want it to be an identifier
  // so we can identify the format intrinsic (we want all 'formatting'
  // to look the same) => assume it is an identifier, consume it
  // and set name to 'format'.
  if (name == NULL && sym_ == FORMAT) {
    Next();  // consume 'format'
    name = "format";
  }

  if (name != NULL || sym_ == IDENT) {
    if (name == NULL)
      name = ParseIdent();

    Object* obj = Lookup(start, name);
    assert(obj != NULL);
    if (obj->AsLiteral() != NULL) {
      return obj->AsLiteral();

    } else if (obj->AsVarDecl() != NULL) {
      VarDecl* decl = obj->AsVarDecl();
      Variable* var = Variable::New(proc_, Span(start), decl);
      if (quants_.is_present(decl))
        Error("value of 'all' quantifier variable %s undefined in body of when statement", decl->name());
      return var;

    } else if (obj->AsField() != NULL) {
      Field* field = obj->AsField();
      Error("field %s may only be used after a '.' operator", field->name());
      return BadExpr::New(proc_, field->file_line(), field);

    } else if (obj->AsIntrinsic() != NULL) {
      return obj->AsIntrinsic();

    } else if (obj->AsTypeName() != NULL) {
      TypeName* type_name = obj->AsTypeName();
      if (sym_ == LPAREN) {
        // syntactic sugar for conversion
        Next();  // consume '('
        // don't provide obj->type() as hint for ParseExpression()
        // otherwise compatible composites will already have the
        // correct target type and CreateConversion() will complain
        // (conversion supressed)
        Expr* src = ParseExpression();
        List<Expr*>* params;
        if (sym_ != RPAREN) {
          Expect(COMMA);
          params = ParseArgList(false);
        } else {
          // create empty parameter list
          params = List<Expr*>::New(proc_);
        }
        Expect(RPAREN);
        return IR::CreateConversion(this, Span(start),
                                    type_name->type(), src, params, true,
                                    false /* not implicit */);
      } else if (sym_ == LBRACE)  {
        // anonymous function introduced with a function type
        FunctionType* ftype = type_name->type()->as_function();
        if (ftype != NULL)
          return ParseFunction(start, NULL, ftype);  // anonymous function
      }
    }

    Error("%s is not a legal operand", name);
    return BadExpr::New(proc_, Span(start), NULL);
  }

  switch (sym_) {
    case DOLLAR: {
      Next();
      Expr* array;
      Variable* length_temp = NULL;
      if (indexing == NULL) {
        Error("'$' must appear in index expression");
        array = BadExpr::New(proc_, Span(start), NULL);
      } else if (indexing->array->type()->is_map()) {
        Error("'$' must not be used with a map (%N)", indexing->array);
        array = BadExpr::New(proc_, Span(start), indexing->array);
      } else {
        array = indexing->array;
        if (array->AsVariable() != NULL) {
          // No evaluation, so OK to use the variable,
          // but must not reuse the Variable node itself.
          Variable* v = array->AsVariable();
          array = Variable::New(proc_, v->file_line(), v->var_decl());
        } else {
          // Arrange to store the length in a temp when the array is evaluated
          // to avoid repeated evaluation of the array expression
          // (even for composites: cannot fold here since it would affect
          // source printing, but codegen is too late)
          // The array is still supplied, but only for printing purposes;
          // it should not participate in static analysis or code generation.
          // (Should this be managed with a "rewritten" member instead?)
          if (indexing->temp == NULL)
            indexing->temp = CreateTempDecl(Span(start),
                                            SymbolTable::int_type());
          length_temp = Variable::New(proc_, Span(start), indexing->temp);
          // Alternatively, Dollar could hold a pointer to the Index or Slice
          // object (new abstract base, e.g. Indexed?).  The pointer could be
          // set up in ParseIndex; we could keep a list of Dollar nodes in the
          // Indexing object and update them after creating the Index or Slice.
          // Then the Indexed node could have a length field that is filled in
          // by constant folding or code generation just before the index
          // expressions are evaluated, and the Dollar nodes would use that
          // value.  This seems like a lot of work for very little gain.
        }
      }
      return Dollar::New(proc_, Span(start), array, length_temp);
    }

    case QUERY: {
      Next();
      StatExpr* statexpr = StatExpr::New(proc_, Span(start));
      statexpr_stack_.push(statexpr);
      statexpr->set_body(ParseBlock(NULL, NULL, true));
      statexpr_stack_.pop();
      if (statexpr->type()->is_incomplete()) {
        Error("?{} has no result statement");
        statexpr->set_type(SymbolTable::bad_type());
      }
      return statexpr;
    }

    case LBRACE:
      return ParseComposite();

    case LPAREN: {
        Next();
        Expr* x = ParseExpression(NULL, NULL, indexing, NULL);
        Expect(RPAREN);
        return x;
      }

    case BITNOT: {
      Next();
      Expr* x = SymbolTable::int_m1();
      Expr* y = ParseFactor(start, NULL, indexing);
      Type* type = y->type();
      if (type->IsEqual(SymbolTable::int_type(), false)) {
        return Binary::New(proc_, Span(start), type, x,
                           Binary::BXOR, xor_int, y);
      }
      if (type->IsEqual(SymbolTable::uint_type(), false)) {
        x = SymbolTable::uint_m1();
        return Binary::New(proc_, Span(start), type, x,
                           Binary::BXOR, xor_uint, y);
      }
      Error("bit complement applied to non-int %N", y);
      type = SymbolTable::bad_type();
      return Binary::New(proc_, Span(start), type, x,
                         Binary::BXOR, xor_int, y);
    }

    case NOT: {
      Next();
      Expr* x = SymbolTable::bool_f();
      Expr* y = ParseFactor(start, NULL, indexing);
      Type* type = y->type();
      if (!type->IsEqual(SymbolTable::bool_type(), false)) {
        Error("boolean 'not' applied to non-bool %N", y);
        type = SymbolTable::bad_type();
      }
      return Binary::New(proc_, Span(start), type, x,
                         Binary::EQL, eql_bits, y);
    }

    case MINUS: {
      Next();
      Opcode op = illegal;
      Expr* zero;
      Expr* x = ParseFactor(start, NULL, indexing);
      Type* type = x->type();
      if (type->IsEqual(SymbolTable::int_type(), false)) {
        zero = SymbolTable::int_0();
        op = sub_int;
      } else if (type->IsEqual(SymbolTable::float_type(), false)) {
        zero = SymbolTable::float_0();
        op = sub_float;
      } else {
        Error("negation cannot be applied to %N", x);
        zero = BadExpr::New(proc_, Span(start), NULL);
        type = SymbolTable::bad_type();
      }
      return Binary::New(proc_, Span(start), type, zero,
                         Binary::SUB, op, x);
    }

    case PLUS: {
      Next();
      return ParseFactor(start, NULL, indexing);
    }

    case INT:
    case CHAR: {
      szl_int value = scanner_.int_value();
      Next();
      return Literal::NewInt(proc_, Span(start), NULL, value);
    }

    case FINGERPRINT: {
      szl_int value = scanner_.int_value();
      Next();
      return Literal::NewFingerprint(proc_, Span(start), NULL, value);
    }

    case TIME: {
      szl_int value = scanner_.int_value();
      Next();
      return Literal::NewTime(proc_, Span(start), NULL, value);
    }

    case UINT: {
      szl_uint value = scanner_.int_value();
      Next();
      return Literal::NewUInt(proc_, Span(start), NULL, value);
    }

    case FLOAT: {
      szl_float value = scanner_.float_value();
      Next();
      return Literal::NewFloat(proc_, Span(start), NULL, value);
    }

    case STRING: {
      // beware of \0 in string literals!
      const szl_string val = scanner_.string_value();
      const int len = scanner_.string_len() - 1; // includes terminal '\0' => -1
      if (strlen(val) < len)
        Error("string literal %z contains a \\0 character", val, len);
      Literal* lit = Literal::NewString(proc_, Span(start), NULL, val);
      Next();
      return lit;
    }

    case BYTES: {
      const char* val = scanner_.bytes_value();
      const int len = scanner_.bytes_len();
      Literal* lit = Literal::NewBytes(proc_, Span(start), NULL, len, val);
      Next();
      return lit;
    }

    case FUNCTION: {
      FunctionType* ftype = ParseFunctionType(NULL);
      return ParseFunction(start, NULL, ftype);  // anonymous function
    }

    default: {
      Error("factor expected, found %s", scanner_.PrintSymbol(sym_));
      ConsumeOffendingSymbol();
      return BadExpr::New(proc_, Span(start), NULL);
    }
  }
}


Expr* Parser::ParseSelector(Position* start, Expr* x) {
  struct Local {
    static bool AlsoTreatAsKeyword(szl_string name) {
      // The protocol compiler keyword check includes the predefined names
      // of the basic types, even though these names did not conflict.
      // This list is hard-coded here because it is only relevant here and
      // in the protocol compiler.
      static const char *type_names[] = { "bool", "bytes", "fingerprint",
                                          "float", "int", "string", "time" };
      for (int i = 0; i < ARRAYSIZE(type_names); i++)
        if (strcmp(name, type_names[i]) == 0)
          return true;
      return false;
    }
  };
  Trace t(&tlevel_, "(Selector");
  Verify(PERIOD);
  TupleType* tuple = x->type()->as_tuple();
  // x must be a tuple type
  if (tuple != NULL) {
    // Accept keywords as message tuple field names.
    if (tuple->is_message() && IsKeyword(sym_))
      sym_ = IDENT;
    szl_string field_name = ParseIdent();
    Object* obj = tuple->scope()->Lookup(field_name);
    if (obj != NULL) {
      if (obj->AsField() != NULL) {
        // ordinary field
        return Selector::New(proc_, Span(start), x, obj->AsField());
      } else if (obj->AsVarDecl() != NULL) {
        // static declaration
        return Variable::New(proc_, Span(start), obj->AsVarDecl());
      } else {
        // type declaration
        Error("member %s in tuple type %T is not a field", field_name, tuple);
      }
    } else {
      Error("no %s field in tuple type %T", field_name, tuple);
    }
  } else {
    Error("%N (%T) is not a tuple", x, x->type());
  }
  return BadExpr::New(proc_, Span(start), x);
}


Object* Parser::ParseStaticSelector(Position* start, TypeName* x) {
  Trace t(&tlevel_, "(StaticSelector");
  while (true) {
    Verify(PERIOD);
    TupleType* tuple = x->type()->as_tuple();
    if (tuple == NULL) {
      Error("%N (%T) is not a tuple type", x, x->type());
      return BadExpr::New(proc_, Span(start), NULL)->object();
    }
    szl_string member_name = ParseIdent();
    Object* obj = tuple->scope()->Lookup(member_name);
    if (obj == NULL) {
      Error("no %s member in tuple type %T", member_name, tuple);
      return BadExpr::New(proc_, Span(start), NULL)->object();
    }
    if (obj->AsTypeName() == NULL || sym_ != PERIOD)
      return obj;
    x = obj->AsTypeName();
  }
}


Expr* Parser::ParseIndex(Position* start, Expr* x) {
  Trace t(&tlevel_, "(Index");
  Verify(LBRACK);
  Indexing indexing = { x, NULL };
  Expr* beg = ParseExpression(NULL, NULL, &indexing, NULL);
  Expr* end = NULL;
  // accept slice even if not legal, complain afterwards
  if (sym_ == COLON) {
    Next();
    end = ParseExpression(NULL, NULL, &indexing, NULL);
  }
  Expect(RBRACK);

  // we could be indexing a composite; e.g.: { 2, 3, 5 }[i]
  // => make sure we have complete type
  if (x->AsComposite() != NULL && x->type()->is_incomplete())
    IR::DetermineCompositeType(proc_, x->AsComposite(), false);

  // make sure various constraints are satisfied
  Expr* res = NULL;
  Variable* length_temp = NULL;
  if (indexing.temp != NULL) {
    length_temp = Variable::New(proc_, Span(start), indexing.temp);
    MarkLvalue(length_temp, false);
  }
  if (x->type()->is_indexable()) {
    if (!beg->type()->is_int()) {
      Error("index %N (%T) must be of type int", beg, beg->type());
      beg = BadExpr::New(proc_, beg->file_line(), beg);
    }
    if (end == NULL) {
      // simple index
      res = Index::New(proc_, Span(start), x, beg, length_temp);
    } else {
      // slice
      if (!end->type()->is_int()) {
      Error("index %N (%T) must be of type int", end, end->type());
        end = BadExpr::New(proc_, end->file_line(), end);
      }
      res = Slice::New(proc_, Span(start), x, beg, end, length_temp);
    }
  } else if (x->type()->is_map()) {
    assert(indexing.temp == NULL);
    MapType* map = x->type()->as_map();
    if (!IR::IsCompatibleExpr(proc_, map->index_type(), beg)) {
      Error("map index %N (%T) must be of type %T", beg, beg->type(), map->index_type());
      beg = BadExpr::New(proc_, beg->file_line(), beg);
    }
    if (end != NULL) {
      Error("no slices allowed for map index");
      end = NULL;
    }
    res = Index::New(proc_, Span(start), x, beg, length_temp);
  } else {
    if (x->type()->is_output())
      // it is a common error to forget the 'emit' keyword
      // in emit statements - assume this is the case here
      // and give a better error message (table variables
      // can only be used in emit statements)
      Error("'emit' expected before %N (%T)", x, x->type());
    else
      Error("%N (%T) is not indexable", x, x->type());
    res = BadExpr::New(proc_, x->file_line(), x);
  }

  assert(res != NULL);
  return res;
}

// Special case: to allow rolling out the new protocolcompiler, which
// generates uints for unsigned integers and string for strings
// while remaining compatible with existing programs,
// allow mixing ints and uints, bytes and string.
// warning_template must have 2 "%s" for expected type and actual type.
bool Parser::ConvertIfPossible(
    Type *expected_type, Type *actual_type, Position *start, Expr **expr,
    const char* warning_template) {
  if (!FLAGS_enable_proto_conversion_hack)
    return NULL;
  if (expected_type->IsEqual(SymbolTable::int_type(), false) &&
      actual_type->IsEqual(SymbolTable::uint_type(), false)) {
    Warning(warning_template, "int", "uint", *expr);
    *expr = IR::CreateConversion(this, Span(start),
                                 SymbolTable::int_type(),
                                 *expr, List<Expr*>::New(proc_),
                                 true, false);
    return true;
  } else if (expected_type->IsEqual(SymbolTable::uint_type(), false) &&
      actual_type->IsEqual(SymbolTable::int_type(), false)) {
    Warning(warning_template, "uint", "int", *expr);
    *expr = IR::CreateConversion(this, Span(start),
                                 SymbolTable::uint_type(),
                                 *expr, List<Expr*>::New(proc_),
                                 true, false);
    return true;
  } else if (expected_type->IsEqual(SymbolTable::string_type(), false) &&
      actual_type->IsEqual(SymbolTable::bytes_type(), false)) {
    Warning(warning_template, "string", "bytes", *expr);
    *expr = IR::CreateConversion(this, Span(start),
                                 SymbolTable::string_type(),
                                 *expr, List<Expr*>::New(proc_),
                                 true, false);
    return true;
  } else if (expected_type->IsEqual(SymbolTable::bytes_type(), false) &&
      actual_type->IsEqual(SymbolTable::string_type(), false)) {
    Warning(warning_template, "bytes", "string", *expr);
    *expr = IR::CreateConversion(this, Span(start),
                                 SymbolTable::bytes_type(),
                                 *expr, List<Expr*>::New(proc_),
                                 true, false);
    return true;
  } else {
    return false;
  }
}

Expr* Parser::GenIncompatibleCallError(FileLine* fl, szl_string message,
                                       Intrinsic* i, List<Expr*>* args) {
  Call* call = Call::New(proc_, fl, i, args);
  Fmt::State f;
  F.fmtstrinit(&f);
  for (Intrinsic* cur = i; cur != NULL; cur = cur->next_overload()) {
    // keep track of candidate list for error messages
    F.fmtprint(&f, "\n    %N: %T", cur, cur->type());
  }
  char* clist = F.fmtstrflush(&f);
  Error("%s for %N: candidates are:%s", message, call, clist);
  free(clist);
  return BadExpr::New(proc_, fl, call);
}


Expr* Parser::ConvertableTuple(Position* start, Expr* x, Type *type) {
  if (!FLAGS_enable_proto_conversion_hack)
    return NULL;
  TupleType *tuple_type = type->as_tuple();
  if (tuple_type == NULL)
    return NULL;
  Composite *comp = x->AsComposite();
  if (comp == NULL)
    return NULL;
  if (comp->length() != tuple_type->fields()->length())
    return NULL;
  Composite* new_comp = Composite::New(proc_, Span(start));
  // Rewrite uint<>int, string<>bytes mismatches only; we'll recheck afterwards
  for (int i = 0; i < comp->length(); i++) {
    Expr* elem = comp->list()->at(i);
    Field* field = tuple_type->fields()->at(i);
    const char* warning = "Tuple element is type %s, placing %s (%N); "
                          "converting automatically";
    if (!ConvertIfPossible(
        field->type(), elem->type(), start, &elem, warning)) {
      Expr* elem_comp = ConvertableComposite(start, elem, field->type());
      if (elem_comp != NULL)
        elem = elem_comp;
      if (!elem->type()->IsEqual(field->type(), false))
        return NULL;
    }
    new_comp->Append(elem);
  }
  new_comp->set_type(type);
  return new_comp;
}


Expr* Parser::ConvertableArray(Position* start, Expr* x, Type *type) {
  if (!FLAGS_enable_proto_conversion_hack)
    return NULL;
  ArrayType *array_type = type->as_array();
  if (array_type == NULL)
    return NULL;
  Composite *comp = x->AsComposite();
  if (comp == NULL)
    return NULL;
  Composite* new_comp = Composite::New(proc_, Span(start));
  Field* field = array_type->elem();
  // Rewrite uint<>int, string<>bytes mismatches only; we'll recheck afterwards
  for (int i = 0; i < comp->length(); i++) {
    Expr* elem = comp->list()->at(i);
    const char* warning = "array element is type %s, placing %s (%N); "
                          "converting automatically";
    if (!ConvertIfPossible(
        field->type(), elem->type(), start, &elem, warning)) {
      Expr* elem_comp = ConvertableComposite(start, elem, field->type());
      if (elem_comp != NULL)
        elem = elem_comp;
      if (!elem->type()->IsEqual(field->type(), false))
        return NULL;
    }
    new_comp->Append(elem);
  }
  new_comp->set_type(type);
  return new_comp;
}


Expr* Parser::ConvertableComposite(Position* start, Expr* x, Type *type) {
  Expr* expr = ConvertableTuple(start, x, type);
  if (expr == NULL)
    expr = ConvertableArray(start, x, type);
  return expr;
}


Call* Parser::ConvertableCall(Position* start, Expr* x, List<Expr*>* args) {
  if (!FLAGS_enable_proto_conversion_hack)
    return NULL;
  FunctionType* type = x->type()->as_function();
  List<Field*>* params = type->parameters();
  if (args->length() != params->length())
    return NULL;
  List<Expr*>* nargs = List<Expr*>::New(proc_);
  for (int i = 0; i < args->length(); i++) {
    Field* param = params->at(i);
    Expr* arg = args->at(i);
    const char* warning = "function expects %s, passing %s (%N); "
                          "converting automatically";
    ConvertIfPossible(
        param->type(), arg->type(), start, &arg, warning);
    nargs->Append(arg);
  }
  // Now does it work?
  if (!IR::IsCompatibleFunctionArgList(proc_, type, nargs))
    return NULL;
  return Call::New(proc_, Span(start), x, nargs);
}

Expr* Parser::ParseCall(Position* start, Expr* x) {
  Trace t(&tlevel_, "(Call");
  assert(sym_ == LPAREN);
  FunctionType* ftype = x->type()->as_function();
  if (ftype == NULL) {
    // x is not a function => cannot call it
    // (Parse args for more graceful error handling.)
    if (x->AsBadExpr() == NULL)
      Error("%N is not a function, cannot call it", x);
    ParseArgList(true);
    return BadExpr::New(proc_, x->file_line(), x);
  }

  // x is a function => determine call kind
  Intrinsic* fun = x->AsIntrinsic();
  if (fun == NULL) {
    // handle user-defined functions
    List<Expr*>* args = ParseArgList(true);
    FileLine* fl = Span(start);
    Call* call = Call::New(proc_, fl, x, args);
    if (!IR::IsCompatibleFunctionArgList(proc_, ftype, args)) {
      // Special case: to allow rolling out the new protocolcompiler, which
      // generates uints for unsigned integers and string for strings
      // while remaining compatible with existing programs,
      // allow mixing ints and uints, bytes and string.
      call = ConvertableCall(start, x, args);
      if (call == NULL) {
        Error("incompatible argument list for %N", x);
        return BadExpr::New(proc_, fl, call);
      }
    }
    return call;
  }

  // handle non-user defined functions
  // a) handle intrinsics which are translated into special nodes
  switch (fun->kind()) {
    case Intrinsic::CONVERT:
      return ParseConvert(start, fun);
    case Intrinsic::NEW:
      return ParseNew(start, fun);
    case Intrinsic::REGEX:
      return ParseRegex(start, fun);
    case Intrinsic::SAW:
    case Intrinsic::SAWN:
    case Intrinsic::SAWZALL:
      return ParseSaw(start, fun);
    default:
      // need to provide default to silence gcc -Wall
      break;
  }

  // This isn't one of the special forms above.  Since there is an opening
  // '(', assume a call; parse the argument list so the code below can
  // match against intrinsic candidates, which may be overloaded.
  List<Expr*>* args = ParseArgList(true);
  FileLine* fl = Span(start);

  // b) handle remaining intrinsics
  if (fun->kind() == Intrinsic::INTRINSIC ||
      fun->kind() == Intrinsic::MATCH ||
      fun->kind() == Intrinsic::MATCHPOSNS ||
      fun->kind() == Intrinsic::MATCHSTRS) {


    // Handle regular intrinsics (regular return type and parameter lists)
    // Important to construct Call object here. (See notes about
    // IR::IsCompatibleFunctionArgList() below.)
    Call* call = Call::New(proc_, fl, fun, args);

    if (fun->kind() == Intrinsic::INTRINSIC) {
      // Overloads are only supported for custom intrinsics and min()/max()
      // TODO: could also support some other built-ins and user functions
      // Walk the list of overloads (if any), checking for compatible argument
      // list.  Must find exactly one match, otherwise report error.
      Intrinsic* match = NULL;
      for (Intrinsic* i = fun; i != NULL; i = i->next_overload()) {
        // Important to construct Call object before calling
        // IR::IsCompatibleFunctionArgList() because otherwise the
        // syntax tree will contain default values for optional
        // parameters (they are filled in by the IR call).  When
        // printing source, these optional parameters would get
        // printed which is confusing to the user.  Some tests also
        // depend on this syntax tree not containing defaults for
        // optional parameters.
        Call* call_i = Call::New(proc_, fl, i, args);
        // candidate must be an Expr with type FunctionType
        FunctionType* ftype = i->type()->as_function();
        if (IR::IsCompatibleFunctionArgList(proc_, ftype, args)) {
          if (match != NULL) {
            // Multiple matches found (ambiguous call).
            // List candidates for diagnostic purposes.
            return GenIncompatibleCallError(fl, "ambiguous argument list",
                                            fun, args);
          }

          // First match found.
          // Continue loop to verify this is not an ambiguous call.
          match = i;
          call = call_i;
        }
      }
      if (match == NULL) {
        // No matches found.  List all the candidates for diagnostic purposes.
        return GenIncompatibleCallError(fl, "incompatible argument list", fun, args);
      }

      // Found match.  Use it in remaining checks.
      fun = match;

    } else if (!IR::IsCompatibleFunctionArgList(proc_, fun->ftype(), args)) {
      // regular built-ins don't support overloading, so just check arg list
      return GenIncompatibleCallError(fl, "incompatible argument list",
                                      fun, args);
    }

    // Do not call getrusage unless we are using getresourcestats. Note
    // that any function with this name will activate resource collection
    // but it's not worth trying harder to get this perfect.
    if (strcmp(fun->name(), "getresourcestats") == 0) {
      proc_->set_calls_getresourcestats();
    }
    return call;

  } else {
    // handle irregular intrinsics (special return type or parameter lists).
    // take care to access args only if they exist.
    bool rewrite = false;  // flag to factor out the rewriting of fun
    Type* ret_type = SymbolTable::void_type();
    switch (fun->kind()) {
      case Intrinsic::LOOKUP:
        // for lookup the return type is m->elem_type()
        if (args->length() > 0 && args->at(0)->type()->is_map())
          ret_type = args->at(0)->type()->as_map()->elem_type();
        rewrite = true;
        break;

      case Intrinsic::ABS:
      case Intrinsic::SORT:
        // for these the return type is the same as the args type.
        // (not including SORTX, because it has fixed return type.)
        if (args->length() > 0)
          ret_type = args->at(0)->type();
        rewrite = true;
        break;

      case Intrinsic::KEYS:
        // for keys the return type is array of m->index_type(), which
        // is precomputed as m->key_array_type()
        if (args->length() > 0 && args->at(0)->type()->is_map())
          ret_type = args->at(0)->type()->as_map()->key_array_type();
        rewrite = true;
        break;

      default:  // need to provide default to silence gcc -Wall
        break;
    }
    if (rewrite) {
      FunctionType* ftype = FunctionType::NewUnfinished(proc_, NULL, NULL);
      ftype->set_result(Field::New(proc_, fl, NULL, ret_type));
      ftype->Finish(proc_);
      fun = Intrinsic::New(proc_, fun->file_line(), fun->name(), ftype,
                           fun->kind(), fun->function(), NULL,
                           fun->attr(), fun->can_fail());
    }
    Call* call = Call::New(proc_, fl, fun, args);
    if (!IsCompatibleIntrinsicArgList(fun, args))
      return BadExpr::New(proc_, fl, call);
    return call;
  }
}


Expr* Parser::ParseFactor(Position* start, szl_string name,
                          Indexing* indexing) {
  Trace t(&tlevel_, "(Factor");
  Expr* left = ParseOperand(start, name, indexing);
  while (true) {
    if (sym_ == PERIOD) {
      // parse tuple field selector
      left = ParseSelector(start, left);
    } else if (sym_ == LBRACK) {
      // parse array index or slice, or map
      left = ParseIndex(start, left);
    } else if (sym_ == LPAREN) {
      // parse function invocation
      left = ParseCall(start, left);
    } else {
      // simple operand => nothing to do
      if (left->AsIntrinsic() != NULL) {
        Error("intrinsic function '%N' cannot be used as value", left);
        left = BadExpr::New(proc_, Span(start), left);
      }
      return left;
    }
  }
}


Expr* Parser::CreateBinary(Position* start, Type* type, Expr* left,
                           Binary::Op op, Opcode opcode, Expr* right) {
  if (!left->type()->IsEqual(right->type(), false)) {
    // Special case: to allow rolling out the new protocolcompiler, which
    // generates uints for unsigned integers and string for strings
    // while remaining compatible with existing programs,
    // allow mixing ints and uints, bytes and string.
    const char* warning =
        "expression combines %s and %s (%N); converting automatically";
    // Convert left value only if right expr is uint.
    if (left->type()->IsEqual(SymbolTable::int_type(), false) &&
        right->type()->IsEqual(SymbolTable::uint_type(), false)) {
      Warning("expression combines int (%N) and uint (%N); "
              "converting the latter to int.");
      left = IR::CreateConversion(this, Span(start),
                                  SymbolTable::int_type(),
                                  left, List<Expr*>::New(proc_),
                                  true, false);
    } else if (!ConvertIfPossible(
        left->type(), right->type(), start, &right, warning)) {
      Error("type mismatch: %N (type %T) %s %N (type %T)", left, left->type(),
            Binary::Op2String(op), right, right->type());
      type = SymbolTable::bad_type();
    }
  } else if (!IR::IsCompatibleOp(left->type(), op)) {
    Error("operator %s does not apply to %N (type %T)",
      Binary::Op2String(op), right, right->type());
    type = SymbolTable::bad_type();
  }
  return Binary::New(proc_, Span(start), type, left, op, opcode, right);
}


Opcode Parser::OpcodeFor(Symbol sym, Expr* expr) {
  Type* type = expr->type();
  Opcode op = IR::OpcodeFor(sym, type);
  if (op == illegal && !type->is_bad())  // don't issue error msg for bad types
    Error("binary operator %Y cannot be applied to %N (type %T)", sym, expr, type);
  return op;
}


Expr* Parser::ParseTerm(Position* start, szl_string name, Indexing* indexing) {
  Trace t(&tlevel_, "(Term");
  Expr* left = ParseFactor(start, name, indexing);
  while (true) {
    Symbol sym = sym_;
    Binary::Op op;
    switch (sym_) {
      case TIMES:
        op = Binary::MUL;
        break;
      case DIV:
        op = Binary::DIV;
        break;
      case MOD:
        op = Binary::MOD;
        break;
      case SHL:
        op = Binary::SHL;
        break;
      case SHR:
        op = Binary::SHR;
        break;
      case BITAND:
        op = Binary::BAND;
        break;
      default:
        return left;
    }
    Next();  // consume operator
    Position right_start(this);
    Expr* right = ParseFactor(&right_start, NULL, indexing);
    left = CreateBinary(start, left->type(), left, op,
                        OpcodeFor(sym, left), right);
  }
}


Expr* Parser::ParseSimpleExpr(Position* start, szl_string name,
                              Indexing* indexing) {
  Trace t(&tlevel_, "(SimpleExpr");
  Expr* left = ParseTerm(start, name, indexing);
  while (true) {
    Binary::Op op;
    Symbol sym = sym_;
    bool absorb = true;
    switch (sym_) {
      case PLUS:
        op = Binary::ADD;
        break;
      case BITOR:
        op = Binary::BOR;
        break;
      case BITXOR:
        op = Binary::BXOR;
        break;
      case MINUS:
        op = Binary::SUB;
        break;
      case INT:
        // scanner may have absorbed minus sign into integer literal
        // (the scanner needs to accept a minus sign as part of an
        // int literal so that we can represent the most negative int
        // in the language)
        if (scanner_.int_value() < 0) {
          // convert -x to -(x)
          scanner_.negate_int_value();
          op = Binary::SUB;
          sym = MINUS;
          absorb = false;
          break;
        }
        return left;
      case FLOAT:
        // scanner may have absorbed minus sign into float literal
        // (it needs to do so for INTs and because it cannot know
        // if a number is a FLOAT until it sees a decimal point or
        // an exponent, it also needs to do so for FLOATs)
        if (scanner_.float_value() < 0) {
          // convert -x to -(x)
          scanner_.negate_float_value();
          op = Binary::SUB;
          sym = MINUS;
          absorb = false;
          break;
        }
      // fall through
      default:
        return left;
    }
    if (absorb)
      Next();  // consume operator
    Position right_start(this);
    Expr* right = ParseTerm(&right_start, NULL, indexing);
    if (op == Binary::ADD) {
      // Special case: handle array concatenation with one incomplete operand.
      // Adjust an incomplete composite's type to match the other operand.
      IR::IsCompatibleExpr(proc_, left->type(), right);
      IR::IsCompatibleExpr(proc_, right->type(), left);
      // Special case: handle array concatenation with two incomplete operands.
      if (left->AsComposite() != NULL && left->type()->is_incomplete() &&
          !left->AsComposite()->has_pairs())
        IR::DetermineCompositeType(proc_, left->AsComposite(), false);
      if (right->AsComposite() != NULL && right->type()->is_incomplete() &&
          !right->AsComposite()->has_pairs())
        IR::DetermineCompositeType(proc_, right->AsComposite(), false);
    }
    left = CreateBinary(start, left->type(), left, op,
                        OpcodeFor(sym, left), right);
  }
}


Expr* Parser::ParseComparison(Position* start, szl_string name,
                              Indexing* indexing) {
  Trace t(&tlevel_, "(Comparison");
  Expr* left = ParseSimpleExpr(start, name, indexing);
  Symbol sym = sym_;
  Binary::Op op;
  switch (sym_) {
    case EQL:
      op = Binary::EQL;
      break;
    case NEQ:
      op = Binary::NEQ;
      break;
    case LSS:
      op = Binary::LSS;
      break;
    case LEQ:
      op = Binary::LEQ;
      break;
    case GTR:
      op = Binary::GTR;
      break;
    case GEQ:
      op = Binary::GEQ;
      break;
    default:
      return left;
  }
  Next();  // consume operator
  Position right_start(this);
  Expr* right = ParseSimpleExpr(&right_start, NULL, indexing);
  return CreateBinary(start, SymbolTable::bool_type(), left, op,
                      OpcodeFor(sym, left), right);
}


Expr* Parser::ParseConjunction(Position* start, szl_string name,
                               Indexing* indexing) {
  Expr* left = ParseComparison(start, name, indexing);
  while (true) {
    Symbol sym = sym_;
    Binary::Op op;
    switch (sym_) {
      case CONDAND:
        op = Binary::LAND;
        break;
      case AND:
        op = Binary::AND;
        Warning("%s operator is deprecated; use %s instead",
                scanner_.PrintSymbol(AND), scanner_.PrintSymbol(CONDAND));
        break;
      default:
        return left;
    }
    Next();  // consume operator
    Position right_start(this);
    Expr* right = ParseComparison(&right_start, NULL, indexing);
    if (op == Binary::LAND)
      right->SetLineCounter();
    left = CreateBinary(start, SymbolTable::bool_type(), left, op,
                        OpcodeFor(sym, left), right);
  }
}


Expr* Parser::ParseDisjunction(Position* start, szl_string name,
                               Indexing* indexing) {
  Trace t(&tlevel_, "(Disjunction");
  Expr* left = ParseConjunction(start, name, indexing);
  while (true) {
    Symbol sym = sym_;
    Binary::Op op;
    switch (sym_) {
      case CONDOR:
        op = Binary::LOR;
        break;
      case OR:
        op = Binary::OR;
        Warning("%s operator is deprecated; use %s instead",
                scanner_.PrintSymbol(OR), scanner_.PrintSymbol(CONDOR));
        break;
      default: return left;
    }
    Next();  // consume operator
    Position right_start(this);
    Expr* right = ParseConjunction(&right_start, NULL, indexing);
    if (op == Binary::LOR)
      right->SetLineCounter();
    left = CreateBinary(start, SymbolTable::bool_type(), left, op,
                        OpcodeFor(sym, left), right);
  }
}


Expr* Parser::ParseExpression(Position* start, szl_string name,
                              Indexing* indexing, Type* hint) {
  // Unlike ParseTypeName(), "start" can be non-null even if "name" is null.
  Trace t(&tlevel_, "(Expression");
  Position sym_start(this);
  if (start == NULL)
    start = &sym_start;
  Expr* x = ParseDisjunction(start, name, indexing);
  // if we have a target type, attempt to set it for incompletely typed composites
  if (hint != NULL && x->AsComposite() != NULL && x->type()->is_incomplete())
    IR::SetCompositeType(proc_, x->AsComposite(), hint);
  return x;
}


Expr* Parser::ParseExpression() {
  return ParseExpression(NULL, NULL, NULL, NULL);
}


Expr* Parser::ParseExpression(Type* hint) {
  return ParseExpression(NULL, NULL, NULL, hint);
}


Expr* Parser::ParseBoolExpression(Position* start, szl_string name) {
  // Unlike ParseTypeName(), "start" can be non-null even if "name" is null.
  Trace t(&tlevel_, "(BoolExpression");
  Expr* x = ParseExpression(start, name, NULL, SymbolTable::bool_type());
  if (!x->type()->is_bool()) {
    Error("expression %N (%T) must be of type bool", x, x->type());
    x = BadExpr::New(proc_, x->file_line(), x);
  }
  return x;
}


Break* Parser::ParseBreak(BreakableStatement* bstat) {
  Trace t(&tlevel_, "(Break");
  Position start(this);
  Verify(BREAK);
  if (bstat == NULL)
    Error("'break' must be in a loop or a 'switch', but not in a 'when' statement");
  Expect(SEMICOLON);
  Break* x = Break::New(proc_, Span(&start), bstat);
  x->SetLineCounter();
  return x;
}


Continue* Parser::ParseContinue(Loop* loop) {
  Trace t(&tlevel_, "(Continue");
  Position start(this);
  Verify(CONTINUE);
  if (loop == NULL)
    Error("'continue' must be in a loop, but not in a 'when' statement");
  Expect(SEMICOLON);
  Continue* x = Continue::New(proc_, Span(&start), loop);
  x->SetLineCounter();
  return x;
}


Emit* Parser::ParseEmit() {
  Trace t(&tlevel_, "(Emit");
  Position start(this);
  Verify(EMIT);

  List<VarDecl*>* index_decls;
  Type* elem_type;
  Type* weight_type;  // weight type (NULL, if no weight)
  OutputType* type = NULL;

  // parse output variable identifier
  Position var_start(this);
  Object* obj = Lookup(&var_start, ParseIdent());
  VarDecl* var_decl = obj->AsVarDecl();
  Variable* var = Variable::New(proc_, Span(&var_start), var_decl);
  if (var_decl != NULL && obj->type()->is_output()) {
    type = obj->type()->as_output();
    if (!type->is_static() && InStaticDecl()) {
      Error("output variable %s uses a nonstatic variable and so may not "
            "be used in a static context", obj->name());
    }
    index_decls = type->index_decls();
    elem_type = type->elem_type();
    if (type->weight() != NULL)
      weight_type = type->weight() ->type();
    else
      weight_type = NULL;
  } else {
    Error("%s is not an output variable", obj->name());
    index_decls = List<VarDecl*>::New(proc_);  // make sure we have a list
    elem_type = SymbolTable::bad_type();
    weight_type = SymbolTable::bad_type();
  }

  // parse indices, if any
  List<Expr*>* indices = List<Expr*>::New(proc_);
  int index_no = 0;
  Position index_start(this);
  while (sym_ == LBRACK) {
    Next();  // consume '['

    Type* index_type;
    if (index_no < index_decls->length()) {
      index_type = index_decls->at(index_no)->type();
    } else {
      Error("too many indices (output variable defines only %d dimensions)",
        index_decls->length());
      type = NULL;
      index_type = SymbolTable::bad_type();
    }

    // parse the index even if we have too many
    Expr* index = ParseExpression(index_type);

    // verify index type
    if (!index->type()->IsEqual(index_type, false)) {
      // Special case: to allow rolling out the new protocolcompiler, which
      // generates uints for unsigned integers and string for strings
      // while remaining compatible with existing programs,
      // allow mixing ints and uints, bytes and string.
      const char* warning =
          "table index should be %s, is %s (%N) ; converting automatically";
      if (!ConvertIfPossible(
          index_type, index->type(), &start, &index, warning)) {
        Error("output variable index no. %d, [%N] of type %T, should be %T",
          index_no + 1, index, index->type(), index_type);
        type = NULL;
      }
    }

    // add index and consume ']'
    indices->Append(index);
    Expect(RBRACK);
    index_no++;
  }
  FileLine* index_file_line = Span(&index_start);

  // make sure we have seen all indices
  if (index_no < index_decls->length()) {
    Error("not enough indices (output variable defines %d dimension(s))",
      index_decls->length());
      type = NULL;
  }

  // parse element
  Expect(LARROW);
  Expr* value = ParseExpression(elem_type);
  if (!value->type()->IsEqual(elem_type, false)) {
    const char* warning =
        "'emit' value should be %s, is %s (%N); converting automatically";
    if (!ConvertIfPossible(elem_type, value->type(), &start, &value, warning)) {
      Expr* tuple = ConvertableTuple(&start, value, elem_type);
      if (tuple == NULL) {
        Error("value for 'emit' is %N (%T); should be of type %T",
          value, value->type(), elem_type);
        type = NULL;
        value = BadExpr::New(proc_, value->file_line(), value);
      } else {
        value = tuple;
      }
    }
  }

  // parse weight, if any
  Expr* weight = NULL;
  if (sym_ == WEIGHT) {
    Next();  // consume 'weight'
    weight = ParseExpression(weight_type /* maybe NULL */);
  }

  // check weight constraints
  if (weight_type != NULL) {
    // we expect a weight
    if (weight != NULL) {
      // we've seen a weight => make sure the types match
      if (!weight->type()->IsEqual(weight_type, false)) {
        Error("weight for 'emit' is %N (%T); should be of type %T",
          weight, weight->type(), weight_type);
        type = NULL;
        weight = BadExpr::New(proc_, weight->file_line(), weight);
      }
    } else {
      Error("weight of type %T expected", weight_type);
      type = NULL;
    }
  } else {
    // we don't expect a weight
    if (weight != NULL) {
      Error("weight not allowed (no weight specification in output type)");
      type = NULL;
    }
  }

  Expect(SEMICOLON);

  // Set up the index and element variables and the formats.
  index_decls = NULL;
  VarDecl* elem_decl = NULL;
  Expr* index_format = NULL;
  Expr* elem_format = NULL;
  if (type != NULL) {
    // The locations of the cloned expressions are reset to the location
    // of the emit so that errors and warnings refer to the emit, not the
    // declaration of the output type.
    CloneMap cmap(proc_, table_, top_function(), Span(&start));

    // Create the real index and element variables and add them to the
    // clone map.
    // (We only need the original versions in the OutputType so that we can
    // replace any references to them in the format calls with references
    // to the real ones.  (This is how we would handle parameters when
    // cloning the body of an inline function.)
    // TODO: consider whether the temps need to be named for
    // error messages and stack traces..
    List<VarDecl*>* old_index_decls = type->index_decls();
    index_decls = List<VarDecl*>::New(proc_);
    for (int i = 0; i < old_index_decls->length(); i++) {
      Expr* index = indices->at(i);
      VarDecl* index_decl = CreateTempDecl(index->file_line(), index->type());
      index_decls->Append(index_decl);
      cmap.Insert(old_index_decls->at(i), index_decl);
    }
    elem_decl = CreateTempDecl(value->file_line(), value->type());
    cmap.Insert(type->elem_decl(), elem_decl);

    static Intrinsic* format_fun =
          SymbolTable::universe()->LookupOrDie("format")->AsIntrinsic();
    if (type->index_format_args() != NULL) {
      // There is an index format; clone the args and create call to format()
      List<Expr*>* args = cmap.CloneList(type->index_format_args());
      assert(IsCompatibleIntrinsicArgList(format_fun, args));
      index_format = Call::New(proc_, index_file_line, format_fun, args);
    }
    if (type->elem_format_args() != NULL) {
      // There is an element format; clone the args and create call to format()
      List<Expr*>* args = cmap.CloneList(type->elem_format_args());
      assert(IsCompatibleIntrinsicArgList(format_fun, args));
      elem_format = Call::New(proc_, value->file_line(), format_fun, args);
    }
  }

  return Emit::New(proc_, Span(&start), var, index_decls, elem_decl,
                   indices, value, weight, index_format, elem_format);
}


When* Parser::ParseWhen() {
  Trace t(&tlevel_, "(When");
  Position start(this);
  Verify(WHEN);
  Scope* scope = OpenScope();
  Expect(LPAREN);
  Expr* cond = NULL;
  while (sym_ != RPAREN && sym_ != SCANEOF && cond == NULL) {
    Position cond_start(this);
    szl_string name = NULL;
    if (sym_ == IDENT) {
      Position var_start(this);
      name = ParseIdent();
      if (sym_ == COLON) {
        FileLine* pos =  Span(&var_start);
        Next();
        QuantVarDecl::Kind kind = QuantVarDecl::ALL;
        switch (sym_) {
          case ALL:
            kind = QuantVarDecl::ALL;
            break;
          case EACH:
            kind = QuantVarDecl::EACH;
            break;
          case SOME:
            kind = QuantVarDecl::SOME;
            break;
          default:
            Error("declaration of non-quantifier in 'when' condition");
        }
        Next();
        Type* type = ParseType(NULL, NULL, false);
        Expect(SEMICOLON);
        QuantVarDecl* var = QuantVarDecl::New(proc_, pos, name, type,
                                          top_function(), top_level(), kind);
        Declare(var);
        continue;
      }
    }
    cond = ParseBoolExpression(&cond_start, name);
    cond->SetLineCounter();
  }
  Expect(RPAREN);

  // warn if a 'when' doesn't declare quantifiers
  if (scope->num_entries() == 0)
    Warning("no quantifiers in 'when' - use 'if' instead");

  // it's erroneous to have no condition
  if (cond == NULL)
    Error("empty condition in 'when' statement");

  // add 'all' variables to stack of vars unusable in body
  for (int i = 0; i < scope->num_entries(); i++) {
    QuantVarDecl* var = scope->entry_at(i)->AsQuantVarDecl();
    if (var != NULL && var->kind() == QuantVarDecl::ALL)
      quants_.push(var);
  }

  // break and continue statements don't go across when statements
  Statement* body = ParseControlStatementBody(NULL, NULL);
  body->SetLineCounter();

  // reset quants_
  quants_.Clear();

  CloseScope(scope);
  When* when = When::New(proc_, Span(&start), scope, cond, body);
  // no need to rewrite the 'when' if we have parse errors
  if (error_count() == 0) {
    when->rewrite(proc_, top_function(), top_level());
    if (when->error())
      Error(when->error());
  }
  return when;
}


Statement* Parser::ParseControlStatementBody(BreakableStatement* bstat,
                                             Loop* loop) {
  Trace t(&tlevel_, "(Branch");
  // Control flow structures must always open a new scope regardless of the
  // number of body statements and presence of {}.
  Scope* scope = OpenScope();
  Statement* branch;
  if (sym_ == LBRACE)
    branch = ParseBlock(bstat, loop, false);
  else
    branch = ParseStatement(bstat, loop);
  CloseScope(scope);
  return branch;
}


If* Parser::ParseIf(BreakableStatement* bstat, Loop* loop) {
  Trace t(&tlevel_, "(If");
  Position start(this);
  Verify(IF);
  Expect(LPAREN);
  Expr* cond = ParseBoolExpression(NULL, NULL);
  Expect(RPAREN);
  Statement* then_part = ParseControlStatementBody(bstat, loop);
  Statement* else_part;
  if (sym_ == ELSE) {
    Next();
    else_part = ParseControlStatementBody(bstat, loop);
  } else {
    // Zero-length span just after the "then" part.
    Position pos(this);
    FileLine* else_fl = FileLine::New(proc_, pos.file_name, pos.line,
                                      pos.offset, 0);
    else_part = Empty::New(proc_, else_fl);
  }
  then_part->SetLineCounter();
  else_part->SetLineCounter();
  return If::New(proc_, Span(&start), cond, then_part, else_part);
}


Loop* Parser::ParseDo() {
  Trace t(&tlevel_, "(Do");
  Position start(this);
  Verify(DO);
  Loop* loop = Loop::New(proc_, Span(&start), DO);
  Statement* body = ParseControlStatementBody(loop, loop);
  body->SetLineCounter();
  Expect(WHILE);
  Expect(LPAREN);
  Expr* cond = ParseBoolExpression(NULL, NULL);
  Expect(RPAREN);
  Expect(SEMICOLON);
  loop->set_cond(cond);
  loop->set_body(body);
  loop->set_file_line(Span(&start));
  return loop;
}


Loop* Parser::ParseWhile() {
  Trace t(&tlevel_, "(While");
  Position start(this);
  Verify(WHILE);
  Expect(LPAREN);
  Expr* cond = ParseBoolExpression(NULL, NULL);
  Expect(RPAREN);
  Loop* loop = Loop::New(proc_, Span(&start), WHILE);
  Statement* body = ParseControlStatementBody(loop, loop);
  body->SetLineCounter();
  loop->set_cond(cond);
  loop->set_body(body);
  loop->set_file_line(Span(&start));
  return loop;
}


Loop* Parser::ParseFor() {
  Trace t(&tlevel_, "(For");
  Position start(this);
  Verify(FOR);
  Scope* scope = OpenScope();
  Expect(LPAREN);
  Statement* before = NULL;
  Expr* cond = NULL;
  Statement* after = NULL;
  if (sym_ != SEMICOLON)
    before = ParseSimpleStatement(false, false);
  Expect(SEMICOLON);
  if (sym_ != SEMICOLON)
    cond = ParseBoolExpression(NULL, NULL);
  Expect(SEMICOLON);
  // TODO: disallow declarations in this position?
  if (sym_ != RPAREN)
    after = ParseSimpleStatement(false, false);
  Expect(RPAREN);
  Loop* loop = Loop::New(proc_, Span(&start), FOR);
  Statement* body = ParseControlStatementBody(loop, loop);
  body->SetLineCounter();
  loop->set_before(before);
  loop->set_cond(cond);
  loop->set_after(after);
  loop->set_body(body);
  CloseScope(scope);
  loop->set_file_line(Span(&start));
  return loop;
}


Result* Parser::ParseResult() {
  Trace t(&tlevel_, "(Result");
  Position start(this);
  Verify(RESULT);
  Expr* expr = ParseExpression();
  Expect(SEMICOLON);
  StatExpr* statexpr = NULL;
  if (statexpr_stack_.is_empty()) {
    Error("result statement must be in ?{}");
    return Result::New(proc_, Span(&start), NULL, NULL, NULL);
  }
  statexpr = statexpr_stack_.mutable_top();
  Type* type = expr->type();
  if (statexpr->type()->is_incomplete()) {
    // First result statement encountered; set type and create temporary.
    statexpr->set_type(type);
    statexpr->set_tempvar(CreateTempVar(expr));
    Variable* var = Variable::New(proc_, Span(&start), statexpr->tempvar()->var_decl());
    statexpr->set_var(var);
  }
  if (!IR::IsCompatibleExpr(proc_, statexpr->type(), expr)) {
    Error("result expression (%N) is not compatible "
          "with previous result type (%T)", expr, statexpr->type());
  }
  Variable* var = Variable::New(proc_, Span(&start), statexpr->tempvar()->var_decl());
  return Result::New(proc_, Span(&start), statexpr, var, expr);
}


Return* Parser::ParseReturn() {
  Trace t(&tlevel_, "(Return");
  // note that even top level code is in a function ($main)
  Position start(this);
  Verify(RETURN);
  Function* fun = top_function();
  FunctionType* ftype = fun->ftype();
  Expr* result = NULL;
  if (sym_ != SEMICOLON) {
    // expect a result
    result = ParseExpression(ftype->result_type());
    // make sure we expect a result and have the right result type
    if (ftype->has_result()) {
      if (!IR::IsCompatibleExpr(proc_, ftype->result_type(), result)) {
        const char* warning =
            "%s function, returning %s: %N; converting automatically";
        if (!ConvertIfPossible(
            ftype->result_type(), result->type(), &start, &result, warning)) {
          Error("result (%N) is not compatible with function result type (%T)",
                result, ftype->result_type());
          result = BadExpr::New(proc_, result->file_line(), result);
        }
      }
    } else {
      Error("function %s does not expect a result", fun->name());
    }
  } else {
    // make sure we don't expect a result
    if (ftype->has_result())
      Error("function %s expects a result", fun->name());
  }
  Expect(SEMICOLON);
  return Return::New(proc_, Span(&start), result);
}


Statement* Parser::ParseCaseStatements(BreakableStatement* bstat, Loop* loop) {
  Trace t(&tlevel_, "(Case");
  // for now we just always create a block, even if there is only one statement
  Position start(this);
  Scope* scope = OpenScope();
  Block* block = Block::New(proc_, Span(&start), top_scope(), false);
  block->SetLineCounter();
  block->Append(ParseStatement(bstat, loop));
  while (sym_ != CASE && sym_ != DEFAULT && sym_ != RBRACE && sym_ != SCANEOF)
    block->Append(ParseStatement(bstat, loop));
  block->set_file_line(Span(&start));
  CloseScope(scope);
  return block;
}


Switch* Parser::ParseSwitchStatement(Loop* loop) {
  Trace t(&tlevel_, "(Switch");
  Position start(this);
  Verify(SWITCH);
  // parse tag
  Expect(LPAREN);
  Expr* tag = ParseExpression();
  if (!tag->type()->is_basic()) {
    Error("switch tag %N (%T) - must be basic type", tag, tag->type());
    tag = BadExpr::New(proc_, tag->file_line(), tag);
  }
  Switch* switch_statement = Switch::New(proc_, Span(&start), tag);
  Expect(RPAREN);

  // parse cases
  Expect(LBRACE);
  List<Case*>* cases = List<Case*>::New(proc_);
  while (sym_ == CASE) {
    Next();
    List<Expr*>* labels = List<Expr*>::New(proc_);
    // parse case label list
    while (true) {
      Expr* label = ParseExpression(tag->type());
      if (!label->type()->IsEqual(tag->type(), false)) {
        Error(" label %N (type %T) should be of type %T", label, label->type(), tag->type());
        label = BadExpr::New(proc_, label->file_line(), label);
      }
      labels->Append(label);
      if (sym_ == COMMA) {
        Next();
      } else {
        break;
      }
    }
    Expect(COLON);
    Statement* stat = ParseCaseStatements(switch_statement, loop);
    cases->Append(Case::New(proc_, labels, stat));
  }
  Expect(DEFAULT);
  Expect(COLON);
  Statement* default_case = ParseCaseStatements(switch_statement, loop);
  Expect(RBRACE);

  switch_statement->set_file_line(Span(&start));
  switch_statement->set_cases(cases, default_case);
  return switch_statement;
}


Assignment* Parser::ParseAssignment(Position* start, Expr* lvalue,
                                    bool expect_semi) {
  Trace t(&tlevel_, "(Assignment");
  // complete assignment parse
  Verify(ASSIGN);
  Expr* rvalue = ParseExpression();

  // if either lvalue or rvalue are bad, don't create more error messages
  if (lvalue->AsBadExpr() == NULL && rvalue->AsBadExpr() == NULL) {
    // otherwise make sure assignment is legal
    Variable* lvar = IR::RootVar(lvalue);
    if (lvar == NULL) {
      Error("%N not valid on lhs of assignment", lvalue);
      lvalue = BadExpr::New(proc_, lvalue->file_line(), lvalue);
    } else if (IR::IsStaticLvalue(lvalue)) {
      Error("%N is static; cannot assign to it", lvalue);
      lvalue = BadExpr::New(proc_, lvalue->file_line(), lvalue);
    } else if (lvar->type()->is_output()) {
      Error("unimplemented assignment to local table reference");
      lvalue = BadExpr::New(proc_, lvalue->file_line(), lvalue);
    } else if (lvar->var_decl()->AsQuantVarDecl() != NULL) {
      Error("%N is a quantifier; cannot assign to it", lvalue);
      lvalue = BadExpr::New(proc_, lvalue->file_line(), lvalue);
    } else if (!IR::IsCompatibleExpr(proc_, lvalue->type(), rvalue)) {
      const char* warning =
          "Expecting %s, assigning %s: (%N); converting automatically";
      if (ConvertIfPossible(
          lvalue->type(), rvalue->type(), start, &rvalue, warning)) {
        MarkLvalue(lvalue, false);
      } else {
        Expr* comp = ConvertableComposite(start, rvalue, lvalue->type());
        if (comp != NULL) {
          rvalue = comp;
          MarkLvalue(lvalue, false);
        } else {
          Error("type mismatch in assignment: %N (type %T) = %N (type %T)",
                lvalue, lvalue->type(), rvalue, rvalue->type());
          rvalue = BadExpr::New(proc_, rvalue->file_line(), rvalue);
        }
      }
    } else {
      MarkLvalue(lvalue, false);
    }
  }

  if (expect_semi)
    Expect(SEMICOLON);
  return Assignment::New(proc_, Span(start), lvalue, rvalue);
}


// Recognize statements that might confuse code coverage
// These are statements that might have several basic blocks
static bool ComplexForCounters(Statement* s) {
  if (s->AsVarDecl() && s->AsVarDecl()->type()->is_function())
    return true;
  if (s->AsIf() || s->AsSwitch() || s->AsWhen() || s->AsLoop())
    return true;
  if (s->AsBlock())
    return true;
  return false;
}


// Might this statement contain executable code?
// (and not marked elsewhere by the parser)
static bool ExecutableForCounters(Statement* s) {
  if (s->AsAssignment() || s->AsExprStat() || s->AsIncrement())
    return true;
  if (s->AsSaw() || s->AsEmit())
    return true;
  return false;
}


// To report code coverage, executable statements after function declarations
// and other complex statements need counters
static void AddExtraCounters(Block* block) {
  bool need = false;
  for (int i = 0; i < block->length(); i++) {
    Statement* s = block->at(i);
    if (need && ExecutableForCounters(s)) {
      s->SetLineCounter();
      need = false;
    }
    if (ComplexForCounters(s))
      need = true;
  }
}


Block* Parser::ParseBlock(BreakableStatement* bstat, Loop* loop, bool new_scope) {
  Trace t(&tlevel_, "(Block");
  Position start(this);
  Expect(LBRACE);
  Scope* scope = NULL;  // init to avoid gcc warning
  if (new_scope)
    scope = OpenScope();

  Block* block = Block::New(proc_, Span(&start), top_scope(), false);
  while (sym_ != RBRACE && sym_ != SCANEOF)
    block->Append(ParseStatement(bstat, loop));
  AddExtraCounters(block);  // line counters for code coverage

  if (new_scope)
    CloseScope(scope);
  Expect(RBRACE);
  block->set_file_line(Span(&start));
  return block;
}


Statement* Parser::ParseSimpleStatement(bool is_static, bool expect_semi) {
  Trace t(&tlevel_, "(SimpleStatement");
  Position start(this);
  // assignment or declaration
  szl_string name = ParseIdent();
  // consider declarations of package-qualified name
  if (sym_ == COLON)
    return ParseDecl(&start, name, is_static, expect_semi);

  if (is_static)
    Error("variable declaration expected after 'static'");
  Expr* lvalue = ParseExpression(&start, name, NULL, NULL);
  if (sym_ == ASSIGN)
    return ParseAssignment(&start, lvalue, expect_semi);

  if (sym_ == INC || sym_ == DEC) {
    Symbol sym = sym_;
    Next();
    if (IR::RootVar(lvalue) == NULL) {
      Error("cannot apply %Y to %N", sym, lvalue);
      lvalue = BadExpr::New(proc_, lvalue->file_line(), lvalue);
    } else if (!lvalue->type()->IsEqual(SymbolTable::int_type(), false)) {
      Error("cannot apply %Y to %N of type %T", sym, lvalue, lvalue->type());
      lvalue = BadExpr::New(proc_, lvalue->file_line(), lvalue);
    } else if (IR::IsStaticLvalue(lvalue)) {
      Error("%N is static; cannot apply %Y to it", lvalue, sym);
      lvalue = BadExpr::New(proc_, lvalue->file_line(), lvalue);
    } else {
      MarkLvalue(lvalue, true);  // but is still also an rvalue
    }
    if (expect_semi)
      Expect(SEMICOLON);
    return Increment::New(proc_, Span(&start), lvalue, (sym == INC ? 1 : -1));
  }

  // assume it's an expression only (with side-effects; e.g. a function call)
  if (expect_semi)
    Expect(SEMICOLON);
  return ExprStat::New(proc_, Span(&start), lvalue);
}


Statement* Parser::ParseStatement(BreakableStatement* bstat, Loop* loop) {
  Trace t(&tlevel_, "(Statement");
  bool is_static = false;
  switch (sym_) {
    case SEMICOLON: {
      Position start(this);
      Next();
      return Empty::New(proc_, Span(&start));
    }
    case BREAK:
      return ParseBreak(bstat);
    case CONTINUE:
      return ParseContinue(loop);
    case DO:
      return ParseDo();
    case EMIT:
      return ParseEmit();
    case WHEN:
      return ParseWhen();
    case IF:
      return ParseIf(bstat, loop);
    case WHILE:
      return ParseWhile();
    case FOR:
      return ParseFor();
    case LBRACE:
      return ParseBlock(bstat, loop, true);
    case TYPE:
      return ParseTypeDecl(true);
    case RESULT:
      return ParseResult();
    case RETURN:
      return ParseReturn();
    case SWITCH:
      return ParseSwitchStatement(loop);
    case STATIC:
      is_static = true;
      Next();  // consume 'static'
      // fall through
    case IDENT: {
      Statement* s = ParseSimpleStatement(is_static, true);
      return s;
    }
    case PROTO: {
      return ParseProto();
    }
    default:
      Expr* expr = ParseExpression();
      Expect(SEMICOLON);
      return ExprStat::New(proc_, expr->file_line(), expr);
  }
}


Proto* Parser::ParseProto() {
  Trace t(&tlevel_, "(Proto");
  Position start(this);
  const char* including_file = scanner_.current_file_name();

  int include_level = scanner_.ScanProto();
  const char* proto_file = scanner_.current_file_name();
  const szl_string proto_name = proc_->CopyString(scanner_.string_value());
  Proto* proto = Proto::New(proc_, Span(&start), proto_name);

  // Advance to the 1st symbol of the generated source if a proto file was
  // opened; advance to the next symbol after the proto clause otherwise
  Next();
  if (strcmp(including_file, proto_file) != 0) {  // opened an include
    while (scanner_.IsOpenInclude(proto_file, include_level)) {
      // Store all statements generated for the proto clause in the proto node
      proto->Append(ParseStatement(NULL, NULL));
    }
  }
  return proto;
}


Scope* Parser::OpenMain(Position* start, FileLine* init_fl) {
  // Set up implicit $main signature:
  //   $main(input: bytes, input_key: bytes)
  FunctionType* ftype = FunctionType::NewUnfinished(proc_, NULL, NULL);
  ftype->add_parameter(Field::New(proc_, init_fl, "input",
                                  SymbolTable::bytes_type()));
  ftype->add_parameter(Field::New(proc_, init_fl, "input_key",
                                  SymbolTable::bytes_type()));
  ftype->Finish(proc_);

  // Initialize, but don't declare $main (no need because it cannot be used;
  // declaring will enter it into universe scope which will cause
  // double declaration errors for subsequent compiles)
  Function* main = Function::New(proc_, Span(start), "$main", ftype, NULL, 1);
  Scope* main_scope = OpenFunctionScope(main);
  CreateParameters(main);   // add parameters to scope
  table_->set_main_function(main);

  // Initialize program body
  Block* body = Block::New(proc_, Span(start), main_scope, true);
  body->SetLineCounter();
  main->set_body(body);
  table_->set_program(body);

  return main_scope;
};


void Parser::CloseMain(Scope* scope, Position* start) {
  Function* main = table_->main_function();
  Block* body = table_->program();

  AddExtraCounters(body);
  body->set_file_line(Span(start));
  main->set_file_line(body->file_line());
  table_->add_function(main);

  CloseFunctionScope(scope);
};


void Parser::ParseProgram() {
  // get the first symbol
  Next();

  Trace t(&tlevel_, "(Program");
  Position start(this);

  // set up main
  FileLine* init_fl = FileLine::New(proc_, "initialization", 1, 0, 0);
  Scope* main_scope = OpenMain(&start, init_fl);
  assert(table_->main_function() != NULL);

  // parse main block
  Block* body = table_->program();
  assert(body != NULL);
  while (sym_ != SCANEOF)
    body->Append(ParseStatement(NULL, NULL));

  CloseMain(main_scope, &start);
}


void Parser::CheckForInputProtoConversion(Variable* var, TupleType* type) {
  // The first time this function is called with the "input" parameter to
  // "main" and with a named type, remember the type.  This is used to
  // determine the proto type of the input source.
  if (table_->input_proto() == NULL && type->type_name() != NULL) {
    if (var->is_param() && var->owner()->level() == 1 &&
        strcmp(var->name(), "input") == 0) {
      table_->set_input_proto(type);
    }
  }
}


VarDecl* Parser::CreateTempDecl(FileLine* pos, Type* type) {
  VarDecl* decl = VarDecl::New(proc_, pos, NULL, type,
                               top_function(), top_level(), false, NULL);
  if (decl->is_static())
    table_->add_static(decl);
  else
    top_function()->AddLocal(decl);
  return decl;
}


TempVariable* Parser::CreateTempVar(Expr* src) {
  VarDecl* decl = CreateTempDecl(src->file_line(), src->type());
  return TempVariable::New(proc_, decl, src);
}

void Parser::ValidateTableName(szl_string name) {
  // make sure output variable names are unique
  // across different scopes (because we need
  // unique table names for output) => search the
  // existing list of static declarations
  Statics* s = table_->statics();
  for (int i = s->length(); i-- > 0; ) {
    VarDecl* var = s->at(i);
    if (var->type()->is_output() && strcmp(var->name(), name) == 0) {
      Error("output variable %s already declared in a different scope", name);
      break;
    }
  }
}

}  // namespace sawzall
