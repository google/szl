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

#include <vector>


// PrintEmitter is a default Emitter implementation
// that simply prints the emitted value or even the
// entire emit statement.

class PrintEmitter: public sawzall::Emitter {
 public:
  // Flush routine type to be used in conjunction with the
  // particular Fmt used by the PrintEmitter (might be
  // file descriptor or string based Fmt).
  typedef int (*Flusher)(Fmt::State*);
  
  // If a flush routine is provided (i.e., flush != NULL), it will be
  // called after each emit, and upon destruction on the PrintEmitter.
  // Obviously, the flush routine must match the kind of Fmt f provided.
  //
  // If verbose is set, the entire emit statement (incl. table name,
  // indices, weight, and trailing semicolon) is printed followed by
  // a newline. If verbose is not set, only the element value is printed
  // without newline.
  // TODO:         in practice verbose=false silences the emitter for scalars
  //               (szl/sizl take advantage of this to silence PrintEmitters
  //                that replace unimplemented SzlEmitters for tables like
  //                recordio, text, etc that only work with scalar values)
  //                we should update PrintEmitter to silence tuple emits as well
  //                for consistency with SzlEmitter since the non-verbose
  //                feature is not currently used anywhere.
  PrintEmitter(const char* table_name, Fmt::State* f, bool verbose);
  virtual ~PrintEmitter()  { Fmt::fmtfdflush(f_);  }
   
  // Emitter interface
  virtual void Begin(sawzall::Emitter::GroupType type, int len);
  virtual void End(sawzall::Emitter::GroupType type, int len);

  virtual void PutBool(bool b);
  virtual void PutBytes(const char* p, int len);
  virtual void PutInt(int64 i);
  virtual void PutFloat(double f);
  virtual void PutFingerprint(uint64 fp);
  virtual void PutString(const char* s, int len);
  virtual void PutTime(uint64 t);

  // Shorthand emit functions
  virtual void EmitInt(int64 i);
  virtual void EmitFloat(double f);
  
  // PrintEmitter specific accessors
  Fmt::State* fmt_state() const  { return f_; }
  
 private:
  typedef struct {
    sawzall::Emitter::GroupType type;
    int field_count;
  } State;

  // State information
  const char* table_name_;
  Fmt::State* f_;  // the printing state
  bool verbose_;  // if set, the entire emit statement is printed
  enum {
    DONE, INIT,
    IN_INDEX, AFTER_INDEX,
    IN_ELEMENT, AFTER_ELEMENT,
    IN_WEIGHT, AFTER_WEIGHT
  } position_;
  State state_;  // current state, if nesting_level() >= 0
  vector<State> saved_states_;
  
  // Printing support
  int nesting_level() const;
  void P(const char* fmt, ...);
  void Prologue();
  void Epilogue();
};
