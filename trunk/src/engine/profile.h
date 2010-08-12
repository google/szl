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

// ----------------------------------------------------------------------------
// A simple and cheap pseudo random number generator
// The cycle length is 2147483647, but the low order
// bits don't exhibit a very 'random' distribution. Good
// enough for many simple situations.
// (by Jim Reeds, Bell Labs)

class Random {
 public:
  // Creation
  Random() : x_(-1)  {}
  
  // The next pseudo random number
  int32 Next() {
    x_ += x_;
    if (x_ < 0)
      x_ ^= 0x88888EEFL;
    return x_;
  }
  
 private:
  int32 x_;
};


// ----------------------------------------------------------------------------


class Profile {
 public:
  // creation/destruction
  // - creates a new stopped profiler
  explicit Profile(Proc* proc);
  ~Profile();

  // start/stop the profiler
  // - initially, the profiler is stopped
  void Start();
  void Stop();
  bool is_started() const  { return is_started_; }
  
  // profile tick handler
  // - every HandleTick call indicates the beginning of a new
  // set of kInstrPerTick instructions (i.e., the first time
  // HandleTick is called, no instructions have been executed)
  // - HandleTick determines the wall clock difference since
  // the last tick and attributes a corresponding amount of
  // time to the currently running code
  // - the profiler must have been started
  // - HandleTick returns a random number of instructions
  // to execute before the next tick should be issued
  int HandleTick(Frame* fp, Val** sp, Instr* pc);
  
  // reset all counters to 0 and stops the profiler
  void Reset();
  
  // accessors
  struct Count {
    int top;  // number of ticks delivered when the function was on TOS
    int all;  // number of ticks delivered when the function was on the stack
     
    void Clear() {
      top = 0;
       all = 0;
    }
     
    void Add(Count* c) {
      top += c->top;
      all += c->all;
    }
  };
   
  Count* ticks_at(int i) const {
    assert(0 <= i && i < length_);
    return &ticks_[i];
  }

  int length() const  { return length_; }

  // print the raw profile, sorted by 'hottest' code interval -
  // code intervals with costs below the cutoff value will
  // not be printed (e.g., cutoff = 0.01 => code intervals
  // executed less then 1% of the time will not be printed)
  void PrintRaw(float cutoff) const;

  // print the aggregated profile, sorted by 'hottest' function -
  // functions with costs below the cutoff value will
  // not be printed (e.g., cutoff = 0.01 => functions
  // executed less then 1% of the time will not be printed)
  void PrintAggregated(float cutoff) const;
  
 private:
   Proc* proc_;  // the corresponding Proc
   Count* ticks_;  // each element corresponds to a code interval
   int length_;  // the number of ticks_ elements
   int64 last_;  // the last time HandleTick was called
   bool is_started_;  // true if profiling is started (vs stopped)
   Random rnd_;  // to compute the number of instructions before the next tick
};


}  // namespace sawzall
