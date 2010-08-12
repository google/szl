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

class Histogram {
 public:
  // creation
  static Histogram* New(Proc* proc);
  
  // counting of opcodes
  void Count(Opcode op) {
    assert(0 <= op && op < number_of_opcodes);
    counts_[op]++;
  }
  
  // collect (add) the counts of another histogram to this one
  void Collect(Histogram* histo);

  // the total number of opcodes counted
  typedef int64 Counter;
  Counter TotalCount() const;
  
  // reset all counters to 0
  void Reset();
  
  // print the histogram, sorted by most frequent opcode
  // opcodes with frequencies below the cutoff value
  // will not be printed (e.g., cutoff = 0.01 => opcodes
  // used less then 1% of the time will not be printed)
  void Print(float cutoff) const;
  
 private:
  Proc* proc_;
  Counter counts_[number_of_opcodes];
};

}  // namespace sawzall
