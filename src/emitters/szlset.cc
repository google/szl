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

// Structure for keeping a set of elements.
// The N is the maximum number of elements we will report.

#include <set>
#include <string>
#include <vector>

#include "public/porting.h"
#include "public/logging.h"

#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/szldecoder.h"
#include "public/szlencoder.h"
#include "public/szltabentry.h"


class SzlSet: public SzlTabWriter {
 private:
  explicit SzlSet(const SzlType& type)
    : SzlTabWriter(type, true, false)  { }
  ~SzlSet()  { }
  class SzlSetEntry;

 public:
  static SzlTabWriter* Create(const SzlType& type, string* error) {
    return new SzlSet(type);
  }

  virtual SzlTabEntry* CreateEntry(const string& index) const {
    return new SzlSetEntry(element_ops(), param());
  }

 private:
  class SzlSetEntry: public SzlTabEntry {
   public:
    explicit SzlSetEntry(const SzlOps& element_ops, int param)
      : element_ops_(element_ops), maxElems_(param)  { }

    virtual int AddElem(const string& elem) {
      tot_elems_++;
      // Do not add if we are already full.
      if (set_.size() > maxElems_)
        return 0;
      if (set_.insert(elem).second)
        return kNodeSize + elem.size();
      return 0;
    }

    virtual void Flush(string* output);
    virtual void FlushForDisplay(vector<string>* output);
    virtual SzlTabEntry::MergeStatus Merge(const string& val);

    virtual void Clear() {
      tot_elems_ = 0;
      set_.clear();
    }

    virtual int Memory() {
      int memory = sizeof(SzlSetEntry) + (1 + set_.size()) * kNodeSize;
      for (set<string>::const_iterator i = set_.begin(); i != set_.end(); ++i) {
        memory += i->size();
      }
      return memory;
    }

    virtual int TupleCount()  { return set_.size(); }

    const SzlOps& element_ops() const  { return element_ops_; }

   private:
    const SzlOps& element_ops_;

    // <set> has fat nodes (bool, 3 ptrs, value) and might need better
    // implementation.  A valid SzlSetEntry can hold up to maxElems_
    // elements, but internally, set_ may hold up to maxElems_ + 1 when
    // the set overflows.  An overflowed set is ignored by the
    // Flush methods.
    set<string> set_;

    // Maximum number of elements allowed.
    const int maxElems_;

    static const int kNodeSize = 4 * sizeof(void*) + sizeof(string);
  };
};



REGISTER_SZL_TAB_WRITER(set, SzlSet);


// Produce the encoded string that represents the data in this entry. This
// value is used for merge operations as it contains all information
// needed for the merge.
void SzlSet::SzlSetEntry::Flush(string* output) {
  // Ignore sets with more than the maximum allowed number of elements.
  if (set_.size() > maxElems_ || set_.size() == 0) {
    output->clear();
    return;
  }
  SzlEncoder enc;
  enc.PutInt(tot_elems_ - set_.size());
  enc.PutInt(set_.size());
  enc.Swap(output);
  for (set<string>::const_iterator i = set_.begin(); i != set_.end(); ++i) {
    // Entries are already encoded, therefore no need to encode or delimit.
    output->append(*i);
  }
  Clear();
}


// Get an encoded string representation for each entry for display
// purposes. This value doesn't have count of elements etc.
void SzlSet::SzlSetEntry::FlushForDisplay(vector<string>* output) {
  output->clear();
  // Ignore sets with more than the maximum allowed number of elements.
  if (set_.size() > maxElems_)
    return;

  if (set_.size() == 0) {
    output->push_back("");
    return;
  }
 
  for (set<string>::const_iterator i = set_.begin(); i != set_.end(); ++i) {
    // Entries are already encoded, therefore no need to encode or delimit.
    output->push_back(*i);
  }
}


// Merge an encoded state into the current state.
// Returns whether the string looked like a valid SzlSetEntry state.
SzlTabEntry::MergeStatus SzlSet::SzlSetEntry::Merge(const string& val) {
  if (val.empty())
    return MergeOk;

  SzlDecoder dec(val.data(), val.size());
  int64 extra;
  if (!dec.GetInt(&extra))
    return MergeError;
  int64 nvals;
  if (!dec.GetInt(&nvals))
    return MergeError;

  // Pick up each element.
  // They are SzlEncoded values of our set's element type.
  // We want to leave them in their encoded format,
  // but use SzlValue's ability to parse instances of complex SzlTypes.
  for (int i = 0; i < nvals; i++) {
    // Record the starting position of the next value.
    unsigned const char* p = dec.position();

    // Skip past it.
    if (!element_ops().Skip(&dec))
      return MergeError;

    // Now we know the end of the encoded value.
    // Since success of Skip above implies a valid string,
    // add the entire encoded value.
    AddElem(string(reinterpret_cast<const char*>(p), dec.position() - p));
  }

  tot_elems_ += extra;
  return MergeOk;
}
