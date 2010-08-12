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

#include <stdint.h>

#include "engine/globals.h"
#include "engine/gctrigger.h"
#include "engine/memory.h"


namespace sawzall {

GCTrigger::GCTrigger(Memory* heap, int* num_steps, int* cycle_count)
    : heap_(heap),
      num_steps_(num_steps),
      cycle_count_(cycle_count),
      stop_for_gc_(false) {
  heap_->RegisterGCTrigger(this);
}


GCTrigger::~GCTrigger() {
  heap_->RegisterGCTrigger(NULL);
}


void GCTrigger::SetupStopForGC() {
  // The interpreter loop has already added the original cycle_count
  // to num_steps; subtract the count of loops we will not execute back in.
  *num_steps_ -= *cycle_count_;
  // Force the inner loop to stop.  We use this convoluted method to avoid
  // adding another test in the inner loop, which would have a significant
  // impact on execution time.
  *cycle_count_ = 0;
  // Once stopped, we want to run GC.
  stop_for_gc_ = true;
}


void GCTrigger::CheckForGC(Frame* fp, Val** sp, Instr* pc) {
  if (stop_for_gc_) {
    heap_->GarbageCollect(fp, sp, pc);
    stop_for_gc_ = false;
  }
}

}  // end namespace sawzall
