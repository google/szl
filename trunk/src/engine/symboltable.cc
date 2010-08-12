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
#include <math.h>
#include <assert.h>
#include <time.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"

DECLARE_bool(szl_bb_count);


namespace sawzall {

// ------------------------------------------------------------------------------
// Implementation of SymbolTable

Proc::Proc* SymbolTable::init_proc_ = NULL;

List<TableType*>* SymbolTable::table_types_ = NULL;
TableType* SymbolTable::collection_type_ = NULL;
TableType* SymbolTable::sum_type_ = NULL;

Scope* SymbolTable::universe_ = NULL;
FileLine* SymbolTable::init_file_line_ = NULL;

BadType* SymbolTable::bad_type_ = NULL;
IncompleteType* SymbolTable::incomplete_type_ = NULL;

BasicType* SymbolTable::int_type_ = NULL;
BasicType* SymbolTable::uint_type_ = NULL;
BasicType* SymbolTable::float_type_ = NULL;
BasicType* SymbolTable::string_type_ = NULL;
BasicType* SymbolTable::time_type_ = NULL;
BasicType* SymbolTable::bytes_type_ = NULL;
BasicType* SymbolTable::bool_type_ = NULL;
BasicType* SymbolTable::fingerprint_type_ = NULL;
BasicType* SymbolTable::void_type_ = NULL;

ArrayType* SymbolTable::array_of_bool_type_ = NULL;
ArrayType* SymbolTable::array_of_int_type_ = NULL;
ArrayType* SymbolTable::array_of_fingerprint_type_ = NULL;
ArrayType* SymbolTable::array_of_float_type_ = NULL;
ArrayType* SymbolTable::array_of_time_type_ = NULL;
ArrayType* SymbolTable::array_of_string_type_ = NULL;
ArrayType* SymbolTable::array_of_bytes_type_ = NULL;
ArrayType* SymbolTable::array_of_uint_type_ = NULL;

MapType* SymbolTable::map_string_of_bytes_type_ = NULL;
MapType* SymbolTable::map_string_of_int_type_ = NULL;
MapType* SymbolTable::proto_enum_map_type_ = NULL;

TupleType* SymbolTable::any_tuple_type_ = NULL;

ArrayType* SymbolTable::array_of_incomplete_type_ = NULL;
Field* SymbolTable::incomplete_field_ = NULL;
Field* SymbolTable::void_field_ = NULL;

IntForm* SymbolTable::int_form_ = NULL;
UIntForm* SymbolTable::uint_form_ = NULL;
FloatForm* SymbolTable::float_form_ = NULL;
StringForm* SymbolTable::string_form_ = NULL;
TimeForm* SymbolTable::time_form_ = NULL;
BytesForm* SymbolTable::bytes_form_ = NULL;
BoolForm* SymbolTable::bool_form_ = NULL;
FingerprintForm* SymbolTable::fingerprint_form_ = NULL;

VarDecl* SymbolTable::output_var_ = NULL;
VarDecl* SymbolTable::stdout_var_ = NULL;
VarDecl* SymbolTable::stderr_var_= NULL;
VarDecl* SymbolTable::undef_cnt_var_ = NULL;
VarDecl* SymbolTable::undef_details_var_ = NULL;
VarDecl* SymbolTable::line_count_var_ = NULL;

Literal* SymbolTable::bool_t_ = NULL;
Literal* SymbolTable::bool_f_ = NULL;
Literal* SymbolTable::int_m1_ = NULL;
Literal* SymbolTable::uint_m1_ = NULL;
Literal* SymbolTable::int_0_ = NULL;
Literal* SymbolTable::int_1_ = NULL;
Literal* SymbolTable::int_10_ = NULL;
Literal* SymbolTable::int_max_ = NULL;
Literal* SymbolTable::float_0_ = NULL;
Literal* SymbolTable::float_1_ = NULL;
Literal* SymbolTable::empty_string_ = NULL;
Literal* SymbolTable::string_utf8_ = NULL;

const char* SymbolTable::dummy_doc = NULL;
const char* SymbolTable::dummy_intrinsic(Proc* proc, Val**& sp) {
  ShouldNotReachHere();
  return "should not reach dummy_intrinsic";
}
void SymbolTable::dummy_intrinsic_nofail(Proc* proc, Val**& sp) {
  ShouldNotReachHere();
}


void SymbolTable::Initialize() {
  if (universe_ == NULL) {
    init_proc_ = Proc::initial_proc();
    table_types_ = List<TableType*>::New(init_proc_);

    collection_type_ = RegisterTableType("collection", false, false);
    sum_type_ = RegisterTableType("sum", false, false);
    universe_ = Scope::New(init_proc_);
    init_file_line_ = FileLine::New(init_proc_, "initialization", 1, 0, 0);

    // predefined types
    // - for better performance define in order of usage frequency
    bad_type_ = BadType::New(init_proc_);
    incomplete_type_ = IncompleteType::New(init_proc_);
    int_type_ = DefineBasic(BasicType::INT, true);
    uint_type_ = DefineBasic(BasicType::UINT, true);
    float_type_ = DefineBasic(BasicType::FLOAT, true);
    string_type_ = DefineBasic(BasicType::STRING, true);
    time_type_ = DefineBasic(BasicType::TIME, true);
    bytes_type_ = DefineBasic(BasicType::BYTES, true);
    bool_type_ = DefineBasic(BasicType::BOOL, true);
    fingerprint_type_ = DefineBasic(BasicType::FINGERPRINT, true);
    void_type_ = DefineBasic(BasicType::VOID, false);

    int_form_ = int_type_->int_form();
    uint_form_ = uint_type_->uint_form();
    float_form_ = float_type_->float_form();
    string_form_ = string_type_->string_form();
    time_form_ = time_type_->time_form();
    bytes_form_ = bytes_type_->bytes_form();
    bool_form_ = bool_type_->bool_form();
    fingerprint_form_ = fingerprint_type_->fingerprint_form();

    // predefined output variables
    output_var_ = DefineOutputBytesVar("output", "/dev/stdout");
    stdout_var_ = DefineOutputStringVar("stdout", "/dev/stdout");
    stderr_var_ = DefineOutputStringVar("stderr", "/dev/stderr");
    undef_cnt_var_ = DefineUndefCntVar();
    undef_details_var_ = DefineUndefDetailsVar();
    if (FLAGS_szl_bb_count)
      line_count_var_ = DefineLineCountVar();

    // predefined Vals in Factory. (after bool_type, before NewBool)
    Factory::Initialize(init_proc_);

    // frequently used constants
    bool_t_ = Literal::NewBool(init_proc_, init_file_line_, "true", true);
    bool_f_ = Literal::NewBool(init_proc_, init_file_line_, "false", false);
    // the m1 (minus one) variables are all ones, used for bitwise complement
    int_m1_ = Literal::NewInt(init_proc_, init_file_line_, NULL, -1);
    uint_m1_ = Literal::NewUInt(init_proc_, init_file_line_, NULL, ~0ULL);
    int_0_ = Literal::NewInt(init_proc_, init_file_line_, NULL, 0);
    int_1_ = Literal::NewInt(init_proc_, init_file_line_, NULL, 1);
    int_10_ = Literal::NewInt(init_proc_, init_file_line_, NULL, 10);
    int_max_ = Literal::NewInt(init_proc_, init_file_line_, NULL, kint64max);
    float_0_ = Literal::NewFloat(init_proc_, init_file_line_, NULL, 0.0);
    float_1_ = Literal::NewFloat(init_proc_, init_file_line_, NULL, 1.0);
    empty_string_ = Literal::NewString(init_proc_, init_file_line_, NULL, "");
    string_utf8_ = Literal::NewString(init_proc_, init_file_line_, NULL, "UTF-8");

    // convenient helper types
    Field* string_field = Field::New(init_proc_, init_file_line_, NULL, string_type_);
    Field* int_field = Field::New(init_proc_, init_file_line_, NULL, int_type_);
    Field* uint_field = Field::New(init_proc_, init_file_line_, NULL, uint_type_);
    array_of_bool_type_ = ArrayType::New(init_proc_, Field::New(init_proc_, init_file_line_, NULL, bool_type_));
    Field* bytes_field = Field::New(init_proc_, init_file_line_, NULL, bytes_type_);
    array_of_int_type_ = ArrayType::New(init_proc_, int_field);
    array_of_fingerprint_type_ = ArrayType::New(init_proc_, Field::New(init_proc_, init_file_line_, NULL, fingerprint_type_));
    array_of_float_type_ = ArrayType::New(init_proc_, Field::New(init_proc_, init_file_line_, NULL, float_type_));
    array_of_time_type_ = ArrayType::New(init_proc_, Field::New(init_proc_, init_file_line_, NULL, time_type_));
    array_of_string_type_ = ArrayType::New(init_proc_, string_field);
    array_of_bytes_type_ = ArrayType::New(init_proc_, bytes_field);
    array_of_uint_type_ = ArrayType::New(init_proc_, uint_field);
    map_string_of_bytes_type_ = MapType::New(init_proc_, string_field, bytes_field);
    map_string_of_int_type_ = MapType::New(init_proc_, string_field, int_field);
    proto_enum_map_type_ = MapType::New(init_proc_,
        Field::New(init_proc_, init_file_line_, "enum_value", int_type_),
        Field::New(init_proc_, init_file_line_, "enum_name", string_type_));
    array_of_incomplete_type_ = ArrayType::New(init_proc_, Field::New(init_proc_, init_file_line_, NULL, incomplete_type_));
    any_tuple_type_ = TupleType::New(init_proc_, Scope::New(init_proc_),
                                     false /* not proto */,
                                     false /* not message */,
                                     true /* predefined */);
    incomplete_field_ = Field::New(init_proc_, init_file_line_, NULL,
                                   incomplete_type_);

    void_field_ = Field::New(init_proc_, init_file_line_, NULL, void_type_);

    // predefined constants
    DefineCon(bool_t_);
    DefineCon(bool_f_);
    DefineCon(Literal::NewFloat(init_proc_, init_file_line_, "PI", M_PI));
    DefineCon(Literal::NewFloat(init_proc_, init_file_line_, "inf", HUGE_VAL));
    DefineCon(Literal::NewFloat(init_proc_, init_file_line_, "nan", NAN));
    DefineCon(Literal::NewFloat(init_proc_, init_file_line_, "Inf", HUGE_VAL));
    DefineCon(Literal::NewFloat(init_proc_, init_file_line_, "NaN", NAN));

    // predefined times
#define DEF(name, secs) DefineCon(Literal::NewTime(init_proc_, init_file_line_, #name, 1000000LL*secs))
    DEF(SECOND, 1);
    DEF(SEC, 1);
    DEF(MINUTE, 60);
    DEF(MIN, 60);
    DEF(HOUR, 60*60);
    DEF(HR, 60*60);
#undef DEF

    // intrinsics for which special code is generated in the backend
    // (no corresponding CFunction, but get the can fail / cannot fail right)
    RegisterIntrinsic("DEBUG", Intrinsic::DEBUG, int_type_,
                              dummy_intrinsic_nofail, dummy_doc,
                              Intrinsic::kNormal);
    RegisterIntrinsic("def", Intrinsic::DEF, bool_type_,
                              dummy_intrinsic_nofail, dummy_doc,
                              Intrinsic::kNormal);

    // intrinsics translated into special nodes - recognized in the parser
    // (result type is not strictly needed here, though we choose a type that
    // is closest to the real type if possible)
    RegisterIntrinsic("convert", Intrinsic::CONVERT, incomplete_type_,
                              dummy_intrinsic, dummy_doc,
                              Intrinsic::kCanFold);
    RegisterIntrinsic("new", Intrinsic::NEW, incomplete_type_,
                              dummy_intrinsic, dummy_doc,
                              Intrinsic::kCanFold);
    RegisterIntrinsic("regex", Intrinsic::REGEX, string_type_,
                              dummy_intrinsic, dummy_doc,
                              Intrinsic::kCanFold);
    RegisterIntrinsic("saw", Intrinsic::SAW, array_of_string_type_,
                              dummy_intrinsic, dummy_doc,
                              Intrinsic::kNormal);
    RegisterIntrinsic("sawn", Intrinsic::SAWN, array_of_string_type_,
                              dummy_intrinsic, dummy_doc,
                              Intrinsic::kNormal);
    RegisterIntrinsic("sawzall", Intrinsic::SAWZALL,
                              array_of_string_type_,
                              dummy_intrinsic, dummy_doc, Intrinsic::kNormal);
  }
}


void SymbolTable::Clear() {
  assert(universe_ != NULL);
  program_ = NULL;
  main_function_ = NULL;
  statics_.Clear();
  functions_.Clear();
  input_proto_ = NULL;
  proto_types_ = Scope::New(proc_);
}


void SymbolTable::Reset() {
  Clear();
  add_static(output_var_);
  add_static(stdout_var_);
  add_static(stderr_var_);
  add_static(undef_cnt_var_);
  add_static(undef_details_var_);
  if (FLAGS_szl_bb_count)
    add_static(line_count_var_);
}


void SymbolTable::add_static(VarDecl* decl) {
  assert(decl->is_static());
  statics_.Append(decl);
}


void SymbolTable::add_function(Function* fun) {
  functions_.Append(fun);
}


void SymbolTable::set_program(Block* program) {
  assert(program->is_program());
  program_ = program;
}


// These helper functions must match those in scanner.cc.
// TODO: Factor them out!
static bool is_letter(int ch) {
  return ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z') || ch == '_';
}


static bool is_digit(int ch) {
  return '0' <= ch && ch <= '9';
}


TableType* SymbolTable::RegisterTableType(szl_string name, bool has_param, bool has_weight) {
  // name must be a legal Sawzall identifier
  if (name != NULL && is_letter(name[0])) {
    const char* p = &name[1];
    while (is_letter(*p) || is_digit(*p))
      p++;
    if (*p != '\0')
      return NULL;  // we didn't make it to the end of the name
  } else {
    return NULL;  // name doesn't exist or didn't start with a letter
  }
  // make sure name has not been registered differently before
  TableType* t = LookupTableType(name);
  if (t == NULL) {
    t = TableType::New(init_proc_, name, has_param, has_weight);
    table_types_->Append(t);
    return t;
  }
  if (t->ConsistentWith(has_param, has_weight))
    return t;
  return NULL;
}


TableType* SymbolTable::LookupTableType(szl_string name) {
  assert(name != NULL);
  // try to find it
  for (int i = 0; i < table_types_->length(); i++)
    if (strcmp(table_types_->at(i)->name(), name) == 0)
      return table_types_->at(i);
  // not found
  return NULL;
}


void SymbolTable::RegisterIntrinsic(
    szl_string name, Intrinsic::Kind kind, Type* function_or_result_type,
    Intrinsic::CFunctionCanFail cfun, const char* doc, int attr) {
  assert(kind != Intrinsic::INTRINSIC);
  FunctionType* ftype;
  if (function_or_result_type->is_function())
    ftype = function_or_result_type->as_function();
  else
    ftype = FunctionType::New(init_proc_)->res(function_or_result_type);
  Intrinsic* fun = Intrinsic::New(init_proc_, init_file_line_, name, ftype,
                                  kind, (Intrinsic::CFunction)cfun, doc,
                                  attr, true /* can_fail */);
  universe()->InsertOrOverloadOrDie(fun);
}


void SymbolTable::RegisterIntrinsic(
    szl_string name, FunctionType* type,
    Intrinsic::CFunctionCanFail cfun, const char* doc, int attr) {
  Intrinsic* fun = Intrinsic::New(init_proc_, init_file_line_, name, type,
                                  Intrinsic::INTRINSIC,
                                  (Intrinsic::CFunction)cfun, doc,
                                  attr, true /* can_fail */);
  universe()->InsertOrOverloadOrDie(fun);
}


void SymbolTable::RegisterIntrinsic(
    szl_string name, Intrinsic::Kind kind,
    Type* function_or_result_type,
    Intrinsic::CFunctionCannotFail cfun, const char* doc, int attr) {
  assert(kind != Intrinsic::INTRINSIC);
  FunctionType* ftype;
  if (function_or_result_type->is_function())
    ftype = function_or_result_type->as_function();
  else
    ftype = FunctionType::New(init_proc_)->res(function_or_result_type);
  Intrinsic* fun = Intrinsic::New(init_proc_, init_file_line_, name, ftype,
                                  kind, (Intrinsic::CFunction)cfun, doc,
                                  attr, false /* can_fail */);
  universe()->InsertOrOverloadOrDie(fun);
}


void SymbolTable::RegisterIntrinsic(
    szl_string name,
    FunctionType* type,
    Intrinsic::CFunctionCannotFail cfun, const char* doc, int attr) {
  Intrinsic* fun = Intrinsic::New(init_proc_, init_file_line_, name,
                   type, Intrinsic::INTRINSIC, (Intrinsic::CFunction)cfun, doc,
                   attr, false /* can_fail */);
  universe()->InsertOrOverloadOrDie(fun);
}


void SymbolTable::RegisterType(szl_string name, Type* type) {
  TypeName* tname = TypeName::New(init_proc_, init_file_line_, name);
  tname->set_type(type);
  universe()->InsertOrDie(tname);
}


BasicType* SymbolTable::DefineBasic(BasicType::Kind kind, bool visible) {
  BasicType* type = BasicType::New(init_proc_, kind);
  TypeName* tname = TypeName::New(init_proc_, init_file_line_, BasicType::Kind2String(kind));
  tname->set_type(type);
  if (visible)
    universe()->InsertOrDie(tname);
  return type;
}


VarDecl* SymbolTable::DefineOutputBytesVar(const char* name, const char* file) {
  List<VarDecl*>* index_decls = List<VarDecl*>::New(init_proc_);
  VarDecl* elem_decl = VarDecl::New(init_proc_, init_file_line_, NULL,
                               SymbolTable::bytes_type(), NULL, 0, false, NULL);
  List<Expr*>* index_format_args = List<Expr*>::New(init_proc_);
  index_format_args->Append(Literal::NewString(init_proc_, init_file_line_, NULL,
                                          file));
  OutputType* type = OutputType::New(
      init_proc_, SymbolTable::collection_type(), NULL, -1, index_decls,
      elem_decl, NULL, false, index_format_args, NULL, true, NULL);
  VarDecl* var = VarDecl::New(init_proc_, init_file_line_, name, type,
                              NULL, 0, false, NULL);
  universe()->InsertOrDie(var);
  return var;
}


VarDecl* SymbolTable::DefineOutputStringVar(const char* name, const char* file) {
  List<VarDecl*>* index_decls = List<VarDecl*>::New(init_proc_);
  VarDecl* elem_decl = VarDecl::New(init_proc_, init_file_line_, "s",
                              SymbolTable::string_type(), NULL, 0, false, NULL);
  List<Expr*>* index_format_args = List<Expr*>::New(init_proc_);
  index_format_args->Append(Literal::NewString(init_proc_, init_file_line_, NULL,
                                          file));
  List<Expr*>* elem_format_args = List<Expr*>::New(init_proc_);
  elem_format_args->Append(Literal::NewString(init_proc_, init_file_line_, NULL,
                                         "%s\n"));
  elem_format_args->Append(Variable::New(init_proc_, init_file_line_, elem_decl));
  OutputType* type = OutputType::New(
      init_proc_, SymbolTable::collection_type(), NULL, -1, index_decls,
      elem_decl, NULL, false, index_format_args, elem_format_args, true, NULL);
  VarDecl* var = VarDecl::New(init_proc_, init_file_line_, name, type,
                              NULL, 0, false, NULL);
  universe()->InsertOrDie(var);
  return var;
}


VarDecl* SymbolTable::DefineUndefDetailsVar() {
  VarDecl* index_decl = VarDecl::New(init_proc_, init_file_line_, "msg",
                              SymbolTable::string_type(), NULL, 0, false, NULL);
  List<VarDecl*>* index_decls = List<VarDecl*>::New(init_proc_);
  index_decls->Append(index_decl);
  VarDecl* elem_decl = VarDecl::New(init_proc_, init_file_line_, NULL,
                              SymbolTable::int_type(), NULL, 0, false, NULL);
  OutputType* type = OutputType::New(
      init_proc_, SymbolTable::sum_type(), NULL, -1, index_decls, elem_decl,
      NULL, false, NULL, NULL, true, NULL);
  VarDecl* var = VarDecl::New(init_proc_, init_file_line_, "_undef_details",
                              type, NULL, 0, false, NULL);
  var->set_doc("accumulate counts of detailed undef messages");
  universe()->InsertOrDie(var);
  return var;
}


VarDecl* SymbolTable::DefineUndefCntVar() {
  // black magic using undocumented techniques
  List<VarDecl*>* index_decls = List<VarDecl*>::New(init_proc_);
  VarDecl* elem_decl = VarDecl::New(init_proc_, init_file_line_, NULL,
                              SymbolTable::int_type(), NULL, 0, false, NULL);
  // OutputType is defined in type.h
  OutputType* type = OutputType::New(
      init_proc_, SymbolTable::sum_type(), NULL, -1,  index_decls, elem_decl,
      NULL, false, NULL, NULL, true, NULL);
  VarDecl* var = VarDecl::New(init_proc_, init_file_line_, "_undef_cnt", type,
                              NULL, 0, false, NULL);
  var->set_doc("count the number of records with undefineds");
  universe()->InsertOrDie(var);
  return var;
}


VarDecl* SymbolTable::DefineLineCountVar() {
  // _line_count: table sum[offset: string] of count: int;
  // (cf. DefineUndefDetailsVar())
  VarDecl* index_decl = VarDecl::New(init_proc_, init_file_line_, "offset",
                              SymbolTable::string_type(), NULL, 0, false, NULL);
  List<VarDecl*>* index_decls = List<VarDecl*>::New(init_proc_);
  index_decls->Append(index_decl);
  VarDecl* elem_decl = VarDecl::New(init_proc_, init_file_line_, "count",
                              SymbolTable::int_type(), NULL, 0, false, NULL);
  OutputType* type = OutputType::New(
      init_proc_, SymbolTable::sum_type(), NULL, -1, index_decls, elem_decl,
      NULL, false, NULL, NULL, true, NULL);

  VarDecl* var = VarDecl::New(init_proc_, init_file_line_, "_line_counts",
                              type, NULL, 0, false, NULL);
  var->set_doc("number of times line was executed");
  universe()->InsertOrDie(var);
  return var;
}


void SymbolTable::DefineCon(Literal* val) {
  universe()->InsertOrDie(val);
}


SymbolTable::SymbolTable(Proc* proc)
  : proc_(proc),
    statics_(proc),
    functions_(proc) {
  Clear();
}


}  // namespace sawzall
