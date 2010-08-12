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

// The support routines called from generated code are wrapped in a class
// NSupport, which is declared friend of Proc in order to access proc->state_.
//
// Parameter order is important:
// all non-Sawzall value arguments (without ref counts) must precede Sawzall
// values (with ref counts) in the parameter list, so they are pushed last.
// This order guarantees that non-Sawzall values will not be on the stack
// during trap handling that decrements ref counts while popping values.
// A trap cannot occur once a support routine is entered, but it can occur
// when pushing undefined Sawzall arguments on the native stack in preparation
// to a support routine call. See also call area example in frame.h.


namespace sawzall {

class NSupport {
 public:
  // Public helper functions can be called from generated code,
  // so parameter order is important (see header comment).
  static Val* DebugRef(Proc* proc, Val* val);
  static Val* Uniq(Proc* proc, Val*& var);
  static Val* CheckAndUniq(Proc* proc, Val*& var);
  static int Inc(Proc* proc, Val*& var);
  static int Dec(Proc* proc, Val*& var);
  static Val* AddInt(Proc* proc, IntVal* x, IntVal* y);
  static Val* SubInt(Proc* proc, IntVal* x, IntVal* y);
  static Val* MulInt(Proc* proc, IntVal* x, IntVal* y);
  static Val* DivInt(Proc* proc, IntVal* x, IntVal* y);
  static Val* RemInt(Proc* proc, IntVal* x, IntVal* y);
  static Val* ShlInt(Proc* proc, IntVal* x, IntVal* y);
  static Val* ShrInt(Proc* proc, IntVal* x, IntVal* y);
  static Val* AndInt(Proc* proc, IntVal* x, IntVal* y);
  static Val* OrInt(Proc* proc, IntVal* x, IntVal* y);
  static Val* XorInt(Proc* proc, IntVal* x, IntVal* y);
  static Val* AddFloat(Proc* proc, FloatVal* x, FloatVal* y);
  static Val* SubFloat(Proc* proc, FloatVal* x, FloatVal* y);
  static Val* MulFloat(Proc* proc, FloatVal* x, FloatVal* y);
  static Val* DivFloat(Proc* proc, FloatVal* x, FloatVal* y);
  static Val* AddFpr(Proc* proc, FingerprintVal* x, FingerprintVal* y);
  static Val* AddArray(Proc* proc, ArrayVal* x, ArrayVal* y);
  static Val* AddBytes(Proc* proc, BytesVal* x, BytesVal* y);
  static Val* AddString(Proc* proc, StringVal* x, StringVal* y);
  static Val* AddTime(Proc* proc, TimeVal* x, TimeVal* y);
  static Val* SubTime(Proc* proc, TimeVal* x, TimeVal* y);
  static Val* AddUInt(Proc* proc, UIntVal* x, UIntVal* y);
  static Val* SubUInt(Proc* proc, UIntVal* x, UIntVal* y);
  static Val* MulUInt(Proc* proc, UIntVal* x, UIntVal* y);
  static Val* DivUInt(Proc* proc, UIntVal* x, UIntVal* y);
  static Val* ModUInt(Proc* proc, UIntVal* x, UIntVal* y);
  static Val* ShlUInt(Proc* proc, UIntVal* x, UIntVal* y);
  static Val* ShrUInt(Proc* proc, UIntVal* x, UIntVal* y);
  static Val* AndUInt(Proc* proc, UIntVal* x, UIntVal* y);
  static Val* OrUInt(Proc* proc, UIntVal* x, UIntVal* y);
  static Val* XorUInt(Proc* proc, UIntVal* x, UIntVal* y);
  static int CmpInt(IntVal* x, IntVal* y);
  static bool EqlFloat(FloatVal* x, FloatVal* y);
  static bool LssFloat(FloatVal* x, FloatVal* y);
  static bool LeqFloat(FloatVal* x, FloatVal* y);
  static bool EqlBits(Val* x, Val* y);
  static bool LssBits(Val* x, Val* y);
  static int CmpString(StringVal* x, StringVal* y);
  static int EqlString(StringVal* x, StringVal* y);
  static int CmpBytes(BytesVal* x, BytesVal* y);
  static int EqlBytes(BytesVal* x, BytesVal* y);
  static bool EqlArray(ArrayVal* x, ArrayVal* y);
  static bool EqlMap(MapVal* x, MapVal* y);
  static bool EqlTuple(TupleVal* x, TupleVal* y);
  static bool EqlClosure(ClosureVal* x, ClosureVal* y);
  static void FClearB(int i, TupleVal* t);
  static void FSetB(int i, TupleVal* t);
  static bool FTestB(int i, TupleVal* t);
  static Val* XLoad8(Proc* proc, IntVal* x, BytesVal* b);
  static Val* XLoadR(Proc* proc, IntVal* x, StringVal* s);
  static Val* XLoadV(Proc* proc, IntVal* x, ArrayVal* a);
  static Val* XLoadVu(Proc* proc, IntVal* x, ArrayVal* a);
  static Val* MLoadV(Proc* proc, MapVal* m, Val* key);
  static Val* MInsertV(Proc* proc, MapVal* m, Val* key);
  static Val* MIndexV(MapVal* m, IntVal* index);
  static Val* MIndexVu(Proc* proc, MapVal* m, IntVal* index);
  static void MStoreV(MapVal* m, IntVal* index, Val* value);
  static int XStore8(Proc* proc, IntVal* x, BytesVal* b, IntVal* e);
  static int XStoreR(Proc* proc, IntVal* x, StringVal* s, IntVal* e);
  static int XStoreV(Proc* proc, IntVal* x, ArrayVal* a, Val* e);
  static int XInc8(Proc* proc, int8 delta, IntVal* x, BytesVal* b);
  static int XIncR(Proc* proc, int8 delta, IntVal* x, StringVal* s);
  static int XInc64(Proc* proc, int8 delta, IntVal* x, ArrayVal* a);
  static void MInc64(Proc* proc, int8 delta, MapVal* m, IntVal* index);
  static Val* SLoad8(Proc* proc, IntVal* eng, IntVal* beg, BytesVal* b);
  static Val* SLoadR(Proc* proc, IntVal* eng, IntVal* beg, StringVal* s);
  static Val* SLoadV(Proc* proc, IntVal* eng, IntVal* beg, ArrayVal* a);
  static void SStoreV(Proc* proc, IntVal* eng, IntVal* beg, IndexableVal* a, Val* x);
  static Val* NewA(Proc* proc, ArrayType* atype, IntVal* length, Val* init);
  static Val* NewM(Proc* proc, MapType* mtype, IntVal* occupancy);
  static Val* NewB(Proc* proc, IntVal* length, IntVal* init);
  static Val* NewStr(Proc* proc, IntVal* nrunes, IntVal* init);
  static Val* CreateC(Proc* proc, FunctionType* ftype, int entry, Frame* context);
  static Val* CreateB(Proc* proc, int num_args, ...);
  static Val* CreateStr(Proc* proc, int num_args, ...);
  static Val* CreateA(Proc* proc, ArrayType* atype, int num_vals);
  static Val* InitA(Proc* proc, int from_val, int num_args, ...);
  static Val* CreateM(Proc* proc, MapType* mtype, int num_vals);
  static Val* InitM(Proc* proc, int num_args, ...);
  static Val* CreateT(Proc* proc, TupleType* ttype);
  static Val* InitT(Proc* proc, int from_val, int num_args, ...);
  static void CreateTAndStore(Proc* proc, TupleType* ttype, Val* var_index, ...);
  static void OpenO(Proc* proc, Frame* bp, int var_index, int tab_index,
                    IntVal* param);
  static void IncCounter(Proc* proc, int index);

  // The va_list in the following helpers consist of num_args arguments in reverse
  // order. The reverse order is required, because va_args must be read in order
  // to be pushed on the interpreter stack, thereby reversing the order again.
  static Val* Saw(Proc* proc, void** cache, int num_vars, int num_args, ...);
  static void Emit(Proc* proc, int num_args, Val* var, ...);
  static Val* FdPrint(Proc* proc, int fd, int num_args, ...);

  static Proc::Status HandleTrap(char* trap_info, bool fatal, NFrame* fp,
                                 intptr_t sp_adjust, int native_sp_adjust,
                                 Instr*& trap_pc, Val**& trap_sp);

  static void AllocStatics(Proc* proc, size_t statics_size);

  // Return the name of a helper given its address, used in debug mode only
  static const char* HelperName(intptr_t addr);

 private:
  // Private helper functions cannot be called from generated code,
  // so parameter order is not important.

  // Error reporting
  static void ArrayIndexError(Proc* proc, ArrayVal* a, szl_int index);
  static void BytesIndexError(Proc* proc, BytesVal* b, szl_int index);
  static void StringIndexError(Proc* proc, StringVal* s, szl_int char_index);
};

}  // namespace sawzall
