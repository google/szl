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

// This file defines all engine opcodes, and some of the opcode fields.

// The opcodes implemented by the Sawzall interpreter

// Note:
// - "... x -> ... y" comments describe what was on
//   the top of stack before and after the instruction execution.
// - "u" in <instr>u stands for unique - the "u" versions clone the result
//   value if its reference count is greater than one, and use the cloned
//   value (which now has a reference count of one). This is necessary when
//   the result will be modified because other variables should continue to
//   refer to the unmodified value.

enum Opcode {
  illegal,

  // DEBUG
  nop,  // no-op, should never be executed, used for alignment only
  comment,  // puts char* in instruction stream; ignored
  debug_ref,  // ... array -> ... refcount
#ifndef NDEBUG
  verify_sp,  // offset: int32 (verifies sp position - fatal error if mismatch)
#endif  // NDEBUG

  // variable loads
  loadV,   // var_index: int16,  ... -> ... bp[var_index]
  loadVu,  // var_index: int16,  ... -> ... bp[var_index]
  loadVi,  // ... var_index -> ... bp[var_index]

  // fields
  // ... t -> ... t.data()[slot_index]
  // slot_index: int16
  floadV,
  floadVu,

  // indexed loads
  // ... i a -> ... a[i]
  xload8,
  xloadR,
  xloadV,
  xloadVu,

  // mapped loads
  // two instructions. mload* puts index on stack; mindex* loads
  // value with that index in map. there is no mloadVu because
  // we use loadSu in that case to push the map on the stack.
  mloadV,    // ... key m -> i m
  mindexV,   // ... i m -> m[[i]]
  mindexVu,  // ... i m -> ... m[[i]]

  // sliced loads
  // ... j i a -> ... a[i : j]
  sload8,
  sloadR,
  sloadV,
  sloadVu,  // do we need this?

  // variable stores
  storeV,   // var_index: int16, ... x -> ... (side effect: bp[var_index] = x)
  storeVi,  // ... x var_index -> ... (side effect: bp[var_index] = x)

  // undefine variables
  // ... -> ... (side effect: bp[var_index] = NULL)
  // var_index: int16
  undefine,

  // open output descriptor
  openO,    // var_index: int16, outputter table index: int16
            // ... param -> ...
            // (side effect: bp[var_index] = index)

  // field stores
  // ... v t -> ... (side effect: t.data()[slot_index] = v)
  // slot_index: int16
  fstoreV,

  // proto field support
  // clear inproto bit
  // ... t -> ... (side effect: t.clear_slot_bit_at(index))
  fclearB,  // index: in32t
  // set inproto bit
  // ... t -> ... t (side effect: t.set_slot_bit_at(index))
  fsetB,   // index: in32t
  // test inproto bit
  // ... t -> ... cc (side effect: cc = t.slot_bit_at(index))
  ftestB,  // index: int32

  // indexed stores
  // ... v i a -> ... (side effect: a[i] = v)
  xstore8,
  xstoreR,
  xstoreV,

  // mapped stores
  // two instructions. minsert* puts index on stack; mstore* stores
  // value at that index in map.
  // minsert: ... key m -> i m
  // mstore: ... v i m -> ... (side effect: m[[i]] = v)
  minsertV,
  mstoreV,

  // sliced stores
  // ... v j i a -> ... (side effect: a[i : j] = v)
  sstoreV,

  // inc
  // delta: int8
  inc64,   // var_index: int16, ... -> ... (side effect: bp[var_index] ++ or --)
  finc64,  // slot_index: int16, ... t -> ... t.data()[slot_index]++ or --
  xinc8,
  xincR,
  xinc64,  // ... i a -> ... (side effect: a[i]++ or --)
  minc64,  // ... i m -> ... (side effect: m[i]++ or --)

  // basic type literals
  // ... -> ... x
  push8,  // x: int8
  pushV,  // x: Val*

  // byte literals
  createB,    // n: int32
  newB,

  // string literals
  createStr,  // n: int32
  newStr,

  // tuple literals
  // tuples are created empty and initialized in pieces to
  // bound the required stack size
  // ... -> ... TupleDesc
  createT,   // t: TupleType*
  // ... TupleDesc fieldn-1, ... field1, field0 -> ... TupleDesc
  initT,     // f: int32, n: int32

  // array creation:
  // arrays are created empty and initialized in pieces to
  // bound the required stack size.
  // array literals
  // ... -> ... ArrayDesc
  createA,   // n: int32, t: ArrayType*
  // ... ArrayDesc elem[n-1]] ... elem[1] elem[0] -> ... ArrayDesc
  initA,     // f: int32, n: int32
  // array allocation
  // ... init length -> ... ArrayDesc
  newA,

  // map creation, map literals
  // maps are created empty and initialized in pieces to bound
  // the required stack size.
  // ... -> ... MapDesc
  createM,   // n: int32, t: MapType*
  // ... MapDesc, valuen-1, keyn-1, ... value0, key0 -> ... MapDesc
  initM,     // n: int32
  // map allocation
  // ... -> ... MapDesc
  newM,      // t: Type*

  // closures
  // ... -> ... closure
  createC,  // e: int32, n: int8

  // dup/pop
  dupV,
  popV,

  // arithmetics
  and_bool,  // ... x y -> ... x & y
  or_bool,   // ... x y -> ... x | y

  add_int,   // ... x y -> ... x + y
  sub_int,   // ... x y -> ... x - y
  mul_int,   // ... x y -> ... x * y
  div_int,   // ... x y -> ... x / y
  mod_int,   // ... x y -> ... x % y
  shl_int,   // ... x y -> ... x << y
  shr_int,   // ... x y -> ... x >> y
  and_int,   // ... x y -> ... x & y
  or_int,    // ... x y -> ... x | y
  xor_int,   // ... x y -> ... x ^ y

  add_uint,  // ... x y -> ... x + y
  sub_uint,  // ... x y -> ... x - y
  mul_uint,  // ... x y -> ... x * y
  div_uint,  // ... x y -> ... x / y
  mod_uint,  // ... x y -> ... x % y
  shl_uint,  // ... x y -> ... x << y
  shr_uint,  // ... x y -> ... x >> y
  and_uint,  // ... x y -> ... x & y
  or_uint,   // ... x y -> ... x | y
  xor_uint,  // ... x y -> ... x ^ y

  add_float,  // ... x y -> ... x + y
  sub_float,  // ... x y -> ... x - y
  mul_float,  // ... x y -> ... x * y
  div_float,  // ... x y -> ... x / y

  add_fpr,    // ... x y -> ... xy
  add_array,  // ... x y -> ... xy
  add_bytes,  // ... x y -> ... xy
  add_string,  // ... x y -> ... xy

  add_time,  // ... x y -> ... x + y
  sub_time,  // ... x y -> ... x - y

  // condition codes
  set_cc,  // ... b -> ... (side effect: cc = b)
  get_cc,  // ... -> ... b (uses cc)

  // comparisons
  cmp_begin,  // not a legal instruction - begin of cmp instructions
  eql_bits,   // ... x y -> ... (side effect: cc = (x == y))
  neq_bits,   // ... x y -> ... (side effect: cc = (x != y))
  lss_bits,   // ... x y -> ... (side effect: cc = (x < y))
  leq_bits,   // ... x y -> ... (side effect: cc = (x <= y))
  gtr_bits,   // ... x y -> ... (side effect: cc = (x > y))
  geq_bits,   // ... x y -> ... (side effect: cc = (x >= y))
  eql_float,  // ... x y -> ... (side effect: cc = (x == y))
  neq_float,  // ... x y -> ... (side effect: cc = (x != y))
  lss_float,  // ... x y -> ... (side effect: cc = (x < y))
  leq_float,  // ... x y -> ... (side effect: cc = (x <= y))
  gtr_float,  // ... x y -> ... (side effect: cc = (x > y))
  geq_float,  // ... x y -> ... (side effect: cc = (x >= y))
  lss_int,    // ... x y -> ... (side effect: cc = (x < y))
  leq_int,    // ... x y -> ... (side effect: cc = (x <= y))
  gtr_int,    // ... x y -> ... (side effect: cc = (x > y))
  geq_int,    // ... x y -> ... (side effect: cc = (x >= y))
  eql_string,  // ... x y -> ... (side effect: cc = (x == y))
  neq_string,  // ... x y -> ... (side effect: cc = (x != y))
  lss_string,  // ... x y -> ... (side effect: cc = (x < y))
  leq_string,  // ... x y -> ... (side effect: cc = (x <= y))
  gtr_string,  // ... x y -> ... (side effect: cc = (x > y))
  geq_string,  // ... x y -> ... (side effect: cc = (x >= y))
  eql_bytes,   // ... x y -> ... (side effect: cc = (x == y))
  neq_bytes,   // ... x y -> ... (side effect: cc = (x != y))
  lss_bytes,   // ... x y -> ... (side effect: cc = (x < y))
  leq_bytes,   // ... x y -> ... (side effect: cc = (x <= y))
  gtr_bytes,   // ... x y -> ... (side effect: cc = (x > y))
  geq_bytes,   // ... x y -> ... (side effect: cc = (x >= y))
  eql_array,   // ... x y -> ... (side effect: cc = (x == y))
  neq_array,   // ... x y -> ... (side effect: cc = (x != y))
  eql_map,     // ... x y -> ... (side effect: cc = (x == y))
  neq_map,     // ... x y -> ... (side effect: cc = (x != y))
  eql_tuple,   // ... x y -> ... (side effect: cc = (x == y))
  neq_tuple,   // ... x y -> ... (side effect: cc = (x != y))
  eql_closure,  // ... x y -> ... (side effect: cc = (x == y))
  neq_closure,  // ... x y -> ... (side effect: cc = (x != y))
  cmp_end,     // not a legal instruction - end of cmp instructions

  // conversions
  // (if op == typecast, bytes2proto, proto2bytes, or tuple2tuple,
  // the instruction is also followed by a 32bit tuple ptr)
  basicconv,  // op: int8
              // indicates that the conversion is applied to a basic type
  arrayconv,  // op: int8
              // indicates that the conversion following is applied to an array
  mapconv,    // map_type: Type*,
              // key_op: in8, value_op: in8; indicates that the conversion ops
              // that follow define how to convert an array to a map

  // control structures
  // pc offset: int32
  branch,
  branch_true,   // ... -> ... (uses cc)
  branch_false,  // ... -> ... (uses cc)
  // msg: const char*
  trap_false,    // ... -> ... (uses cc)

  // calls
  enter,    // n: int32, m: int32
            // ... -> ... n_local_var_slots  Frame  m_expr_slots
  set_bp,   // n: int8, bp = base(fp, n)
  callc,    // c func: int32
            // ... arg[n-1] .. arg[1] arg[0] -> ... possibly result
  callcnf,  // c func: int32
            // ... arg[n-1] .. arg[1] arg[0] -> ... possibly result
            // cannot return an error
  call,     // ... arg[n-1] .. arg[1] arg[0] closure -> ... possibly result
  calli,    // offset: int32
            //  ... arg[n-1] .. arg[1] arg[0] -> ... possibly result

  // regular expressions
  match,       // pattern: void**
  matchposns,  // pattern: void**
  matchstrs,   // pattern: void**
  saw,  // n: int8, cache: void**
        // ... array of string, string, re*n, skip*n, count ->
        //        ... array of string, string

  // returns
  // n: int16 - number of slots to pop
  // ... params  local_vars  Frame -> ...
  ret,
  retV,
  retU,
  terminate,  // no n paramter
  stop,       // msg: char*

  // emit
  // ... v in-1 ... i1 i0 var_index -> ...
  // if 'file' or 'proc' is present, indices become string:
  //   ... v filename -> ...
  // if 'format' is present, value becomes string:
  //   ... string in-1 ... i1 i0 ->  ...
  // if 'both' are present:
  //   ... string filename -> ...
  emit,

  // printing
  fd_print,  // ... argn ... arg2 arg1 fmt fd -> ... int

  // profiling counter
  count,    // ... arg: int -> ..., and increments proc_.counters[arg]

  // the total number of opcodes - must be the last value in the enum
  number_of_opcodes
};


// special trap index (set_trap operand)
enum {
  NO_INDEX = 0  // all legal variable indices are > 0
};


// for printing
const char* Opcode2String(Opcode op);
int InstrFmt(Fmt::State* f);


// Conversions
enum ConversionOp {
  // illegal conversion
  noconv,

  // basic -> basic (no value change)
  typecast,

  // basic -> bool
  str2bool,    // ... string -> ... bool

  // basic -> bytes
  fpr2bytes,   // ... fingerprint -> ... bytes
  str2bytes,   // ... encoding string -> ... bytes
  int2bytes,   // ... int -> ... bytes
  uint2bytes,  // ... uint -> ... bytes

  // basic -> fingerprint
  str2fpr,     // ... base string -> ... fingerprint
  uint2fpr,    // ... uint -> fingerprint

  // compound types -> fingerprint
  bytes2fpr,   // ... bytes -> ... fingerprint

  // basic -> float
  int2float,   // ... int -> ... float
  str2float,   // ... string -> ... float
  uint2float,  // ... int -> ... float

  // basic -> int
  float2int,   // ... float -> ... int
  str2int,     // ... base string -> ... int
  uint2int,    // ... uint -> ... int
  bytes2int,   // ... bytes -> ... int

  // basic -> string
  bool2str,    // ... bool -> ... string
  bytes2str,   // ... encoding bytes -> ... string
  float2str,   // ... float -> ... string
  int2str,     // ... base int -> ... string
  time2str,    // ... timezone time -> ... string
  uint2str,    // ... base uint -> ... string
  fpr2str,     //  ... fingerprint -> ... string

  // compound types -> string
  array2str,     // ... any array -> ... string
  map2str,       //  ... any map -> ... string
  tuple2str,     // ... any tuple -> ... string
  function2str,  // ... any closure -> ... string

  // string -> compound types
  str2array,   // ... array of int -> ... string (as unicode characters)

  // basic -> time
  str2time,    // ... timezone string -> ... time
  uint2time,   // ... uint -> ... time

  // basic -> uint
  float2uint,  // ... float -> ... uint
  bits2uint,   // ... bits -> ... uint
  str2uint,    // ... string -> ... uint
  bytes2uint,  // ... bytes -> ... uint

  // bytes (protocol buffer) -> proto, and vice versa
  bytes2proto,  // type: ptr; ... bytes (proto buffer encoded) -> ... tuple
  proto2bytes,  // type: ptr; ... tuple -> ... bytes (proto buffer encoded)

  // tuple -> tuple, with type change
  tuple2tuple,  // type: ptr; ... tuple -> ... tuple
};


// for code generation
bool SetsCC(Opcode op);
bool UsesCC(Opcode op);
int StackDelta(Opcode op);


}  // namespace sawzall
