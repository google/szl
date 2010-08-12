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

// ----------------------------------------------------------------------------
// Syntax tree traversal

// Compute the variable denoted by an lvalue. This is the variable
// to be undefined if the lvalue is used in an assignment and the
// value to be assigned to is undefined.
Variable* UndefVar(Expr* lvalue);


// ----------------------------------------------------------------------------
// Variable offset allocation

// Returns the combined size of allocated globals
size_t ComputeStaticOffsets(List<VarDecl*>* vars, int offset, bool do_params);

// Returns the combined size of allocated locals
size_t ComputeLocalOffsets(List<VarDecl*>* vars, int offset, bool do_params, bool positive);


// ----------------------------------------------------------------------------
// Opcode selection

Opcode VariableAccess(Type* var_type, bool is_load, bool is_lhs, int delta);
Opcode SelectorAccess(Type* field_type, bool is_load, bool is_lhs, int delta);
Opcode IndexedAccess(Type* array_type, bool is_load, bool is_lhs, int delta);
Opcode MappedKey(MapType* map_type, bool is_load, bool is_lhs, int delta,
                 Proc* proc, int* error_count);
Opcode MappedValue(MapType* map_type, bool is_load, bool is_lhs, int delta,
                   Proc* proc, int* error_count);


// ----------------------------------------------------------------------------
// Regex compilation and regex patterns

// Returns the compiled regex for pattern x, if possible (returns NULL otherwise)
void* CompiledRegexp(Expr* x, Proc* proc, int* error_count);

// Returns the pattern for regex x, if possible (returns "" otherwise)
const char* RegexPattern(Regex* x, Proc* proc, int* error_count);

}  // namespace sawzall
