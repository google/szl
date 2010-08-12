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

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/map.h"
#include "engine/proc.h"
#include "engine/opcode.h"
#include "engine/histogram.h"


namespace sawzall {


Histogram* Histogram::New(Proc* proc) {
  Histogram* h = NEW(proc, Histogram);
  h->proc_ = proc;
  h->Reset();
  return h;
}


void Histogram::Collect(Histogram* histo) {
  for (int i = number_of_opcodes; i-- > 0; )
    counts_[i] += histo->counts_[i];
}


Histogram::Counter Histogram::TotalCount() const {
  Counter t = 0;
  for (int i = number_of_opcodes; i-- > 0; )
    t += counts_[i];
  return t;
}


void Histogram::Reset() {
  for (int i = number_of_opcodes; i-- > 0; )
    counts_[i] = 0;
}


static int Compare(const Histogram::Counter* const* x, const Histogram::Counter* const* y) {
  return **x - **y;
}


void Histogram::Print(float cutoff) const {
  const Counter total = TotalCount();
  if (total > 0) {
    // 1) sort the byte codes: setup a permutation array, and sort via that
    // array so we can keep track of the (opcode -> count) mapping
    const Counter** perm = new const Counter*[number_of_opcodes];
    for (int i = 0; i < number_of_opcodes; i++)
      perm[i] = &counts_[i];
    qsort(perm, number_of_opcodes, sizeof(const Counter*),
          reinterpret_cast<int(*)(const void*, const void*)>(&Compare));

    // 2) print result
    // 2a) print header
    F.print("rank        %%       count  opcode\n");
    // 2b) print opcodes
    // the last entry in perm points to the most frequently used opcode
    const char* format1 = "%4d.  %5.1f%%  %10lld  %s\n";
    const char* format2 = "total  %5.1f%%  %10lld  out of %lld opcodes\n";
    Counter sum = 0;
    for (int i = number_of_opcodes; i-- > 0; ) {
      Opcode op = static_cast<Opcode>(perm[i] - counts_);
      const Counter count = counts_[op];
      const float fraction = static_cast<float>(count) / total;  // note that total > 0
      if (fraction < cutoff)
        break;  // all subsequent counts will be below the cutoff point
      F.print(format1, number_of_opcodes - i, fraction * 100.0, count,
                 Opcode2String(op));
      sum += count;
    }
    // 2c) print summary
    F.print(format2, 100.0 * sum / total, sum, total);
    delete [] perm;
  } else {
    // no byte codes counted
    F.print("no opcodes counted\n");
  }
  F.print("\n");
}


}  // namespace sawzall
