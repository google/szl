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


// Implement line counting for Procs, including the array of counters
// and reporting the counts
class LineCount {
 public:
  LineCount(Proc* proc);
  ~LineCount();

  int size() { return counters_size_; }

  void AllocCounters(int len);
  void ResetCounters();

  void IncCounter(int n) {  // called in byte-compiled and native mode
    if (counters_ != NULL && n >= 0 && n < counters_size_)
      counters_[n]++;
  }

  int64 counter_at(int n) const { return counters_[n]; }
  void set_emitter(Emitter* emitter) { emitter_ = emitter; }
  Emitter* emitter() const { return emitter_; }

  // emit the line counts
  void Emit(const char* source);  

 private:
  Proc* proc_;
  int64* counters_;
  int counters_size_;
  Emitter* emitter_;

  // We want to write the program source, which tends to be large, only
  // once in this unix process.  The following is a heuristic which
  // will do that in all current uses of szl.  It does extra writing
  // if there are several separate szl threads using different output,
  // as it is only remembering information about one source file.  it
  // fails if there are separate szl threads with different input and
  // output, but using the same source.  It's obvious that this data
  // should be stored with the overall control of the szl thread, but
  // it's not so easy to do that, hence the hack.
  static uint64 last_src_fingerprint_;

  // Prevent construction, force factory method
  LineCount() { /* nothing to do */ }
};


}  // namespace sawzall
