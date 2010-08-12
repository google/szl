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

#include <string>
#include <vector>

#include "public/szltype.h"
#include "public/szlvalue.h"

class SzlTabWriter;


// Abstract class representing an entry in a table. Each entry has the
// ability to add more data to itself based on what type of aggregation
// or collection it performs.
class SzlTabEntry {
 protected:
  // Force use of SzlTabWriter::CreateEntry.
  SzlTabEntry() : tot_elems_(0)  { }

 public:
  virtual ~SzlTabEntry()  { }

  // Merge result message.
  enum MergeStatus {
    MergeOk,              // Merge succeeded and more values can be added.
    MergeError,           // The merge operation failed.
  };

  // Add a new element to this entry.
  // If not overridden, not allowed for  this table type.
  virtual int AddElem(const string& elem) {
    LOG(FATAL) << "Call to AddElem() for a table that does not support it.";
    return 0;
  }

  // Add a new weighted element to this entry in the table. 
  // If not overridden, not allowed for  this table type.
  virtual int AddWeightedElem(const string& elem, const SzlValue& weight) {
    LOG(FATAL) << "Call to AddWeightedElem() for a table that does not support it.";
    return 0;
  }

  // Produce the encoded string that represents the data in this entry.
  // This value may be used for merge operations as it contains all information
  // needed for the merge.
  virtual void Flush(string* output) {
    LOG(FATAL) << "Call to Flush() for a table that does not support it.";
  }

  // Get the encoded string representation of this entry for display
  // purposes. This value doesn't have things like an additional count of
  // elements with it.
  virtual void FlushForDisplay(vector<string>* output) {
    LOG(FATAL) <<
      "Call to FlushForDisplay() for a table that does not support it.";
  }

  // Merge two table entries by getting the encoded value for one and
  // putting it into this table entry.
  virtual MergeStatus Merge(const string& val) {
    LOG(FATAL) << "Call to Merge() for a table that does not support it.";
    return MergeError;
  }

  // Write a value to a direct output table.
  virtual void Write(const string& val) {
    LOG(FATAL) << "Call to Write() for a table that does not support it.";
  }

  // Clear the value of this entry; if not overridden, does nothing.
  virtual void Clear()  { }

  // Get the amount of memory used by this entry in the table.
  virtual int Memory() = 0;

  // Get the number of elements added to this entry in the table.
  int64 TotElems() const { return tot_elems_; }

  // Get the number of rows stored in this table.
  virtual int TupleCount()  {
    LOG(FATAL) << "Call to TupleCount() for a table that does not support it.";
    return 0;
  }

 protected:
  // Total elements added to this entry in the sum table.
  int64 tot_elems_;
};


// Abstract class for a writer that creates/modifies table entries. This
// writer keeps track of whether or not aggregation or filtering is needed.
class SzlTabWriter {
 public:
 protected:
  // Force use of CreateSzlTabWriter.
  SzlTabWriter(const SzlType& type, bool aggregates, bool filters)
      : param_(type.param()),
        has_indices_(type.indices_size() != 0),
        has_weight_(type.has_weight()),
        aggregates_(aggregates),
        filters_(filters),
        element_ops_(type.element()->type()),
        weight_ops_(has_weight_ ? type.weight()->type() : SzlType::kInt) {
    one_.i = 1;
  }

 public:
  virtual ~SzlTabWriter() { }

  static SzlTabWriter* CreateSzlTabWriter(const SzlType& type, string* error);

  // Attributes set from constructor.
  int param() const  { return param_; };
  bool HasIndices() const  { return has_indices_; }
  bool HasWeight() const  { return has_weight_; }
  bool Aggregates() const  { return aggregates_; }
  bool Filters() const  { return filters_; }

  virtual bool IsMrCounter() const  { return false; }

  // Does this type of table write to the mill?  (Most do.)
  // If not, it generates results directly into a file.
  virtual bool WritesToMill() const  { return true; }

  // Get the operations.
  virtual const SzlOps& element_ops() const  { return element_ops_; }
  virtual const SzlOps& weight_ops() const  { return weight_ops_; }

  // If Filters(), the key filtering function.
  // Returns the filtered results in fkey and fvalue,
  // and an output shard fingerprint in shardfp,
  // which determines the output shard modulo the number of shards.
  // Note: shardfp is ignored if the table has no indices,
  // in which case the sharding is determined by the caller.
  virtual void FilterKey(const string& key,
                         string* fkey, uint64* shardfp) const {
    LOG(FATAL) <<
      "Call to FilterKey() for a table that does not support it.";
  }

  // If Filters(), the value filtering function.
  virtual void FilterValue(const string& value, string* fvalue) const {
    LOG(FATAL) <<
      "Call to FilterValue() for a table that does not support it.";
  }

  // Function to create new table entries for a specified index string.
  // The SzlTabWriter must exist for the entire lifetime of the SzlTabEntry.
  // Caller has the ownership of the created entry.
  virtual SzlTabEntry* CreateEntry(const string& index) const = 0;

  // If the table writes to directly to a file, this function is called
  // to create the file.
  virtual void CreateOutput(const string& filename) {
    LOG(FATAL) <<
      "Call to CreateOutput() for a table that does not support it.";
  }

  // Sets the random seed for the current table. This is a noop for
  // most tables, but tables that need randomness should reseed their
  // RNGs when this is called, in order to produce repeatable results
  // when MR shards are retried. A table can either use a MTRandom
  // with a string seed, or an ACMRandom with a 32-bit seed obtained
  // by hashing the string. See szlsample.cc for an example.
  virtual void SetRandomSeed(const string& seed) { }

 protected:
  const int param_;
  const bool has_indices_;
  const bool has_weight_;
  const bool aggregates_;
  const bool filters_;
  const SzlOps element_ops_;  // element operations
  const SzlOps weight_ops_;   // weight operations
  SzlValue one_;         // integer 1 for default weight
};


typedef SzlTabWriter* (*SzlTabWriterCreator)(const SzlType&, string*);
// Allow registration of table writers.
class SzlTabWriterRegisterer {
 public:
  SzlTabWriterRegisterer(const char* kind, SzlTabWriterCreator creator);
};


#define REGISTER_SZL_TAB_WRITER(kind, name) \
static SzlTabWriterRegisterer __szl_ ## kind ## _writer__creator(#kind, \
                                     &name::Create)
