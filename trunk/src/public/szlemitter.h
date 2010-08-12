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

// Emitter that will produce vector of key-value pairs for data
// returned by Sawzall scripts. Collects output from execution of a szl
// program and returns the resulting table in the form of a vector of
// key-value pairs. Keys and values are returned in the encoded format used by
// SzlEncoder/SzlDecoder.

#include <utility>
#include <string>
#include <vector>

#include "public/hash_map.h"


class SzlType;
union SzlValue;
class SzlTabEntry;
class SzlTabWriter;
class SzlEncoder;

// Creates an emitter that represents a single table when executing szl
// scripts. The emitter is responsible for receiving output and assigning it
// to the correct entries for aggregation as well as for outputting stored data
// in a standard format.
class SzlEmitter : public sawzall::Emitter {
 public:
  typedef pair<string, string> KeyValuePair;
  typedef hash_map<string, SzlTabEntry*> SzlTabEntryMap;

  // Creates a SzlEmitter belonging to a specific user and job with a name
  // used to reference it later. The emitter represents a table with the
  // provided SzlType and uses the provided writer to create entries that
  // function accordingly.  This takes ownership of the writer and will
  // deallocate it.
  SzlEmitter(const string& name, const SzlTabWriter* writer, bool display);

  virtual ~SzlEmitter();

  // Implementations of the Emitter interface.

  // Signals the start of a group of elements or an entry.
  void Begin(GroupType type, int len);

  // Signals the end of a group of elements or an entry.
  void End(GroupType type, int len);

  // Signals to add a boolean object to the current list of elements.
  void PutBool(bool b);

  // Signals to add a byte object to the current list of elements.
  void PutBytes(const char* p, int len);

  // Signals to add an integer object to the current list of elements.
  void PutInt(int64 i);

  // Signals to add a float object to the current list of elements.
  void PutFloat(double f);

  // Signals to add a fingerprint object to the current list of elements.
  void PutFingerprint(uint64 fp);

  // Signals to add a string object to the current list of elements.
  void PutString(const char* s, int len);

  // Signals to add a time object to the current list of elements.
  void PutTime(uint64 t);

  // Shorthand emit functions.
  void EmitInt(int64 i);
  void EmitFloat(double f);

  // Allow merging in data from other emitters so that an emitter can be
  // reconstructed given the proper metadata.
  bool Merge(const string& index, const string& val);

  // Diplays the results in the table.
  void  DisplayResults();

  // Flush current results and clear the storage.
  void Flusher();

  // Clear the table in the emitter.
  void Clear();

  // Return true if any errors occurred during table processing.
  bool ErrorsDetected() const { return errors_detected_; }

  // Returns a count of the number of rows being displayed in the tables.
  int GetTupleCount() const;

  // Returns a count of the memory used by the table.
  int GetMemoryUsage() const;

  // Returns an estimate of the memory used by the table.
  int GetMemoryEstimate() const  { return memory_estimate_; }

  // Return the name of the table.
  string name() const { return name_; }

  SzlOps* weight_ops() { return &weight_ops_; }

 protected:
  // Write a single value to the map output.
  // This version prints the output on stdout; override it
  // to write to map output when using mapreduce.
  virtual void WriteValue(const string& key, const string& value);

  // Factory for producing SzlTableEntries.
  const SzlTabWriter* writer_;

  // SzlOps to perform operations on weight data as requested.
  SzlOps weight_ops_;

  // Encoders used for translating data into an encoded string rep.
  SzlEncoder *key_;
  SzlEncoder *value_;
  SzlEncoder* encoder_;

  // Table that contains all the entries that this emitter has seen.
  SzlTabEntryMap* table_;

  string name_;

  // Estimated memory used.
  int memory_estimate_;

  // Used to track whether the results of the table will be displayed or not.
  bool display_;

  // State variables for the emitter.

  // depth_ refers to how many levels deep the element being added will go. For
  // example, an new emit initially starts out with depth 0, and increments to
  // depth 1 with the opening emit, possibly to depth 2 with the opening of an
  // array, and so on. Depth is reversed when closing the matching openings,
  // so the end of an emit restores depth to 0.
  int depth_;

  // Keeps track of whether the current element being read is meant to be part
  // of the weight rather than an actual element.
  bool in_weight_;

  // Used to keep track of the depth when an array is initially started. This
  // is used to double check to ensure that when an array is closed, the depth_
  // field is back to what it was when the array was opened.
  vector<int> arrays_;

  // The weights for the entry being emitted.
  SzlValue *weight_;

  // The position in the weight (which may consist of more than one element).
  int weight_pos_;

  // Set to true if any of the operations that are being performed cause an
  // error.
  bool errors_detected_;
};
