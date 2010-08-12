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

#include <string>

namespace sawzall {

class Engine {
 public:
  // Execute the program starting with the current proc state
  static Proc::Status Execute(Proc* proc, int max_steps, int* num_steps);
  // Allow specifying a non-standard base pointer, where standard bp == fp.
  static Proc::Status Execute(Proc* proc, int max_steps, int* num_steps,
                              Frame* bp);
  static Val** Print(Fmt::State* f, char* fmt, size_t nfmt, Proc* proc, Val** sp);
  static char* DoSlice(Proc* proc, Val* v, szl_int beg, szl_int end, Val* x);

  // --------------------------------------------------------------------------
  // Execution stack manipulation
  //
  // CAUTION: The performance of these functions is crucial! We rely
  // on the fact that they get inlined away - do not make any changes
  // w/o fully understanding the performance implications.
  //
  // In particular, passing sp as Val**& (instead of Val***) increases
  // instruction execution performance by ~20% for fibo.szl.
  // (If the Val*** parameter type is used instead,
  // at the call site we need to take the address of the sp: &sp. Even
  // though the functions are inlined, the compiler is forced to allocate
  // pc in memory instead of a register).

  static Frame* base(Frame* fp, int delta) {
    assert(delta >= 0);
    while (delta-- > 0) fp = fp->static_link();
    return fp;
  }

  static void push(Val**& sp, Val* x) {
    *--sp = x;
  }

  // Use this if you actually have a bool value
  static void push_szl_bool(Val**& sp, Proc* proc, bool x) {
    push(sp, Factory::NewBool(proc, x));
  }

  // Use this if you actually have a 64-bit value
  static void push_szl_int(Val**& sp, Proc* proc, szl_int x) {
    push(sp, Factory::NewInt(proc, x));
  }

  // Use this if you actually have a double
  static void push_szl_float(Val**& sp, Proc* proc, szl_float x) {
    push(sp, Factory::NewFloat(proc, x));
  }

  static void push_szl_uint(Val**& sp, Proc* proc, szl_uint x) {
    push(sp, Factory::NewUInt(proc, x));
  }

  static Frame* push_frame(Val**& sp, Frame* fp, Frame* bp, Instr* pc) {
    // Frame::Initialize stores data at this[-1],
    // so adjust sp accordingly after the frame is created.
    return (reinterpret_cast<Frame*&>(sp)--)->Initialize(fp, bp, pc);
  }

  static Val* pop(Val**& sp) {
    return *sp++;
  }

  // Convenience routines: the pop_szl_* functions return regular
  // values, like ints and floats.

  static uint64 pop_szl_bits(Val**& sp) {
    Val* v = pop(sp);
    uint64 b =  v->basic64();
    v->dec_ref();
    return b;
  }

  static bool pop_szl_bool(Val**& sp) {
    Val* v = pop(sp);
    bool b = v->as_bool()->val();
    v->dec_ref();
    return b;
  }

  static szl_fingerprint pop_szl_fingerprint(Val**& sp) {
    Val* v = pop(sp);
    szl_fingerprint fpr = v->as_fingerprint()->val();
    v->dec_ref();
    return fpr;
  }

  static szl_float pop_szl_float(Val**& sp) {
    Val* v = pop(sp);
    szl_float f = v->as_float()->val();
    v->dec_ref();
    return f;
  }

  static szl_uint pop_szl_uint(Val**& sp) {
    Val* v = pop(sp);
    szl_uint u = v->as_uint()->val();
    v->dec_ref();
    return u;
  }

  static szl_int pop_szl_int(Val**& sp) {
    Val* v = pop(sp);
    szl_int i = v->as_int()->val();
    v->dec_ref();
    return i;
  }

  static int32 pop_int32(Val**& sp) {
    Val* v = pop(sp);
    szl_int i = v->as_int()->val();
    assert(static_cast<int32>(i) == i);
    v->dec_ref();
    return i;
  }

  static szl_time pop_szl_time(Val**& sp) {
    Val* v = pop(sp);
    szl_time t = v->as_time()->val();
    v->dec_ref();
    return t;
  }

  // Convenience routines: the pop_* functions return Val*
  // narrowed to the appropriate type.

  static ArrayVal* pop_array(Val**& sp) {
    return pop(sp)->as_array();
  }

  static BytesVal* pop_bytes(Val**& sp) {
    return pop(sp)->as_bytes();
  }

  static StringVal* pop_string(Val**& sp) {
    return pop(sp)->as_string();
  }

  static MapVal* pop_map(Val**& sp) {
    return pop(sp)->as_map();
  }

  static TupleVal* pop_tuple(Val**& sp) {
    return pop(sp)->as_tuple();
  }

  static IndexableVal* pop_indexable(Val**& sp) {
    return pop(sp)->as_indexable();
  }

  static Frame* pop_frame(Val**& sp, Frame* fp, Instr*& pc, int locals) {
    assert(sp == fp->stack());  // expression stack must be empty
    pc = fp->return_pc();
    // pop parameters (need loop to decref)
    assert(locals >= 0);
    sp = &fp->at(0);  // top-most parameter
    Val** sp0 = &fp->at(locals);  // top-most expression of caller
    assert(sp <= sp0);
    while (sp < sp0)
      pop(sp)->dec_ref();
    // done
    assert(sp == sp0);
    return fp->dynamic_link();
  }

  static string pop_cpp_string(Proc* proc, Val**& sp) {
    StringVal* str = pop(sp)->as_string();
    // Note that we are relying on the named return value optimization here
    // to avoid an extra copy of the result value.  We assume that the
    // initialization of "result" below will occur directly in the caller's
    // result variable, giving us the opportunity to decrement the ref count
    // after the call to cpp_str() without the extra copy implied by this
    // ordering.
    string result = str->cpp_str(proc);
    str->dec_ref();
    return result;
  }

  static char* pop_c_str(Proc* proc, Val**& sp, char* buf, int nbuf) {
    StringVal* str = pop(sp)->as_string();
    str->c_str(buf, nbuf);
    str->dec_ref();
    return buf;
  }
};

}  // namespace sawzall
