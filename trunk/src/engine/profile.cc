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

#include "engine/globals.h"
#include "public/logging.h"

#include "utilities/sysutils.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/code.h"
#include "engine/frame.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/tracer.h"
#include "engine/codegen.h"
#include "engine/compiler.h"
#include "engine/proc.h"
#include "engine/profile.h"


namespace sawzall {


Profile::Profile(Proc* proc)
  : proc_(proc) {
  // allocate & clear space -
  // we allocate a counter for each code interval
  // of size CodeDesc::kAlignment; i.e., we collect
  // profiling information on a relatively fine-grained
  // instruction block level
  assert(proc->code()->size() % CodeDesc::kAlignment == 0);
  length_ = proc->code()->size() / CodeDesc::kAlignment;
  ticks_ = new Count[length_];  // explicitly deallocated by destructor
  Reset();
}


Profile::~Profile() {
  delete[] ticks_;
}


void Profile::Start() {
  // we are stopped => last_ is the number of
  // cycles accumulated before we were suspended
  if (!is_started_) {
    last_ = CycleClockNow() - last_;
    is_started_ = true;
  }
}


void Profile::Stop() {
  // we are started => set last_ to the number
  // of cycles since the last tick was handled
  if (is_started_) {
    last_ = CycleClockNow() - last_;
    is_started_ = false;
  }
}


int Profile::HandleTick(Frame* fp, Val** sp, Instr* pc) {
  CHECK(is_started_) << ": profiler must be started to handle tick";

  // compute delta as a fraction of the number of cycles per
  // instruction since the last tick: we assume that each instruction
  // takes at least 8 cycles (pretty safe), and thus reduce the
  // number further by a factor of 8. If delta becomes 0, we
  // ignore the tick and let some more time accumulate (this
  // never happens with the existing interpreter: typical instructions
  // take between 75 and 100 cycles, the average delta is around 10).
  const int kLogInstrPerTick = 10;
  const int kInstrPerTick = 1 << kLogInstrPerTick;  // avrg no. of instrs/tick
  const int shift = kLogInstrPerTick + 3;  // 3 for division by 8
  const int delta = (CycleClockNow() - last_) >> shift;
  CHECK(delta >= 0) << ": delta overflow in Sawzall profiler";

  // compute number of instructions before next tick
  // (use a pseudo random number to avoid pathological
  // cases where kInstrPerTick relates in some form to the
  // running program)
  const int mask = (1 << kLogInstrPerTick) - 1;
  const int count = (kInstrPerTick >> 1) + (rnd_.Next() & mask);

  if (delta == 0)  // ignore tick and keep old time_
    return count;

  // iterate through the stack and credit each pc interval
  const int max_depth = 10;  // don't spend too much time in deep stacks
  FrameIterator f(proc_, fp, NULL, sp, pc);
  const Instr* base = proc_->code()->base();
  for (int depth = 0; depth < max_depth && f.is_valid(); depth++) {
    // map pc to tick counter
    const int index = (f.pc() - base) / CodeDesc::kAlignment;
    Count* c = ticks_at(index);
    // increment tick counters
    c->all += delta;
    if (depth == 0)
      c->top += delta;
    f.Unwind();
  }

  // don't measure time spent in HandleTick => call Now() again
  last_ = CycleClockNow();
  return count;
}


void Profile::Reset() {
  for (int i = length_; i-- > 0; )
    ticks_at(i)->Clear();
  last_ = 0;
  is_started_ = false;
}


static int Compare(Profile::Count* const* x, Profile::Count* const* y) {
  return (*x)->top - (*y)->top;
}


static Profile::Count Sum(Profile::Count* list, int length) {
  Profile::Count s;
  s.Clear();
  for (int i = length; i-- > 0; )
    s.Add(&list[i]);
  return s;
}


static void Print(Code* code, Profile::Count* ticks, int length,
                  float cutoff, void (*PrintComment)(Code* code, int index)) {
  const Profile::Count total = Sum(ticks, length);
  if (total.top > 0) {
    assert(total.all > 0);  // if there are 'top' ticks, there must be 'all' ticks!

    // 1) sort ticks: setup a permutation array, and sort via
    // that array so we can keep track of the (index -> count) mapping
    Profile::Count** perm = new Profile::Count*[length];
    for (int i = 0; i < length; i++)
      perm[i] = &ticks[i];
    qsort(perm, length, sizeof(Profile::Count*),
          reinterpret_cast<int(*)(const void*, const void*)>(&Compare));

    // 2) print the result
    // the last entry in perm points to the tick counts of the most frequently used function
    const char* format1 = "%4d.  %5.1f%% %7d  %5.1f%% %7d  ";
    const char* format2 = "total  %5.1f%% %7d  %5.1f%% %7d  (cutoff = %5.1f%%)\n";
    Profile::Count sum;
    sum.Clear();
    for (int i = length; i-- > 0; ) {
      Profile::Count* c = perm[i];
      const float top_fraq = static_cast<float>(c->top) / total.top;
      const float all_fraq = static_cast<float>(c->all) / total.all;
      if (top_fraq >= cutoff || all_fraq >= cutoff) {
        // only print entries where at least one count is above the cutoff value
        F.print(format1,
                   length - i,
                   100.0 * top_fraq,
                   c->top,
                   100.0 * all_fraq,
                   c->all);
        PrintComment(code, perm[i] - ticks);
        F.print("\n");
        sum.Add(c);
      }
    }

    // 3) print summary
    F.print(format2,
               100.0 * sum.top / total.top,
               sum.top,
               100.0 * sum.all / total.all,
               sum.all,
               100.0 * cutoff);
    delete [] perm;
  } else {
    // no ticks counted
    F.print("no ticks counted\n");
  }
  F.print("\n");
}


static void PrintFunction(Code* code, int index) {
  const char* fun = "INIT";
  CodeDesc* desc = code->DescForIndex(index);
  if (desc != NULL && desc->function() != NULL)
    fun = desc->function()->name();
  F.print("%s", fun);
}


static void PrintSegment(Code* code, int index) {
  // print code interval
  const int offset = index * CodeDesc::kAlignment;
  F.print("[%6d, %6d)  ", offset, offset + CodeDesc::kAlignment);
  // print corresponding function
  Instr* pc = code->base() + offset;
  PrintFunction(code, code->DescForInstr(pc)->index());
}


void Profile::PrintRaw(float cutoff) const {
  F.print("rank     top%%   ticks    all%%   ticks  code segment      function\n");
  Print(proc_->code(), ticks_, length_, cutoff, PrintSegment);
}


void Profile::PrintAggregated(float cutoff) const {
  // even though we collect ticks for relatively fine-grained code
  // intervals, we don't have a mechanism to map the pc's back
  // to actual source code positions at this point => for now map
  // pc's back to individual functions (i.e. aggregate all ticks for
  // pc's belonging to the same function)
  const Code* code = proc_->code();
  const int length = code->number_of_segments();
  Count* ticks = new Count[length];  // explicitly deallocated
  // clear all entries
  for (int i = length; i-- > 0; )
    ticks[i].Clear();
  // aggregate ticks_
  Instr* base = code->base();
  for (int i = length_; i-- > 0; ) {
    Instr* pc = base + i * CodeDesc::kAlignment;
    const int j = code->DescForInstr(pc)->index();
    assert(0 <= j && j < length);
    ticks[j].Add(ticks_at(i));
  }

  F.print("rank     top%%   ticks    all%%   ticks  function\n");
  Print(proc_->code(), ticks, length, cutoff, PrintFunction);

  delete[] ticks;
}


}  // namespace sawzall
