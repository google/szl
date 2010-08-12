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

namespace sawzall {

// The list of static variable / function declarations.
typedef List<VarDecl*> Statics;
typedef List<Function*> Functions;

// The SymbolTable keeps everything together. universe is the
// predefined scope.

class SymbolTable {
 public:
  SymbolTable(Proc* proc);

  // Initialization
  static void Initialize();
  static bool is_initialized()  { return universe_ != NULL; }
  void Clear();
  void Reset();

  // Code
  Statics* statics()  { return &statics_; }
  Functions* functions()  { return &functions_; }
  Block* program() const  { return program_; }
  Function* main_function() const  { return main_function_; }
  TupleType* input_proto() const  { return input_proto_; }

  void add_static(VarDecl* decl);
  void add_function(Function* fun);
  void set_program(Block* program);
  void set_main_function(Function* fun)  { main_function_ = fun; }
  void set_input_proto(TupleType* proto) { input_proto_ = proto; }

  // Scopes
  static Scope* universe()  { return universe_; }
  static FileLine* init_file_line() { return init_file_line_; }

  // Frequently used types
  static BadType* bad_type()  { return bad_type_; }
  static IncompleteType* incomplete_type()  { return incomplete_type_; }

  static BasicType* int_type()  { return int_type_; }
  static BasicType* uint_type()  { return uint_type_; }
  static BasicType* float_type()  { return float_type_; }
  static BasicType* string_type()  { return string_type_; }
  static BasicType* time_type()  { return time_type_; }
  static BasicType* bytes_type()  { return bytes_type_; }
  static BasicType* bool_type()  { return bool_type_; }
  static BasicType* fingerprint_type()  { return fingerprint_type_; }
  static BasicType* void_type()  { return void_type_; }

  static ArrayType* array_of_bool_type()  { return array_of_bool_type_; }
  static ArrayType* array_of_int_type()  { return array_of_int_type_; }
  static ArrayType* array_of_fingerprint_type()  { return array_of_int_type_; }
  static ArrayType* array_of_float_type() { return array_of_float_type_; }
  static ArrayType* array_of_time_type() { return array_of_time_type_; }
  static ArrayType* array_of_string_type()  { return array_of_string_type_; }
  static ArrayType* array_of_bytes_type()  { return array_of_bytes_type_; }
  static ArrayType* array_of_uint_type()  { return array_of_uint_type_; }

  static MapType* map_string_of_bytes_type()  { return map_string_of_bytes_type_; }
  static MapType* map_string_of_int_type()  { return map_string_of_int_type_; }
  static MapType* proto_enum_map_type()  { return proto_enum_map_type_; }

  // Special type for intrinsics that accept run-time-defined protos.
  // We generalize to a tuple type because intrinsics that use regular
  // tuples could also use this mechanism.  It is the address that we
  // use for equality testing; the contents are never examined and
  // the code generator never sees it.
  static TupleType* any_tuple_type()  { return any_tuple_type_; }

  // Support for unfinished map and array types
  static ArrayType* array_of_incomplete_type()  { return array_of_incomplete_type_; }
  static Field* incomplete_field()  { return incomplete_field_; }
  static Field* void_field()  { return void_field_; }

  // Shortcuts to frequently used forms to avoid virtual method calls
  static IntForm* int_form()  { return int_form_; }
  static UIntForm* uint_form()  { return uint_form_; }
  static FloatForm* float_form()  { return float_form_; }
  static StringForm* string_form()  { return string_form_; }
  static TimeForm* time_form()  { return time_form_; }
  static BytesForm* bytes_form()  { return bytes_form_; }
  static BoolForm* bool_form()  { return bool_form_; }
  static FingerprintForm* fingerprint_form()  { return fingerprint_form_; }

  // Predeclared output variables
  static VarDecl* output_var()  { return output_var_; }
  static VarDecl* stdout_var()  { return stdout_var_; }
  static VarDecl* stderr_var()  { return stderr_var_; }
  static VarDecl* undef_cnt_var()  { return undef_cnt_var_; }
  static VarDecl* undef_details_var()  { return undef_details_var_; }
  static VarDecl* line_count_var()  { return line_count_var_; }

  // Frequently used constants
  static Literal* bool_t()  { return bool_t_; }
  static Literal* bool_f()  { return bool_f_; }
  static Literal* int_m1()  { return int_m1_; }
  static Literal* uint_m1()  { return uint_m1_; }
  static Literal* int_0()  { return int_0_; }
  static Literal* int_1()  { return int_1_; }
  static Literal* int_10()  { return int_10_; }
  static Literal* int_max()  { return int_max_; }
  static Literal* float_0()  { return float_0_; }
  static Literal* float_1()  { return float_1_; }
  static Literal* empty_string()  { return empty_string_; }
  static Literal* string_utf8()  { return string_utf8_; }

  // Dummy intrinsics for special handling.
  static const char* dummy_doc;
  static const char* dummy_intrinsic(Proc* proc, Val**& sp);
  static void dummy_intrinsic_nofail(Proc* proc, Val**& sp);

  // Support for output types
  static TableType* RegisterTableType(szl_string name, bool has_param, bool has_weight);
  static TableType* LookupTableType(szl_string name);
  static TableType* collection_type()  { return collection_type_; }
  static TableType* sum_type() { return sum_type_; }

  // Support for intrinsics
  // The 2nd and 4th are likely to used by external extensions.
  // Please update extensions/exampleintrinsic.cc if these change.
  static void RegisterIntrinsic(
      szl_string name, Intrinsic::Kind kind,
      Type* function_or_result_type,
      Intrinsic::CFunctionCanFail cfun, const char* doc, int attribute);
  static void RegisterIntrinsic(
      szl_string name, FunctionType* type,
      Intrinsic::CFunctionCanFail cfun, const char* doc,
      int attribute = Intrinsic::kNormal);
  static void RegisterIntrinsic(
      szl_string name, Intrinsic::Kind kind,
      Type* function_or_result_result_type,
      Intrinsic::CFunctionCannotFail cfun, const char* doc, int attribute);
  static void RegisterIntrinsic(
      szl_string name, FunctionType* type,
      Intrinsic::CFunctionCannotFail cfun, const char* doc,
      int attribute = Intrinsic::kNormal);

  static void RegisterType(szl_string name, Type* type);

  // Support definition of predefined constants.
  static void DefineCon(Literal* val);

private:
  // Initial process
  static Proc* init_proc_;
  friend void ValTest1();  // access to init_proc_ only

  // Code
  Proc* proc_;
  Block* program_;
  Function* main_function_;
  Statics statics_;
  Functions functions_;
  TupleType* input_proto_;  // deduced proto type of "input"

  // Frequently used types
  static BadType* bad_type_;
  static IncompleteType* incomplete_type_;

  // Protocol buffer types
  Scope* proto_types_;

  static BasicType* int_type_;
  static BasicType* uint_type_;
  static BasicType* float_type_;
  static BasicType* string_type_;
  static BasicType* time_type_;
  static BasicType* bytes_type_;
  static BasicType* bool_type_;
  static BasicType* fingerprint_type_;
  static BasicType* void_type_;

  static ArrayType* array_of_bool_type_;
  static ArrayType* array_of_int_type_;
  static ArrayType* array_of_fingerprint_type_;
  static ArrayType* array_of_float_type_;
  static ArrayType* array_of_time_type_;
  static ArrayType* array_of_string_type_;
  static ArrayType* array_of_bytes_type_;
  static ArrayType* array_of_uint_type_;

  static MapType* map_string_of_bytes_type_;
  static MapType* map_string_of_int_type_;
  static MapType* proto_enum_map_type_;

  static TupleType* any_tuple_type_;

  static ArrayType* array_of_incomplete_type_;
  static Field* incomplete_field_;
  static Field* void_field_;

  static VarDecl* output_var_;
  static VarDecl* stdout_var_;
  static VarDecl* stderr_var_;
  static VarDecl* undef_cnt_var_;
  static VarDecl* undef_details_var_;
  static VarDecl* line_count_var_;

  // form shortcuts
  static IntForm* int_form_;
  static UIntForm* uint_form_;
  static FloatForm* float_form_;
  static StringForm* string_form_;
  static TimeForm* time_form_;
  static BytesForm* bytes_form_;
  static BoolForm* bool_form_;
  static FingerprintForm* fingerprint_form_;

  static Literal* bool_t_;
  static Literal* bool_f_;
  static Literal* int_m1_;
  static Literal* uint_m1_;
  static Literal* int_0_;
  static Literal* int_1_;
  static Literal* int_10_;
  static Literal* int_max_;
  static Literal* float_0_;
  static Literal* float_1_;
  static Literal* empty_string_;
  static Literal* string_utf8_;

  // Table types
  static List<TableType*>* table_types_;
  static TableType* collection_type_;  // always defined
  static TableType* sum_type_; // for reporting undefineds

  // Scopes
  static Scope* universe_;
  static FileLine* init_file_line_;

  static BasicType* DefineBasic(BasicType::Kind kind, bool visible);
  static VarDecl* DefineOutputBytesVar(const char* name, const char* file);
  static VarDecl* DefineOutputStringVar(const char* name, const char* file);
  static VarDecl* DefineUndefCntVar();
  static VarDecl* DefineUndefDetailsVar();
  static VarDecl* DefineLineCountVar();
};

}  // namespace sawzall
