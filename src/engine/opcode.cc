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
#include <unistd.h>
#include <time.h>
#include <assert.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/convop.h"
#include "engine/map.h"
#include "engine/code.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/frame.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"


namespace sawzall {

// ----------------------------------------------------------------------------
// Support for Opcode

// operand format:
//   letter: operand type
//     b: 32bit branch offset
//     c: 8bit int
//     g: 8bit conversion op
//     h: 16bit int
//     i: 32bit int
//     o: 16bit field offset
//     p: 32bit ptr (void*)
//     s: 32bit ptr to c string
//     t: 32bit Type* pointer
//     v: 16bit variable reference
//     V: 32bit Val* pointer

#define F(x) x, #x
static struct {
  Opcode op;
  const char* name;
  const char* fmt;
  int stack_delta;  // >= 0 for loads/pushes, < 0 for stores/pops
} instr_table[] = {
  { F(nop), "", 0 },
  { comment, "--", "s", 0 },
  { F(debug_ref), "", 0 },
#ifndef NDEBUG
  { F(verify_sp), "i", 0 },
#endif  // NDEBUG
  { F(loadV), "v", 1 },
  { F(loadVu), "v", 1 },
  { F(loadVi), "", 0 },
  { F(floadV), "o", 0 },
  { F(floadVu), "o", 0 },
  { F(xload8), "", -1 },
  { F(xloadR), "", -1 },
  { F(xloadV), "", -1 },
  { F(xloadVu), "", -1 },
  { F(mloadV), "", 0 },
  { F(mindexV), "", -1 },
  { F(mindexVu), "", -1 },
  { F(sload8), "", -2 },
  { F(sloadR), "", -2 },
  { F(sloadV), "", -2 },
  { F(storeV), "v", -1 },
  { F(storeVi), "", -2 },
  { F(undefine), "v", 0 },
  { F(openO), "vh", -1},
  { F(fstoreV), "o", -2 },
  { F(fclearB), "i", -1 },
  { F(fsetB), "i", 0 },
  { F(ftestB), "i", 0 },
  { F(xstore8), "", -3 },
  { F(xstoreR), "", -3 },
  { F(xstoreV), "", -3 },
  { F(minsertV), "", 0 },
  { F(mstoreV), "", -3 },
  { F(sstoreV), "", -4 },
  { F(inc64), "vc", 0 },
  { F(finc64), "oc", -1 },
  { F(xinc8), "c", -2 },
  { F(xincR), "c", -2 },
  { F(xinc64), "c", -2 },
  { F(minc64), "c", -2 },
  { F(push8), "c", 1 },
  { F(pushV), "V", 1 },
  { F(createB), "i", 1 },
  { F(newB), "i", -1 },
  { F(createStr), "i", 1 },
  { F(newStr), "", -1 },
  { F(createT), "t", 1 },
  { F(initT), "ii", 0 },
  { F(createA), "it", 1 },
  { F(initA), "ii", 0 },
  { F(newA), "t", -1 },
  { F(createM), "it", 1 },
  { F(initM), "i", 0 },
  { F(newM), "t", 0 },
  { F(createC), "bct", 0 },  // stack adjusted explicitly using StackMark
  { F(dupV), "", 1 },
  { F(popV), "", -1 },
  { F(and_bool), "", -1 },
  { F(or_bool), "", -1 },
  { F(add_int), "", -1 },
  { F(sub_int), "", -1 },
  { F(mul_int), "", -1 },
  { F(div_int), "", -1 },
  { F(mod_int), "", -1 },
  { F(add_uint), "", -1 },
  { F(sub_uint), "", -1 },
  { F(mul_uint), "", -1 },
  { F(div_uint), "", -1 },
  { F(mod_uint), "", -1 },
  { F(add_float), "", -1 },
  { F(sub_float), "", -1 },
  { F(mul_float), "", -1 },
  { F(div_float), "", -1 },
  { F(add_fpr), "", -1 },
  { F(add_array), "", -1 },
  { F(add_bytes), "", -1 },
  { F(add_string), "", -1 },
  { F(add_time), "", -1 },
  { F(sub_time), "", -1 },
  { F(shl_int), "", -1 },
  { F(shr_int), "", -1 },
  { F(and_int), "", -1 },
  { F(or_int), "", -1 },
  { F(xor_int), "", -1 },
  { F(shl_uint), "", -1 },
  { F(shr_uint), "", -1 },
  { F(and_uint), "", -1 },
  { F(or_uint), "", -1 },
  { F(xor_uint), "", -1 },
  { F(set_cc), "", -1 },
  { F(get_cc), "", 1 },
  { F(cmp_begin), "", 0 },
  { F(eql_bits), "", -2 },
  { F(neq_bits), "", -2 },
  { F(lss_bits), "", -2 },
  { F(leq_bits), "", -2 },
  { F(gtr_bits), "", -2 },
  { F(geq_bits), "", -2 },
  { F(eql_float), "", -2 },
  { F(neq_float), "", -2 },
  { F(lss_float), "", -2 },
  { F(leq_float), "", -2 },
  { F(gtr_float), "", -2 },
  { F(geq_float), "", -2 },
  { F(lss_int), "", -2 },
  { F(leq_int), "", -2 },
  { F(gtr_int), "", -2 },
  { F(geq_int), "", -2 },
  { F(eql_string), "", -2 },
  { F(neq_string), "", -2 },
  { F(lss_string), "", -2 },
  { F(leq_string), "", -2 },
  { F(gtr_string), "", -2 },
  { F(geq_string), "", -2 },
  { F(eql_bytes), "", -2 },
  { F(neq_bytes), "", -2 },
  { F(lss_bytes), "", -2 },
  { F(leq_bytes), "", -2 },
  { F(gtr_bytes), "", -2 },
  { F(geq_bytes), "", -2 },
  { F(eql_array), "", -2 },
  { F(neq_array), "", -2 },
  { F(eql_map), "", -2 },
  { F(neq_map), "", -2 },
  { F(eql_tuple), "", -2 },
  { F(neq_tuple), "", -2 },
  { F(eql_closure), "", -2 },
  { F(neq_closure), "", -2 },
  { F(cmp_end), "", 0 },
  { F(basicconv), "g", 0 },  // stack adjusted explicitly using StackMark
  { F(arrayconv), "g", 0 },  // stack adjusted explicitly using StackMark
  { F(mapconv), "tgg", 0 },  // stack adjusted explicitly using StackMark
  { F(branch), "b", 0 },
  { F(branch_true), "b", 0 },
  { F(branch_false), "b", 0 },
  { F(trap_false), "s", 0 },
  { F(enter), "ii", 0 },
  { F(set_bp), "c", 0 },
  { F(callc), "p", 0 },  // stack adjusted explicitly using StackMark
  { F(callcnf), "p", 0 },  // stack adjusted explicitly using StackMark
  { F(call), "", -1 },
  { F(calli), "b", 0 },
  { F(match), "p", 0 },  // stack adjusted explicitly using StackMark
  { F(matchposns), "p", 0 },  // stack adjusted explicitly using StackMark
  { F(matchstrs), "p", 0 },  // stack adjusted explicitly using StackMark
  { F(saw), "cp", 0 },  // stack adjusted explicitly using StackMark
  { F(ret), "h", 0 },
  { F(retV), "h", -1 },
  { F(retU), "", 0 },
  { F(terminate), "", 0 },
  { F(stop), "s", 0 },
  { F(emit), "", 0 },  // stack adjusted explicitly using StackMark
  { F(fd_print), "", 1 },  // int result
  { F(count),  "i", 0 },
  { F(illegal) , "", 0 }  // illegal must be the last entry
};
#undef F


bool SetsCC(Opcode op) {
  return op == set_cc || (cmp_begin < op && op < cmp_end);
}


bool UsesCC(Opcode op) {
  return (op == get_cc || op == branch_true ||
         op == branch_false || op == trap_false);
}


static int index(Opcode op) {
  int i = 0;
  while (instr_table[i].op != illegal && instr_table[i].op != op)
    i++;
  // returns index for illegal if opcode not found
  return i;
}


int StackDelta(Opcode op) {
  // TODO: This may be somewhat problematic because
  // we call this function during code generation for each
  // emit, and the the call of index() iterates through half
  // of all the instructions on average thus creating a O(n^2)
  // behaviour. The constant is very low so at this point
  // it probably doesn't matter, but we should probably
  // fix this at some point.
  return instr_table[index(op)].stack_delta;
}


const char* Opcode2String(Opcode op) {
  return instr_table[index(op)].name;
}


int InstrFmt(Fmt::State* f) {
  // get instr
  Instr** instrp = FMT_ARG(f, Instr**);
  Instr* instr = *instrp;

  // check for error case
  if (instr == NULL)
    return F.fmtprint(f, "<nil>");

  // lookup name & format
  int i = index((Opcode)*instr++);  // do the lookup only once
  const char* name = instr_table[i].name;
  const char* fmt = instr_table[i].fmt;

  // print instruction
  F.fmtprint(f, name);

  // print operands
  const char* sep = "\t";
  while (*fmt != '\0') {
    // print operand separator
    F.fmtprint(f, "%s", sep);
    sep = ", ";

    // print operand
    switch (*fmt++) {
      case 'b': {
          int offs = Code::pcoff_at(instr);
          F.fmtprint(f, "0x%p (= 0x%p + %d)",
                     (unsigned char*)instr + offs, instr, offs);
        }
        break;
      case 'c':
        F.fmtprint(f, "%d", Code::int8_at(instr));
        break;
      case 'g': {
          ConversionOp op = static_cast<ConversionOp>(Code::int8_at(instr));
          F.fmtprint(f, "%s", ConversionOp2String(op));
          if (op == bytes2proto || op == proto2bytes) {
            F.fmtprint(f, ", proto(0x%p)", Code::ptr_at(instr));
          } else if (op == typecast) {
            Type* t = reinterpret_cast<Type*>(Code::ptr_at(instr));
            F.fmtprint(f, ", %T(0x%p)", t, t);
          } else if (op == tuple2tuple) {
            F.fmtprint(f, ", tuple(0x%p)", Code::ptr_at(instr));
          }
        }
        break;
      case 'h':
        F.fmtprint(f, "%d", Code::int16_at(instr));
        break;
      case 'i':
        F.fmtprint(f, "%ld", Code::int32_at(instr));
        break;
      case 'o':
        F.fmtprint(f, "field@%d", Code::int16_at(instr));
        break;
      case 'p':
        F.fmtprint(f, "0x%p", Code::ptr_at(instr));
        break;
      case 's':
        F.fmtprint(f, "%q", static_cast<char*>(Code::ptr_at(instr)));
        break;
      case 't':
        F.fmtprint(f, "%T", static_cast<Type*>(Code::ptr_at(instr)));
        break;
      case 'v':
        { int index = Code::int16_at(instr);
          if (index == NO_INDEX)
            F.fmtprint(f, "no index");
          else
            F.fmtprint(f, "bp[%d]", index);
        }
        break;
      case 'V':
        // no proc, but we don't need one here
        F.fmtprint(f, "%V", NULL, Code::val_at(instr));
        break;
      default:
        F.fmtprint(f, "??%c??", fmt[-1]);
    }
  }

  // advance instrp
  *instrp = instr;
  return 0;
}

}  // namespace sawzall
