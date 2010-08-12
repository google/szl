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

// IR provides support functions for the Intermediate Representation
// of a Sawzall program. The intermediate representation consists
// of a syntax tree built with Nodes and Types (node.*, type.*).

class Parser;

class IR {
 public:
  // -------------------------------------------------------------------------
  // Structural information

  // Check if x is an l-value (variable, array[i], tuple.f, etc.)
  static bool IsLvalue(Expr* x);

  // Check if an l-value denotes a static variable. If it does,
  // we cannot assign to it after initialization.
  static bool IsStaticLvalue(Expr* x);

  // Determines the top-most (root) variable for an expression x, if any.
  // Returns NULL otherwise. If x has a root variable, it is an l-value.
  static Variable* RootVar(Expr* x);


  // -------------------------------------------------------------------------
  // Expression compatibility

  // Check if op can be used with operands of a certain type.
  static bool IsCompatibleOp(Type* type, Binary::Op op);

  // Check if x is assignment-compatible with type.
  // If x is a Composite with incomplete type, it's type
  // may be adjusted accordingly if compatible.
  static bool IsCompatibleExpr(Proc* proc, Type* type, Expr* x);

  // Check if function argument list is compatible with the funtion type.
  // Note:  args list may be modified by this function to fill in optional
  // parameters.
  static bool IsCompatibleFunctionArgList(Proc* proc, FunctionType* type,
                                          List<Expr*>* args);

  // Check if function argument list exactly matches the function parameter list.
  // Does not modify the argument list.
  static bool IsMatchingFunctionArgList(Proc* proc, FunctionType* type,
                                        const List<Expr*>* args);

  // Try to set the composite type as specified.
  // Returns true if the type could be set, returns false if the composite
  // remains incompletely typed (and the caller reports the error).
  static bool SetCompositeType(Proc* proc, Composite* c, Type* type);

  // Try to set the 'correct' anonymous composite type if all the elements have
  // consistent types (including the types of nested composites), using
  // a map type if there are pairs else an array type.
  // Returns true if any type could be set, returns false if the composite
  // remains incompletely typed (and the caller reports the error).
  // Also optionally supports setting a tuple type if the composite is not
  // paired and cannot be set to an array type.
  static bool DetermineCompositeType(Proc* proc, Composite* c,
                                     bool allow_tuples);

  // Check if a direct or indirect field of a tuple is declared to have the
  // specified tuple type (requiring an infinite size object).
  static bool TupleContainsItself(TupleType* tuple_type, Field* field);


  // -------------------------------------------------------------------------
  // Conversions

  // Create a conversion node convert(<type>, <src>, <params>).
  // If the warning flag is set, a warning is printed if the conversion
  // is not required.
  static Expr* CreateConversion(Parser* parser, FileLine* fl, Type* type,
                                Expr* src, List<Expr*>* params,
                                bool warning, bool implicit);


  // -------------------------------------------------------------------------
  // Translation

  // Compute the instruction opcode for a given operation (sym)
  // and operand type.
  static Opcode OpcodeFor(Symbol sym, Type* type);

};

}  // namespace sawzall
