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

class Memory;
class Frame;
class Val;

// Private interface between Engine and Memory to stop execution for GC.
class GCTrigger {
 public:
  GCTrigger(Memory* heap, int* num_steps, int* cycle_count);
  ~GCTrigger();
  // called by heap to indicate it wants to do GC
  void SetupStopForGC();
  // called after inner loop to do GC if requested
  void CheckForGC(Frame* fp, Val** sp, Instr* pc);
  
 private:
  Memory* heap_;      // for running GC
  int* num_steps_;    // interpreter loop counters that must be adjusted
  int* cycle_count_;
  bool stop_for_gc_;  // indicates we want to stop for GC
};

}  // end namespace sawzall
