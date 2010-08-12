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
#include <algorithm>

#include "engine/globals.h"
#include "public/logging.h"
#include "public/hashutils.h"
#include "public/varint.h"

#include "utilities/strutils.h"
#include "utilities/timeutils.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/code.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/frame.h"
#include "engine/proc.h"
#include "engine/convop.h"
#include "engine/intrinsic.h"
#include "public/emitterinterface.h"
#include "public/sawzall.h"
#include "engine/outputter.h"
#include "engine/tracer.h"
#include "engine/codegen.h"
#include "engine/compiler.h"
#include "engine/histogram.h"
#include "engine/linecount.h"
#include "engine/profile.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/map.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/gctrigger.h"
#include "engine/engine.h"


namespace sawzall {

// Helpers for variable access

static inline int var_index(Instr*& pc) {
  return Code::int16_at(pc);  // advances pc
}


static inline Val* uniq(Proc* proc, Val*& var) {
  return (var = var->Uniq(proc));
}



#define TEST_INDEX(type, value, print_index, test_index, optional_dec_ref) \
  if (!(value)->legal_index(test_index)) { \
    proc->trap_info_ = proc->PrintError( \
                     "index out of bounds (index = %lld, " \
                     type " length = %d)", \
                     (print_index), (value)->semantic_length()); \
    optional_dec_ref; \
    goto trap_handler; \
  }

#define TEST_ARRAY_INDEX(value, index) \
  TEST_INDEX("array", value, index, index, ;)
#define TEST_BYTES_INDEX(value, index) \
  TEST_INDEX("bytes", value, index, index, ;)
#define TEST_STRING_INDEX(value, char_index, byte_index) \
  TEST_INDEX("string", value, char_index, byte_index, ;)

#define TEST_ARRAY_INDEX_DEC_REF(value, index) \
  TEST_INDEX("array", value, index, index, (value)->dec_ref())
#define TEST_BYTES_INDEX_DEC_REF(value, index) \
  TEST_INDEX("bytes", value, index, index, (value)->dec_ref())
#define TEST_STRING_INDEX_DEC_REF(value, char_index, byte_index) \
  TEST_INDEX("string", value, char_index, byte_index, (value)->dec_ref())


// Helpers for calls/returns

// RESTORE_STATE restores the current execution state
// in Engine::Execute (see also comment for SAVE_STATE)
// (this macro is used only once but the code is factored
// out so we can see the symmetry with SAVE_STATE).
#define RESTORE_STATE \
  do { \
    /* restore proc state */ \
    fp = proc->state_.fp_; \
    sp = proc->state_.sp_; \
    lp = proc->limit_sp(); \
    pc = proc->state_.pc_; \
    cc = proc->state_.cc_; \
    /* bp passed in by invoker of Execute, and may be set to fp or gp */ \
    /* setup return_pc */ \
    return_pc = NULL; \
    /* start profiling if enabled */ \
    if (proc->profile() != NULL) \
      proc->profile()->Start(); \
  } while (false)


// SAVE_STATE saves the current execution state in the Proc.
#define SAVE_STATE(status, steps_correction) \
  do { \
    /* stop profiling if enabled */ \
    if (proc->profile() != NULL) \
      proc->profile()->Stop(); \
    /* we don't want to save/restore bp and return_pc, though*/ \
    /* in the TRAPPED/TERMINATED/FAILED case we don't care */ \
    assert((bp == fp && return_pc == NULL) || (status) >= Proc::TRAPPED); \
    /* save proc state */ \
    proc->state_.fp_ = fp; \
    proc->state_.sp_ = sp; \
    /* lp is read-only by Execute => no need to save it */ \
    proc->state_.pc_ = pc; \
    proc->state_.cc_ = cc; \
    /* correct actual number of instructions executed */ \
    *num_steps += steps_correction; \
    assert(*num_steps > 0); \
  } while (false)


// String comparison helpers

static int cmp_string(StringVal* x, StringVal* y) {
  const int lx = x->length();
  const int ly = y->length();
  const int cmp = memcmp(x->base(), y->base(),  min(lx, ly));
  if (cmp != 0)
    return cmp;
  else
    return lx - ly;
}


static bool eq_string(StringVal* x, StringVal* y) {
  const int lx = x->length();
  const int ly = y->length();
  return (lx == ly) && (memcmp(x->base(), y->base(), lx) == 0);
}


// Bytes comparison helpers

static int cmp_bytes(BytesVal* x, BytesVal* y) {
  const int lx = x->length();
  const int ly = y->length();
  const int cmp = memcmp(x->base(), y->base(), min(lx, ly));
  if (cmp != 0)
    return cmp;
  return lx - ly;
}


static bool eq_bytes(BytesVal* x, BytesVal* y) {
  const int lx = x->length();
  const int ly = y->length();
  return (lx == ly) &&  (memcmp(x->base(), y->base(), lx) == 0);
}


char* Engine::DoSlice(Proc* proc, Val* v, szl_int beg, szl_int end, Val* x) {
  // Val.intersect_slice() doesn't tell us enough for assignment; check things
  // on the lhs ourselves.  Also, we don't want to clamp the values; for
  // assignment things should be in range.
  if (beg < 0)
    return proc->PrintError("assignment to slice: beginning index %lld < 0",
                            beg);
  int length = v->as_indexable()->length();
  if (end > length)
    return proc->PrintError("assignment to slice: ending index %lld > "
                            "length of array (%lld)", end, length);
  if (beg > end)
    return proc->PrintError("assignment to slice: starting index %lld < "
                            "ending index %lld", beg, end);
  // Three cases: bytes, strings, and general arrays
  // beg and end have been checked and can be safely truncated to 32-bit
  if (v->is_bytes())
    v->as_bytes()->PutSlice(proc, beg, end, x->as_bytes());
  else if (v->is_string())
    v->as_string()->PutSlice(proc, beg, end, x->as_string());
  else
    v->as_array()->PutSlice(proc, beg, end, x->as_array());
  return NULL;
}


// Implementation of Engine

Proc::Status Engine::Execute(Proc* proc, int max_steps, int* num_steps) {
  // Normally, the base pointer (bp) should be initialized to the
  // frame pointer (fp).
  return Execute(proc, max_steps, num_steps, proc->state_.fp_);
}


Proc::Status Engine::Execute(Proc* proc, int max_steps, int* num_steps,
                             Frame* bp) {
  // CAUTION: Inside this routine the variables sp and pc are hot -
  // we go through some lengths to make sure the compiler can
  // allocate them into registers. In particular, we don't take the
  // address of sp or pc (&sp, &pc). Note that the stack accessors
  // push_X, pop_X take a Val**& stack pointer parameter, but they
  // will be inlined (see also the comment in engine.h). In other cases
  // we introduce temporary variables so we don't have to take the
  // address of sp or pc. Empirical evidence (e.g. making sp a member
  // variable of class Engine) indicates that storing sp in memory leads
  // to a performance degradation of 20% for pure instruction execution
  // (e.g. fibo.szl).

  // The following variables represent the interpreter execution state
  // which needs to be saved/restored when suspending/activating
  // the interpreter. For speed they are copied from proc into local
  // variables via the SAVE_STATE/RESTORE_STATE macros.
  Frame* fp;  // frame pointer
  Val** sp;  // stack pointer
  Val** lp;  // limit pointer
  Instr* pc;  // program counter
  bool cc;  // condition code

  // bp usually equals fp, except after a set_bp or a call instruction.
  // return_pc is only valid between a call and the corresponding
  // enter instruction. We don't want to save/restore these two
  // variables and therefore the outer interpreter loop cannot be
  // interrupted while bp != fp or return_pc != NULL. They are
  // setup by RESTORE_STATE to their canonical values.
  //
  // Note: When Execute is called the first time for an actual run,
  // the very first instruction will be an enter byte code: bp must
  // be the static link (as if set via a call) and return_pc must be
  // the proper return address. For the first frame, the static link
  // must be the global frame, which happens to be where fp points
  // to, and the return_pc must be NULL - both these criteria happen
  // to be the same as for intermediate calls so it all works out nicely.
  //
  // For the DoCall() case, bp might *not* be the same as fp (it could
  // be gp), so we allow the DoCall() caller to pass in bp explicitly.
  Instr* return_pc;

  RESTORE_STATE;

  // num_steps may be NULL - in that case set it to some dummy
  // variable so we can forget about it
  int dummy;
  if (num_steps == NULL)
    num_steps = &dummy;
  *num_steps = 0;  // no instructions executed so far

  // outer interpreter loop - if run w/o any flags
  // (tracing, profiling, etc.) this will iterate once for
  // many dozens of instructions, so performance here
  // is not so crucial)
  //
  // note: even if we have reached max_steps steps, as long
  // as bp != fp or return_pc != NULL we need to execute at
  // least one more iteration (until those conditions are satisfied)
  while (*num_steps < max_steps || bp != fp || return_pc != NULL) {
    // number of cycles before we pause execution
    int cycle_count = max_steps - *num_steps;

    // profiling support
    // (with --trace_code or --print_histogram enabled,
    // profiling may not make a lot of sense since a tick
    // is recorded for each instruction)
    if (proc->profile() != NULL) {
      // number of cycles before next tick determined by HandleTick
      cycle_count = min(cycle_count, proc->profile()->HandleTick(fp, sp, pc));
    }

    // tracing support
    if (FLAGS_trace_code) {
      Instr* tmp = pc;  // don't take address of pc - use tmp! (performance)
      F.print("%p: %p  %I\n", sp, pc, &tmp);
      cycle_count = 1;  // one cycle before next trace
    }

    // histogram support
    if (proc->histo() != NULL) {
      proc->histo()->Count(static_cast<Opcode>(*pc));
      cycle_count = 1;  // one cycle before next histo count
    }

    // hot inner interpreter loop - performance is crucial here!
    cycle_count = max(1, cycle_count);  // inner loop must run at least once
    *num_steps += cycle_count;  // account for time spent in inner loop
    // Note that the heap can adjust cycle_count to stop the loop early.
    GCTrigger gctrigger(proc->heap(), num_steps, &cycle_count);
    while (cycle_count-- > 0) {
      switch (*pc++) {
        // debugging
        case nop:
          // nop's should never be executed, they are used for alignment only
          ShouldNotReachHere();
          break;

        case comment:
          // ignore void* embedded data
          pc += sizeof(void*)/sizeof(Instr);
          continue;  // do not reset bp! (was bug)

        case debug_ref:
          { Val* v = pop(sp);
            // Compute what reference count will be after dec_ref().
            int32 count = v->ref() - (v->is_ptr() && !v->is_null());
            v->dec_ref();
            push_szl_int(sp, proc, count);
          }
          break;

  #ifndef NDEBUG
        case verify_sp:
          { int offs = Code::int32_at(pc);
            if (fp->stack() - sp != offs)
              // compiler bug => FatalError
              FatalError("sp misaligned (fp = %p, sp = %p, stack size = %d, expected = %d)\n",
                         fp, sp, fp->stack() - sp, offs);
          }
          break;
  #endif // NDEBUG

        // loads
        case loadV:
          { Val* v = bp->at(var_index(pc));
            if (v == NULL)
              goto trap_handler;  // variable undefined
            TRACE_REF("before loadV", v);
            v->inc_ref();
            push(sp, v);
          }
          break;

        case loadVu:
          { Val** vp = &bp->at(var_index(pc));
            if (*vp == NULL)
              goto trap_handler;  // variable undefined
            TRACE_REF("before loadVu", *vp);
            Val* v = uniq(proc, *vp);
            v->inc_ref();
            push(sp, v);
          }
          break;

        case loadVi:
          { int i = pop_szl_int(sp);
            Val* v = bp->at(i);
            if (v == NULL)
              goto trap_handler;  // variable undefined
            TRACE_REF("before loadVi", v);
            v->inc_ref();
            push(sp, v);
          }
          break;

        case floadV:
          { TupleVal* t = pop_tuple(sp);
            Val* v = t->slot_at(Code::int16_at(pc));
            v->inc_ref();
            t->dec_ref();
            push(sp, v);
          }
          break;

        case floadVu:
          { TupleVal* t = pop_tuple(sp);
            Val* v = uniq(proc, t->slot_at(Code::int16_at(pc)));
            v->inc_ref();
            t->dec_ref();
            push(sp, v);
          }
          break;

        case xload8:
          { szl_int i = pop_szl_int(sp);
            BytesVal* b = pop_bytes(sp);
            TEST_BYTES_INDEX_DEC_REF(b, i);
            push_szl_int(sp, proc, b->at(i));
            b->dec_ref();
          }
          break;

        case xloadR:
          { szl_int i0 = pop_szl_int(sp);
            StringVal* s = pop_string(sp);
            szl_int i = s->byte_offset(proc, i0);
            TEST_STRING_INDEX_DEC_REF(s, i0, i);
            push_szl_int(sp, proc, s->at(i));
            s->dec_ref();
          }
          break;

        case xloadV:
          { szl_int i = pop_szl_int(sp);
            ArrayVal* a = pop_array(sp);
            TEST_ARRAY_INDEX_DEC_REF(a, i);
            Val* v = a->at(i);
            v->inc_ref();
            a->dec_ref();
            push(sp, v);
          }
          break;

        case xloadVu:
          { szl_int i = pop_szl_int(sp);
            ArrayVal* a = pop_array(sp);
            TEST_ARRAY_INDEX_DEC_REF(a, i);
            Val* v = uniq(proc, a->at(i));
            v->inc_ref();
            a->dec_ref();
            push(sp, v);
          }
          break;

        case mloadV:
          { MapVal* m = pop_map(sp);
            Val* key = pop(sp);
            int64 index = m->map()->Lookup(key);
            key->dec_ref();
            if (index < 0) {
              m->dec_ref();
              proc->trap_info_ = "map key was not present";
              goto trap_handler;
            }
            push_szl_int(sp, proc, index);
            push(sp, m);
          }
          break;

      case mindexV:
          { MapVal* m = pop_map(sp);
            int32 index = pop_int32(sp);  // generated index, never out of range
            Val* value = m->map()->Fetch(index);
            value->inc_ref();
            m->dec_ref();
            push(sp, value);
          }
          break;

        case mindexVu:
          { MapVal* m = pop_map(sp);
            m->dec_ref();
            assert(m->is_unique());
            int32 index = pop_int32(sp);  // generated index, never out of range
            Val* value = m->map()->Fetch(index);
            // Uniq will drop its ref count, but it's still in the map, so that is
            // premature.  Therefore, if we do need to unique it, inc_ref it first.
            if (!value->is_unique()) {
              value->inc_ref();
              value = value->Uniq(proc);
              m->map()->SetValue(index, value);  // update array in map
            }
            value->inc_ref();
            push(sp, value);
          }
          break;

        case sload8:
          { szl_int end = pop_szl_int(sp);
            szl_int beg = pop_szl_int(sp);
            BytesVal* b = pop_bytes(sp);
            b->intersect_slice(&beg, &end, b->length());
            push(sp, SymbolTable::bytes_form()->NewSlice(proc, b, beg, end - beg));
            // ref counting managed in NewSlice()
          }
          break;

        case sloadR:
          { szl_int end = pop_szl_int(sp);
            szl_int beg = pop_szl_int(sp);
            StringVal* s = pop_string(sp);
            s->intersect_slice(&beg, &end, s->num_runes());  // note: num_runes()
            int num_runes = end - beg;
            beg = s->byte_offset(proc, beg);
            end = s->byte_offset(proc, end);
            push(sp, SymbolTable::string_form()->NewSlice(proc, s, beg, end - beg, num_runes));
            // ref counting managed in NewSlice()
          }
          break;

        case sloadV:
          { szl_int end = pop_szl_int(sp);
            szl_int beg = pop_szl_int(sp);
            ArrayVal* a = pop_array(sp);
            a->intersect_slice(&beg, &end, a->length());
            push(sp, a->type()->as_array()->form()->NewSlice(proc, a, beg, end - beg));
            // ref counting managed in NewSlice()
          }
          break;

        // stores
        case storeV:
          { int i = var_index(pc);
            Val** v = &bp->at(i);
            TRACE_REF("var before storeV", *v);
            TRACE_REF("tos before storeV", (Val*)sp);
            (*v)->dec_ref();
            *v = pop(sp);
            assert(*v != NULL);  // value must be defined
            TRACE_REF("after storeV", *v);
          }
          break;

        case storeVi:
          { int i = pop_szl_int(sp);
            Val** v = &bp->at(i);
            TRACE_REF("var before storeVi", *v);
            TRACE_REF("tos before storeVi", (Val*)sp);
            (*v)->dec_ref();
            *v = pop(sp);
            assert(*v != NULL);  // value must be defined
            TRACE_REF("after storeVi", *v);
          }
          break;

        case undefine:
          { int i = var_index(pc);
            Val** v = &bp->at(i);
            TRACE_REF("var before undefine", *v);
            // drop ref on variable's old value
            (*v)->dec_ref();
            // undefine variable
            *v = NULL;
          }
          break;

        case openO:
          { // instruction stream has var index and outputter index;
            // output vars are static and therefore required to be defined;
            // called at static initialization and thus only once per Process
            szl_int param = pop_szl_int(sp);
            int var_i = var_index(pc);
            int tab_i = Code::int16_at(pc);
            // initialize output variable
            bp->at(var_i) = static_cast<IntVal*>(TaggedInts::MakeVal(tab_i));
            Outputter* o = proc->outputter(tab_i);
            OutputType* type = o->type();
            // the undef reporting tables need to be remembered by proc:
            // better would be to remember the Outputter directly,
            // but the header files needed are overwhelming, so we
            // remember the var index instead
            proc->RememberOutputter(o->name(), var_i);
            // for backward-compatibility, detect emitters installed at
            // compile-time for tables with unevaluated parameters
            if (o->emitter() != NULL && !type->is_evaluated_param()) {
              proc->trap_info_ = proc->PrintError(
                  "parameter '%N' must be a constant expression",
                  type->param());
            }
            // if emitter factory is available
            // install emitters if not installed at compile time
            // (null emitters will be detected at emit time)
            if (proc->emitter_factory() != NULL &&
                type->uses_emitter() && o->emitter() == NULL) {
              if (!type->is_evaluated_param()) {
                type->set_evaluated_param(param);
                if (param < 0) {
                  proc->trap_info_ = proc->PrintError(
                      "table parameter must be positive; value is '%lld'",
                      param);
                }
                if (param > kint32max) {  // SzlTypeProto's param is int32
                  proc->trap_info_ = proc->PrintError(
                      "overflow in table parameter '%lld'", param);
                }
              }
              string error;
              Emitter* e = proc->emitter_factory()->
                  NewEmitter(o->table(), &error);
              if (e == NULL)
                proc->trap_info_ = proc->PrintError("%s", error.c_str());
              else
                proc->outputter(tab_i)->set_emitter(e);
            }
            if (proc->trap_info_ != NULL) {
              proc->set_error();  // emitter installation errors are fatal
              goto trap_handler;
            }
          }
          break;

        case fstoreV:
          { TupleVal* t = pop_tuple(sp);
            t->dec_ref();
            assert(t->is_unique());
            Val** field = &t->slot_at(Code::int16_at(pc));
            assert(*field != NULL);  // because t must be defined
            (*field)->dec_ref();
            *field = pop(sp);  // no need to adjust popped structure's ref count
          }
          break;

        case fclearB:
          { TupleVal* t = pop_tuple(sp);
            t->dec_ref();
            assert(t->is_unique());
            t->clear_slot_bit_at(Code::int32_at(pc));
          }
          break;

        case fsetB:
          { TupleVal* t = pop_tuple(sp);
            t->dec_ref();
            assert(t->is_unique());
            t->set_slot_bit_at(Code::int32_at(pc));
            t->inc_ref();  // put it back on the stack for following op
            push(sp, t);
          }
          break;

        case ftestB:
          { TupleVal* t = pop_tuple(sp);
            bool b = t->slot_bit_at(Code::int32_at(pc));
            t->dec_ref();
            push_szl_bool(sp, proc, b);
          }
          break;

        case xstore8:
          { szl_int i = pop_szl_int(sp);
            BytesVal* b = pop_bytes(sp);
            b->dec_ref();
            assert(b->is_unique());
            TEST_BYTES_INDEX(b, i);
            unsigned char x = pop_szl_int(sp);  // truncate silently to byte
            b->at(i) = x;
          }
          break;

        case xstoreR:
          { szl_int i0 = pop_szl_int(sp);
            StringVal* s = pop_string(sp);
            s->dec_ref();
            assert(s->is_unique());
            szl_int i = s->byte_offset(proc, i0);
            TEST_STRING_INDEX(s, i0, i);
            Rune x = pop_szl_int(sp);  // truncate silently to Rune
            if (x == 0) {
              proc->trap_info_ = proc->PrintError(
                  "character value (%d) is NUL, negative or too large", x);
              goto trap_handler;
            }
            s->put(proc, i, x);
          }
          break;

        case xstoreV:
          { szl_int i = pop_szl_int(sp);
            ArrayVal* a = pop_array(sp);
            a->dec_ref();
            assert(a->is_unique());
            TEST_ARRAY_INDEX(a, i);
            Val* x = pop(sp);
            Val** elem = &a->at(i);
            assert(*elem != NULL);  // because a must be defined
            (*elem)->dec_ref();
            *elem = x;
          }
          break;

        case minsertV:
          { MapVal* m = pop_map(sp);
            m->dec_ref();
            assert(m->is_unique());
            Val* key = pop(sp);
            // ref for "key" moved from stack to map
            int32 index = m->map()->InsertKey(key);
            push_szl_int(sp, proc, index);
            m->inc_ref();  // put it back on the stack for following op
            push(sp, m);
          }
          break;

        case mstoreV:
          { MapVal* m = pop_map(sp);
            m->dec_ref();
            assert(m->is_unique());
            int32 index = pop_int32(sp);  // generated index, never out of range
            Val* value = pop(sp);
            // ref for "value" moved from stack to map
            m->map()->SetValue(index, value);
          }
          break;

        case sstoreV:
          { szl_int end = pop_szl_int(sp);
            szl_int beg = pop_szl_int(sp);
            IndexableVal* a = pop_indexable(sp);
            a->dec_ref();
            assert(a->is_unique());
            Val* x = pop(sp);
            proc->trap_info_ = DoSlice(proc, a, beg, end, x);
            x->dec_ref();
            if (proc->trap_info_ != NULL) {
              goto trap_handler;
            }
          }
          break;

        // increment
        case inc64:
          { Val** vp = &bp->at(var_index(pc));
            if (*vp == NULL)
              goto trap_handler;  // variable undefined
            TaggedInts::Inc(proc, vp, Code::int8_at(pc));
          }
          break;

        case finc64:
          { TupleVal* t = pop_tuple(sp);
            t->dec_ref();
            assert(t->is_unique());
            int i = Code::int16_at(pc);
            TaggedInts::Inc(proc, &t->slot_at(i), Code::int8_at(pc));
          }
          break;

        case xinc8:
          { szl_int i = pop_szl_int(sp);
            BytesVal* b = pop_bytes(sp);
            b->dec_ref();
            assert(b->is_unique());
            TEST_BYTES_INDEX(b, i);
            b->at(i) += Code::int8_at(pc);
          }
          break;

        case xincR:
          { szl_int i0 = pop_szl_int(sp);
            StringVal* s = pop_string(sp);
            s->dec_ref();
            assert(s->is_unique());
            szl_int i = s->byte_offset(proc, i0);
            TEST_STRING_INDEX(s, i0, i);
            s->put(proc, i, s->at(i) + Code::int8_at(pc));
          }
          break;

        case xinc64:
          { szl_int i = pop_szl_int(sp);
            ArrayVal* a = pop_array(sp);
            a->dec_ref();
            assert(a->is_unique());
            TEST_ARRAY_INDEX(a, i);
            TaggedInts::Inc(proc, &a->at(i), Code::int8_at(pc));
          }
          break;

        case minc64:
          { MapVal* m = pop_map(sp);
            m->dec_ref();
            assert(m->is_unique());
            int32 i = pop_int32(sp);  // generated index, never out of range
            m->map()->IncValue(i, Code::int8_at(pc));
          }
          break;

        // literals
        case push8:
          push(sp, TaggedInts::MakeVal(Code::int8_at(pc)));
          break;

        case pushV:
          { Val* v = Code::val_at(pc);
            TRACE_REF("before pushV", v);
            v->inc_ref();
            push(sp, v);
          }
          break;

        case createB:
          { const int32 n = Code::int32_at(pc);
            BytesVal* b = Factory::NewBytes(proc, n);
            // fill in bytes
            for (int i = 0; i < n; i++)
              b->at(i) = pop_szl_int(sp);  // truncate silently to byte
            // done
            push(sp, b);
          }
          break;

        case createStr:
          // TODO: Make this more efficient?
          { const int32 n = Code::int32_at(pc);
            // build a Rune string
            Rune* buf = new Rune[n];
            int64 nbytes = 0;
            for (int i = 0; i < n; i++) {
              szl_int r = pop_szl_int(sp);
              if (!IsValidUnicode(r)) {
                // TODO: sometimes we could catch this at compile time
                proc->trap_info_ = proc->PrintError("illegal unicode character U+%x creating string from array", r);
                goto trap_handler;
              }
              nbytes += runelen(r);
              buf[i] = r;
            }
            // convert into UTF-8 string
            StringVal* s = Factory::NewString(proc, nbytes, n);
            RuneStr2Str(s->base(), nbytes, buf, n);
            delete[] buf;
            // done
            push(sp, s);
          }
          break;

        case createT:
          { TupleType* ttype = reinterpret_cast<TupleType*>(Code::ptr_at(pc));
            TupleVal* val = ttype->form()->NewVal(proc, TupleForm::set_inproto);
            // fill with dummy non-ptr Vals because GC may happen before initT
            Val* zero = TaggedInts::MakeVal(0);
            for (int i = 0; i < ttype->nslots(); i++)
              val->slot_at(i) = zero;
            // leave the tuple on the stack
            push(sp, val);
          }
          break;

        case initT:
          { const int32 from = Code::int32_at(pc);
            const int32 num_vals = Code::int32_at(pc);
            TupleVal* val = sp[num_vals]->as_tuple();
            // fill in tuple fields
            for (int i = 0; i < num_vals; i++)
              val->slot_at(from + i) = pop(sp);
            // leave the tuple on the stack
          }
          break;

        case createA:
          { const int32 length = Code::int32_at(pc);
            ArrayType* atype = reinterpret_cast<ArrayType*>(Code::ptr_at(pc));
            ArrayVal* val = atype->form()->NewVal(proc, length);
            // fill with dummy non-ptr Vals because GC may happen before initA
            Val* zero = TaggedInts::MakeVal(0);
            for (int i = 0; i < length; i++)
              val->at(i) = zero;
            push(sp, val);
          }
          break;

        case initA:
          { const int32 from = Code::int32_at(pc);
            const int32 num_vals = Code::int32_at(pc);
            ArrayVal* val = sp[num_vals]->as_array();
            // fill in array elements
            for (int i = 0; i < num_vals; i++)
              val->at(from + i) = pop(sp);
            // leave the array on the stack
          }
          break;

        case newA:
          { int64 length = pop_szl_int(sp);
            ArrayType* type = reinterpret_cast<ArrayType*>(Code::ptr_at(pc));
            Val* init = pop(sp);
            if (length < 0) {
              proc->trap_info_ = proc->PrintError("negative array length in new(%T): %lld", type, length);
              init->dec_ref();
              goto trap_handler;
            }
            ArrayVal* a = type->form()->NewVal(proc, length);
            for (int i = 0; i < length; i++) {
              a->at(i) = init;
              init->inc_ref();
            }
            init->dec_ref();
            push(sp, a);
          }
          break;

        case createM:
          { const int32 npairs = Code::int32_at(pc);
            MapType* mtype = (reinterpret_cast<MapType*>(Code::ptr_at(pc)))->as_map();
            MapVal* val = mtype->form()->NewValInit(proc, npairs, true);
            push(sp, val);
          }
          break;

        case initM:
          { const int32 num_vals = Code::int32_at(pc);
            const int32 npairs = num_vals/2;
            MapVal* val = sp[num_vals]->as_map();
            Map* map = val->map();
            for (int i = 0; i < npairs; i++) {
              int32 index;
              Val* key = pop(sp);
              index = map->InsertKey(key);  // ref goes from stack to map
              Val* value = pop(sp);
              map->SetValue(index, value);  // ref goes from stack to map
            }
            // leave the map on the stack
          }
          break;

        case newM:
          { MapType* mtype = reinterpret_cast<Type*>(Code::ptr_at(pc))->as_map();  // TODO: do we need this?
            push(sp, mtype->form()->NewValInit(proc, pop_szl_int(sp), false));
          }
          break;

        case newB:
          { int64 length = pop_szl_int(sp);
            unsigned char init = pop_szl_int(sp);  // truncate silently to byte
            if (length < 0) {
              proc->trap_info_ = proc->PrintError("negative length in new(bytes): %lld", length);
              goto trap_handler;
            }
            BytesVal* b = Factory::NewBytes(proc, length);
            memset(b->base(), init, length);
            push(sp, b);
          }
          break;

        case newStr:
          { int64 nrunes = pop_szl_int(sp);
            Rune init = pop_szl_int(sp);  // truncate silently to Rune
            if (nrunes < 0) {
              proc->trap_info_ = proc->PrintError("negative length in new(string): %lld", nrunes);
              goto trap_handler;
            }
            if (!IsValidUnicode(init)) {
              proc->trap_info_ = proc->PrintError("illegal unicode character U+%x creating new string", init);
              goto trap_handler;
            }
            char buf[UTFmax];
            int w = runetochar(buf, &init);
            StringVal* str = Factory::NewString(proc, nrunes * w, nrunes);
            char* p = str->base();
            for (int i = 0; i < nrunes; i++) {
              memmove(p, buf, w);
              p += w;
            }
            push(sp, str);
          }
          break;

         case createC:
          { Code::pcoff offs = Code::pcoff_at(pc);
            Instr* pc0 = pc;
            Frame* context = base(fp, Code::uint8_at(pc));
            FunctionType* ftype =
              reinterpret_cast<FunctionType*>(Code::ptr_at(pc));
            // fix entry computation below once we generalize createC
            ClosureVal* c =
              ftype->form()->NewVal(proc, pc0 + offs, context);
            push(sp, c);
          }
          break;

        case dupV:
          { Val* x = pop(sp);
            x->inc_ref();
            push(sp, x);
            push(sp, x);
          }
          break;

        case popV:
          pop(sp)->dec_ref();
          break;

        // arithmetics
        case and_bool:
          { bool y = pop_szl_bool(sp);
            bool x = pop_szl_bool(sp);
            push(sp, Factory::NewBool(proc, x & y));
          }
          break;

        case or_bool:
          { bool y = pop_szl_bool(sp);
            bool x = pop_szl_bool(sp);
            push(sp, Factory::NewBool(proc, x | y));
          }
          break;

        case add_int:
          { Val* y = pop(sp);
            Val* x = pop(sp);
            push(sp, TaggedInts::Add(proc, x, y));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case sub_int:
          { Val* y = pop(sp);
            Val* x = pop(sp);
            push(sp, TaggedInts::Sub(proc, x, y));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case mul_int:
          { Val* y = pop(sp);
            Val* x = pop(sp);
            push(sp, TaggedInts::Mul(proc, x, y));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case div_int:
          { Val* y = pop(sp);
            Val* x = pop(sp);
            Val* r = TaggedInts::Div(proc, x, y);
            y->dec_ref();
            if (r == NULL) {
              proc->trap_info_ = proc->PrintError("divide by zero error: %V / 0", proc, x);
              x->dec_ref();
              goto trap_handler;
            }
            push(sp, r);
            x->dec_ref();
          }
          break;

        case mod_int:
          { Val* y = pop(sp);
            Val* x = pop(sp);
            Val* r = TaggedInts::Rem(proc, x, y);
            y->dec_ref();
            if (r == NULL) {
              proc->trap_info_ = proc->PrintError("divide by zero error: %V %% 0", proc, x);
              x->dec_ref();
              goto trap_handler;
            }
            push(sp, r);
            x->dec_ref();
          }
          break;

        case shl_int:
          { szl_int y = pop_szl_int(sp);
            szl_int x = pop_szl_int(sp);
            push_szl_int(sp, proc, x << (y & 0x3f));
          }
          break;

        case shr_int:
          { uint64 y = pop_szl_int(sp);
            uint64 x = pop_szl_int(sp);
            // is a logical shift because x and y are unsigned
            push_szl_int(sp, proc, x >> (y & 0x3f));
          }
          break;

        case and_int:
          { szl_int y = pop_szl_int(sp);
            szl_int x = pop_szl_int(sp);
            push_szl_int(sp, proc, x & y);
          }
          break;

        case or_int:
          { szl_int y = pop_szl_int(sp);
            szl_int x = pop_szl_int(sp);
            push_szl_int(sp, proc, x | y);
          }
          break;

        case xor_int:
          { szl_int y = pop_szl_int(sp);
            szl_int x = pop_szl_int(sp);
            push_szl_int(sp, proc, x ^ y);
          }
          break;

        case add_uint:
          { szl_uint y = pop_szl_uint(sp);
            szl_uint x = pop_szl_uint(sp);
            push_szl_uint(sp, proc, x + y);
          }
          break;

        case sub_uint:
          { szl_uint y = pop_szl_uint(sp);
            szl_uint x = pop_szl_uint(sp);
            push_szl_uint(sp, proc, x - y);
          }
          break;

        case mul_uint:
          { szl_uint y = pop_szl_uint(sp);
            szl_uint x = pop_szl_uint(sp);
            push_szl_uint(sp, proc, x * y);
          }
          break;

        case div_uint:
          { szl_uint y = pop_szl_uint(sp);
            szl_uint x = pop_szl_uint(sp);
            if (y == 0) {
              proc->trap_info_ = proc->PrintError("divide by zero error: %llud / 0", x);
              goto trap_handler;
            }
            push_szl_uint(sp, proc, x / y);
          }
          break;

        case mod_uint:
          { szl_uint y = pop_szl_uint(sp);
            szl_uint x = pop_szl_uint(sp);
            if (y == 0) {
              proc->trap_info_ = proc->PrintError("divide by zero error: %llud %% 0.0", x);
              goto trap_handler;
            }
            push_szl_uint(sp, proc, x % y);
          }
          break;

        case shl_uint:
          { szl_uint y = pop_szl_uint(sp);
            szl_uint x = pop_szl_uint(sp);
            push_szl_uint(sp, proc, x << (y & 0x3f));
          }
          break;

        case shr_uint:
          { szl_uint y = pop_szl_uint(sp);
            szl_uint x = pop_szl_uint(sp);
            // is a logical shift because x and y are unsigned
            push_szl_uint(sp, proc, x >> (y & 0x3f));
          }
          break;

        case and_uint:
          { szl_uint y = pop_szl_uint(sp);
            szl_uint x = pop_szl_uint(sp);
            push_szl_uint(sp, proc, x & y);
          }
          break;

        case or_uint:
          { szl_uint y = pop_szl_uint(sp);
            szl_uint x = pop_szl_uint(sp);
            push_szl_uint(sp, proc, x | y);
          }
          break;

        case xor_uint:
          { szl_uint y = pop_szl_uint(sp);
            szl_uint x = pop_szl_uint(sp);
            push_szl_uint(sp, proc, x ^ y);
          }
          break;

        case add_float:
          { szl_float y = pop_szl_float(sp);
            szl_float x = pop_szl_float(sp);
            push_szl_float(sp, proc, x + y);
          }
          break;

        case sub_float:
          { szl_float y = pop_szl_float(sp);
            szl_float x = pop_szl_float(sp);
            push_szl_float(sp, proc, x - y);
          }
          break;

        case mul_float:
          { szl_float y = pop_szl_float(sp);
            szl_float x = pop_szl_float(sp);
            push_szl_float(sp, proc, x * y);
          }
          break;

        case div_float:
          { szl_float y = pop_szl_float(sp);
            szl_float x = pop_szl_float(sp);
            if (y == 0.0) {
              proc->trap_info_ = proc->PrintError("divide by zero error: %g / 0.0", x);
              goto trap_handler;
            }
            push_szl_float(sp, proc, x / y);
          }
          break;

        case add_fpr:
          { szl_fingerprint y = pop_szl_fingerprint(sp);
            szl_fingerprint x = pop_szl_fingerprint(sp);
            push(sp, Factory::NewFingerprint(proc, FingerprintCat(x, y)));
          }
          break;

        case add_array:
          { ArrayVal* y = pop_array(sp);
            ArrayVal* x = pop_array(sp);
            assert(x->type()->IsEqual(y->type(), false));
            const int xl = x->length();
            const int yl = y->length();
            ArrayVal* s = x->type()->as_array()->form()->NewVal(proc, xl + yl);
            int i = 0;
            while (i < xl) {
              Val* e = x->at(i);
              e->inc_ref();
              s->at(i++) = e;
            }
            for (int j = 0; j < yl; j++) {
              Val* e = y->at(j);
              e->inc_ref();
              s->at(i++) = e;
            }
            x->dec_ref();
            y->dec_ref();
            push(sp, s);
          }
          break;

        case add_bytes:
          { BytesVal* y = pop_bytes(sp);
            BytesVal* x = pop_bytes(sp);
            assert(x->is_bytes() && y->is_bytes());
            const int xl = x->length();
            const int yl = y->length();
            BytesVal* s =
              SymbolTable::bytes_form()->NewVal(proc, xl + yl);
            memmove(&(s->u_base()[0]), x->base(), xl * sizeof(unsigned char));
            memmove(&(s->u_base()[xl]), y->base(), yl * sizeof(unsigned char));
            x->dec_ref();
            y->dec_ref();
            push(sp, s);
          }
          break;

        case add_string:
          { StringVal* y = pop_string(sp);
            StringVal* x = pop_string(sp);
            assert(x->is_string() && y->is_string());
            const int xl = x->length();
            const int yl = y->length();
            StringVal* s =
              SymbolTable::string_form()->NewVal(proc, xl + yl, x->num_runes() + y->num_runes());
            memmove(&(s->base()[0]), x->base(), xl);
            memmove(&(s->base()[xl]), y->base(), yl);
            x->dec_ref();
            y->dec_ref();
            push(sp, s);
          }
          break;

        case add_time:
          { szl_time y = pop_szl_time(sp);
            szl_time x = pop_szl_time(sp);
            push(sp, Factory::NewTime(proc, x + y));
          }
          break;

        case sub_time:
          { szl_time y = pop_szl_time(sp);
            szl_time x = pop_szl_time(sp);
            push(sp, Factory::NewTime(proc, x - y));
          }
          break;

        // condition codes
        case set_cc:
          cc = pop_szl_bool(sp);
          break;

        case get_cc:
          push_szl_bool(sp, proc, cc);
          break;

        // comparisons
        case cmp_begin:  // silence C++ compiler warnings
          ShouldNotReachHere();
          break;

        case eql_bits:
          { uint64 y = pop_szl_bits(sp);
            uint64 x = pop_szl_bits(sp);
            cc = (x == y);
          }
          break;

        case neq_bits:
          { uint64 y = pop_szl_bits(sp);
            uint64 x = pop_szl_bits(sp);
            cc = (x != y);
          }
          break;

        case lss_bits:
          { uint64 y = pop_szl_bits(sp);
            uint64 x = pop_szl_bits(sp);
            cc = (x < y);
          }
          break;

        case leq_bits:
          { uint64 y = pop_szl_bits(sp);
            uint64 x = pop_szl_bits(sp);
            cc = (x <= y);
          }
          break;

        case gtr_bits:
          { uint64 y = pop_szl_bits(sp);
            uint64 x = pop_szl_bits(sp);
            cc = (x > y);
          }
          break;

        case geq_bits:
          { uint64 y = pop_szl_bits(sp);
            uint64 x = pop_szl_bits(sp);
            cc = (x >= y);
          }
          break;

        case eql_float:
          { szl_float y = pop_szl_float(sp);
            szl_float x = pop_szl_float(sp);
            cc = (x == y);
          }
          break;

        case neq_float:
          { szl_float y = pop_szl_float(sp);
            szl_float x = pop_szl_float(sp);
            cc = (x != y);
          }
          break;

        case lss_float:
          { szl_float y = pop_szl_float(sp);
            szl_float x = pop_szl_float(sp);
            cc = (x < y);
          }
          break;

        case leq_float:
          { szl_float y = pop_szl_float(sp);
            szl_float x = pop_szl_float(sp);
            cc = (x <= y);
          }
          break;

        case gtr_float:
          { szl_float y = pop_szl_float(sp);
            szl_float x = pop_szl_float(sp);
            cc = (x > y);
          }
          break;

        case geq_float:
          { szl_float y = pop_szl_float(sp);
            szl_float x = pop_szl_float(sp);
            cc = (x >= y);
          }
          break;

        case lss_int:
          { Val* y = pop(sp);
            Val* x = pop(sp);
            cc = (TaggedInts::Lss(x, y));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case leq_int:
          { Val* y = pop(sp);
            Val* x = pop(sp);
            cc = (! TaggedInts::Lss(y, x));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case gtr_int:
          { Val* y = pop(sp);
            Val* x = pop(sp);
            cc = (TaggedInts::Lss(y, x));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case geq_int:
          { Val* y = pop(sp);
            Val* x = pop(sp);
            cc = (! TaggedInts::Lss(x, y));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case eql_string:
          { StringVal* y = pop_string(sp);
            StringVal* x = pop_string(sp);
            cc = eq_string(x, y);
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case neq_string:
          { StringVal* y = pop_string(sp);
            StringVal* x = pop_string(sp);
            cc = !eq_string(x, y);
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case lss_string:
          { StringVal* y = pop_string(sp);
            StringVal* x = pop_string(sp);
            cc = (cmp_string(x, y) < 0);
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case leq_string:
          { StringVal* y = pop_string(sp);
            StringVal* x = pop_string(sp);
            cc = (cmp_string(x, y) <= 0);
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case gtr_string:
          { StringVal* y = pop_string(sp);
            StringVal* x = pop_string(sp);
            cc = (cmp_string(x, y) > 0);
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case geq_string:
          { StringVal* y = pop_string(sp);
            StringVal* x = pop_string(sp);
            cc = (cmp_string(x, y) >= 0);
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case eql_bytes:
          { BytesVal* y = pop_bytes(sp);
            BytesVal* x = pop_bytes(sp);
            cc = eq_bytes(x, y);
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case neq_bytes:
          { BytesVal* y = pop_bytes(sp);
            BytesVal* x = pop_bytes(sp);
            cc = !eq_bytes(x, y);
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case lss_bytes:
          { BytesVal* y = pop_bytes(sp);
            BytesVal* x = pop_bytes(sp);
            cc = (cmp_bytes(x, y) < 0);
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case leq_bytes:
          { BytesVal* y = pop_bytes(sp);
            BytesVal* x = pop_bytes(sp);
            cc = (cmp_bytes(x, y) <= 0);
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case gtr_bytes:
          { BytesVal* y = pop_bytes(sp);
            BytesVal* x = pop_bytes(sp);
            cc = (cmp_bytes(x, y) > 0);
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case geq_bytes:
          { BytesVal* y = pop_bytes(sp);
            BytesVal* x = pop_bytes(sp);
            cc = (cmp_bytes(x, y) >= 0);
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case eql_array:
          { ArrayVal* y = pop_array(sp);
            ArrayVal* x = pop_array(sp);
            cc = (x->IsEqual(y));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case neq_array:
          { ArrayVal* y = pop_array(sp);
            ArrayVal* x = pop_array(sp);
            cc = (!x->IsEqual(y));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case eql_map:
          { MapVal* y = pop_map(sp);
            MapVal* x = pop_map(sp);
            cc = (x->IsEqual(y));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case neq_map:
          { MapVal* y = pop_map(sp);
            MapVal* x = pop_map(sp);
            cc = (!x->IsEqual(y));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case eql_tuple:
          { TupleVal* y = pop_tuple(sp);
            TupleVal* x = pop_tuple(sp);
            cc = (x->IsEqual(y));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case neq_tuple:
          { TupleVal* y = pop_tuple(sp);
            TupleVal* x = pop_tuple(sp);
            cc = (!x->IsEqual(y));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case eql_closure:
          { ClosureVal* y = pop(sp)->as_closure();
            ClosureVal* x = pop(sp)->as_closure();
            cc = (x->IsEqual(y));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case neq_closure:
          { ClosureVal* y = pop(sp)->as_closure();
            ClosureVal* x = pop(sp)->as_closure();
            cc = (!x->IsEqual(y));
            x->dec_ref();
            y->dec_ref();
          }
          break;

        case cmp_end:  // silence C++ compiler warnings
          ShouldNotReachHere();
          break;

        // conversions
        case basicconv:
          { // use temporaries so we don't force sp into memory
            Val** tmp = sp;
            ConversionOp op = (ConversionOp)Code::uint8_at(pc);
            Type* type;
            switch (op) {
              case typecast:
              case bytes2proto:
              case proto2bytes:
              case tuple2tuple:
                type = (Type*)Code::ptr_at(pc);
                break;

              default:
                type = NULL;
                break;
            }
            proc->trap_info_ = ConvOp::ConvertBasic(proc, op, tmp, type);
            sp = tmp;
            if (proc->trap_info_ != NULL)
              goto trap_handler;
          }
          break;

        case arrayconv:
          { // use a temporary so we don't force sp into memory
            Val** tmp = sp;
            ConversionOp op = (ConversionOp)Code::uint8_at(pc);
            ArrayType* type = (op == typecast || op == tuple2tuple ||
                               op == bytes2proto || op == proto2bytes)
                              ? ((Type*)Code::ptr_at(pc))->as_array() : NULL;
            proc->trap_info_ = ConvOp::ConvertArray(proc, op, tmp, type);
            sp = tmp;
            if (proc->trap_info_ != NULL)
              goto trap_handler;
          }
          break;

        case mapconv:
          { // use a temporary so we don't force sp into memory
            Val** tmp = sp;
            MapType* type = (reinterpret_cast<Type*>(Code::ptr_at(pc)))->as_map();
            ConversionOp key_op = (ConversionOp)Code::uint8_at(pc);
            ConversionOp value_op = (ConversionOp)Code::uint8_at(pc);
            proc->trap_info_ = ConvOp::ConvertArrayToMap(proc, type, key_op, value_op, tmp);
            sp = tmp;
            if (proc->trap_info_ != NULL)
              goto trap_handler;
          }
          break;

        // control structures
        case branch:
          { // use a temporary to force evaluation order we need:
            // pc must be incremented after it has been modified by pcoff_at
            int offs = Code::pcoff_at(pc);
            pc += offs;
          }
          break;

        case branch_true:
          { int offs = Code::pcoff_at(pc);
            if (cc)
              pc += offs;
          }
          break;

        case branch_false:
          { int offs = Code::pcoff_at(pc);
            if (! cc)
              pc += offs;
          }
          break;

        case trap_false:
          { const char* info = reinterpret_cast<const char*>(Code::ptr_at(pc));
            if (! cc) {
              proc->trap_info_ = info;
              goto trap_handler;
            }
          }
          break;

        // calls
        case enter:
          // allocate space for variables
          { // n: slots for local variables
            int n = Code::int32_at(pc);
            // m: slots for expression stack
            int m = Code::int32_at(pc);
            int frame = sizeof(Frame)/sizeof(Val*);
            // stack overflow check
            if (sp - n - m - frame  < lp) {
              // at this point we are still in the caller's frame because we
              // have not pushed the new activation record => we must use the
              // return_pc to get a correct stack trace
              FrameIterator::PrintStack(2, FLAGS_stacktrace_length, proc,
                                        fp, sp, return_pc);
              proc->set_error();  // terminate execution
              ptrdiff_t count = proc->initial_sp() - (sp - n - m - frame);
              proc->trap_info_ =
                  proc->PrintError("stack overflow: set --stack_size >= %lld",
                      static_cast<int64>(count * sizeof(Val*)));
              goto trap_handler;
            }
            // zero out variables (because of ref counting
            // during initial assignment to array, map, or
            // tuples)
            while (n-- > 0)
              push(sp, NULL);
          }
          // setup frame
          fp = push_frame(sp, fp, bp, return_pc);
          assert(sp == fp->stack());
          return_pc = NULL;  // must only be valid between call and enter
          break;

        case set_bp:
          bp = base(fp, Code::uint8_at(pc));
          continue;  // do not reset bp!

        case callc:
          { // use a temporary so we don't force sp into memory
            Val** tmp = sp;
            proc->trap_info_ = (*(Intrinsic::CFunctionCanFail)
                                Code::ptr_at(pc))(proc, tmp);
            sp = tmp;
            if (proc->trap_info_ != NULL)
              goto trap_handler;
          }
          break;

        case callcnf:
          { // use a temporary so we don't force sp into memory
            Val** tmp = sp;
            (*(Intrinsic::CFunctionCannotFail)Code::ptr_at(pc))(proc, tmp);
            sp = tmp;
          }
          break;

        case call:
          { ClosureVal* c = pop(sp)->as_closure();
            bp = c->context();
            return_pc = pc;
            pc = c->entry();
            c->dec_ref();
          }
          continue;  // do not reset bp!

        case calli:
          { Code::pcoff offs = Code::pcoff_at(pc);
            return_pc = pc;
            pc += offs;
          }
          continue;  // do not reset bp!

        case ret:
          // no result
          fp = pop_frame(sp, fp, pc, Code::int16_at(pc));
          if (pc == NULL) {
            SAVE_STATE(Proc::TERMINATED, -cycle_count);
            return Proc::TERMINATED;
          }
          break;

        case retV:
          { Val* result = pop(sp);
            fp = pop_frame(sp, fp, pc, Code::int16_at(pc));
            push(sp, result);
            if (pc == NULL) {
              // The return address can be null even for a
              // value-returning function, if the value-returning
              // function is being invoked by Proc::DoCall.
              CHECK(proc->mode_ & kDoCalls)
                  << "return address of a value-returning function "
                  << "unexpectedly null";
              SAVE_STATE(Proc::TERMINATED, -cycle_count);
              return Proc::TERMINATED;
            }
          }
          break;

        case retU:
          // no result
          fp = pop_frame(sp, fp, pc, 0 /* doesn't pop locals */);
          goto trap_handler;

        case terminate:
          SAVE_STATE(Proc::TERMINATED, -cycle_count);
          return Proc::TERMINATED;

        case stop:
          proc->set_error();  // terminate execution
          proc->trap_info_ = proc->PrintError("%s", Code::ptr_at(pc));
          goto trap_handler;

        case match:
          { Val** tmp = sp;
            proc->trap_info_ = Intrinsics::Match(proc, tmp, Code::ptr_at(pc));
            sp = tmp;
            if (proc->trap_info_ != NULL)
              goto trap_handler;
          }
          break;

        case matchposns:
          { Val** tmp = sp;
            proc->trap_info_ = Intrinsics::Matchposns(proc, tmp, Code::ptr_at(pc));
            sp = tmp;
            if (proc->trap_info_ != NULL)
              goto trap_handler;
          }
          break;

        case matchstrs:
          { Val** tmp = sp;
            proc->trap_info_ = Intrinsics::Matchstrs(proc, tmp, Code::ptr_at(pc));
            sp = tmp;
            if (proc->trap_info_ != NULL)
              goto trap_handler;
          }
          break;

        case saw:
          { Val** tmp = sp;
            int count = Code::uint8_at(pc);
            proc->trap_info_ = Intrinsics::Saw(proc, tmp, count, reinterpret_cast<void**>(pc));
            sp = tmp;
            if (proc->trap_info_ != NULL)
              goto trap_handler;
            pc += sizeof(void*);  // skip over cache entry
          }
          break;

        // emit
        case emit:
          // no need to check if variable is defined, since output vars
          // are always defined by the initialization code (openO)
          { int out_index = pop_szl_int(sp);
            Val** tmp = sp;
            proc->trap_info_ = proc->outputter(out_index)->Emit(tmp);
            sp = tmp;
            if (proc->trap_info_ != NULL) {
              proc->set_error();  // emitter errors are fatal execution errors
              goto trap_handler;
            }
          }
          break;

        // printing
        case fd_print:
          { int fd = pop_szl_int(sp);
            StringVal* afmt = pop_string(sp);
            Fmt::State f;
            char buf[128];
            F.fmtfdinit(&f, fd, buf, sizeof buf);
            sp = Print(&f, afmt->base(), afmt->length(), proc, sp);
            afmt->dec_ref();
            F.fmtfdflush(&f);
            // push integer return result
            push_szl_int(sp, proc, 0);
          }
          break;

        // line profiling counter. code only emitted if FLAGS_szl_bb_count
        case count:
          { int index = Code::int32_at(pc);
            proc->linecount()->IncCounter(index);
          }
          break;

        default:
          // compiler bug => FatalError
          FatalError("unknown instruction: %p  %s", pc - 1, Opcode2String((Opcode)(pc[-1])));
          break;

        trap_handler:
          { Proc::Status s = proc->status();
            if (s != Proc::FAILED)
              s = Proc::TRAPPED;
            SAVE_STATE(s, -cycle_count);
            return s;
          }
      }

      // reset base pointer
      bp = fp;
    }  // inner interpreter loop

    // If inner loop stopped by heap because it wants to do GC, do it now.
    gctrigger.CheckForGC(fp, sp, pc);
  }  // outer interpreter loop

  SAVE_STATE(Proc::SUSPENDED, 0);
  return Proc::SUSPENDED;
}


// Print arguments already pushed on stack. TOS is first argument.
// Format has already been popped.
// Takes a Fmt::State* argument so it can be used to print to
// file descriptors, buffers, etc.

Val** Engine::Print(Fmt::State* f, char* afmt, size_t nfmt, Proc* proc, Val** sp) {
  char* fmt = afmt;
  char* efmt = fmt + nfmt;
  while (fmt < efmt) {
    Rune r;
    fmt += FastCharToRune(&r, fmt);
    switch (r) {
      default:
        Fmt::fmtrune(f, r);
        break;

      case '\0':  // really shouldn't happen
        break;

      case '%':
        { int i;
          // this loop is safe to use with chars, since it's using byte equality in strchr
          // and the argument string is ASCII.
          for (i = 0; fmt[i] != '\0' && strchr("%bcdeEfgGikopqstTuxX", fmt[i]) == NULL; i++)
            ;
          // use a regular buffer to avoid allocation; size verified in parser
          char tmp[kMaxFormatLen];
          // pop appropriate argument (if any); may need to rewrite the verb
          int w = FastCharToRune(&r, &fmt[i]);
          switch (r) {
            default:
              F.fmtprint(f, "%%bad(%C)%%", r);
              continue;

            case '%':
              F.fmtprint(f, "%%");
              break;

            case 'b':
              // ignore flags and just print the word
              F.fmtprint(f, "%s", pop_szl_bool(sp) ? "true" : "false");
              break;

            case 'c': {
                // TODO: write a helper function for these snprint calls?
                F.snprint(tmp, sizeof(tmp), "%%%.*sC", i, fmt);
                int j = pop_szl_int(sp);  // convert to int from int64
                F.fmtprint(f, tmp, j);
              }
              break;

            case 'k': {
                F.snprint(tmp, sizeof(tmp), "%%%.*sk", i, fmt);
                int j = pop_szl_int(sp);  // convert to int from int64
                F.fmtprint(f, tmp, j);
              }
              break;

            case 'i':
              r = 'd';  // we overrode %i
              // fall through
            case 'd':
            case 'o':
            case 'u':
            case 'x':
            case 'X':
              F.snprint(tmp, sizeof(tmp), "%%%.*sll%C", i, fmt, r);
              if ((*sp)->is_uint())
                F.fmtprint(f, tmp, pop_szl_uint(sp));
              else
                F.fmtprint(f, tmp, pop_szl_int(sp));
              break;

            case 'e':
            case 'E':
            case 'f':
            case 'g':
            case 'G':
              F.snprint(tmp, sizeof(tmp), "%%%.*sl%C", i, fmt, r);
              F.fmtprint(f, tmp, pop_szl_float(sp));
              break;

            case 'p':
              // for now, ignore flags and just print a hex number. TODO?
              F.fmtprint(f, "0x%.16llx", pop_szl_fingerprint(sp));
              break;

            case 's':
            case 'q':
              { // If the format has a period in it, we can't use %.*s to limit
                // the string (because of slices, strings aren't necessarily
                // zero-terminated).  So check for the period, and allocate
                // a null-terminated temporary if necessary; otherwise avoid
                // the overhead of the allocation.  Since the period case is
                // rare and it's much easier to look for periods in a
                // null-terminated char* than a count-terminated Rune*,
                // generate the %.*S format first.
                StringVal* s = pop_string(sp);
                int len = F.snprint(tmp, sizeof(tmp), "%%%.*s.*%C", i, fmt, r);
                if (strchr(tmp, '.') == &tmp[len - 3]) {
                  // no user-provided periods; easy
                  F.fmtprint(f, tmp, s->length(), s->base());
                } else {
                  F.snprint(tmp, sizeof(tmp), "%%%.*s%C", i, fmt, r);
                  // make a null-terminated copy so user's length will work
                  char* str = F.smprint("%.*s", s->length(), s->base());
                  F.fmtprint(f, tmp, str);
                  free(str);
                }
                s->dec_ref();
              }
              break;

            case 't':
              { // ignore flags and just print the time as a string
                char buf[kMaxTimeStringLen + 1];
                if (SzlTime2Str(pop_szl_time(sp), "", &buf))
                  F.fmtprint(f, "%s", buf);
                else
                  F.fmtprint(f, kStringForInvalidTime);
              }
              break;

            case 'T': {
                F.snprint(tmp, sizeof(tmp), "%%%.*sT", i, fmt);
                Val* val = pop(sp);
                F.fmtprint(f, tmp, val->type());
                val->dec_ref();
              }
              break;

          }
          fmt += i + w;
        }
        break;
    }
  }
  return sp;
}

}  // namespace sawzall
