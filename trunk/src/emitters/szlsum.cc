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

// Implementation of SzlTabWriter and SzlTabEntry for sum tables in Sawzall.
// Each SumTabEntry holds the current sum for a single key and as more
// additions are made, it will increment the sum accordingly. Returns
// a single value at the end that corresponds to the final sum.

#include <stdio.h>
#include <string>
#include <vector>

#include "public/porting.h"
#include "public/logging.h"

#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/szldecoder.h"
#include "public/szlencoder.h"
#include "public/szltabentry.h"


// Implementation of sum table objects.

class SzlSum : public SzlTabWriter {
 private:
  explicit SzlSum(const SzlType& type)
    : SzlTabWriter(type, true, false)  { }
  virtual ~SzlSum()  { }
  class SzlSumEntry;

 public:
  static SzlTabWriter* Create(const SzlType& type, string* error) {
    if (!SzlOps::IsAddable(type.element()->type())) {
      *error = "can't add elements of type " + type.KindName(type.kind());
      return NULL;
    }
    return new SzlSum(type);
  }

  virtual SzlTabEntry* CreateEntry(const string& index) const {
    return new SzlSumEntry(element_ops());
  }

private:
  class SzlSumEntry : public SzlTabEntry {
   public:
    explicit SzlSumEntry(const SzlOps& element_ops)
        : element_ops_(element_ops)  { Clear(); }
    virtual ~SzlSumEntry()  { Clear(); }

    virtual int AddElem(const string& elem);
    virtual void Flush(string* output) ;
    virtual void FlushForDisplay(vector<string>* output);
    virtual SzlTabEntry::MergeStatus Merge(const string& val);

    virtual void Clear() {
      tot_elems_ = 0;
      element_ops().AssignZero(&sum_);
      element_ops().Clear(&sum_);
      memory_ = 0;
    }

    // Calculates current memory usage.
    virtual int Memory() {
      return sizeof(SzlSumEntry) + element_ops().Memory(sum_);
    }

    // Calculates current tuple count.
    virtual int TupleCount()  { return 1; }

    const SzlOps& element_ops() const  { return element_ops_; }

   private:
    const SzlOps& element_ops_;
    // Sum of all elements.
    SzlValue sum_;
    // The memory consumed by sum_.
    int64 memory_;
  };
};


REGISTER_SZL_TAB_WRITER(sum, SzlSum);


int SzlSum::SzlSumEntry::AddElem(const string& elem) {
  tot_elems_++;
  if (tot_elems_ == 1) {
    CHECK(element_ops().ParseFromArray(elem.data(), elem.size(), &sum_));
    memory_ = element_ops().Memory(sum_);
    return memory_ - sizeof(SzlValue);
  } else {
    SzlValue elemv;
    CHECK(element_ops().ParseFromArray(elem.data(), elem.size(), &elemv));
    element_ops().Add(elemv, &sum_);
    element_ops().Clear(&elemv);
    int64 mem = memory_;
    memory_ = element_ops().Memory(sum_);
    return memory_ - mem;
  }
}


void SzlSum::SzlSumEntry::Flush(string* output) {
  if (tot_elems_ == 0) {
    output->clear();
    return;
  }

  SzlEncoder enc;
  enc.PutInt(tot_elems_);
  element_ops().Encode(sum_, &enc);
  enc.Swap(output);

  Clear();
}


void SzlSum::SzlSumEntry::FlushForDisplay(vector<string>* output) {
  output->clear();
  if (tot_elems_ == 0) {
    output->push_back("");
    return;
  }

  SzlEncoder enc;
  element_ops().Encode(sum_, &enc);
  string encoded;
  enc.Swap(&encoded);
  output->push_back(encoded);
}


// Merge an encoded string into the current sum.
SzlTabEntry::MergeStatus SzlSum::SzlSumEntry::Merge(const string& val) {
  if (val.empty())
    return MergeOk;

  SzlDecoder dec(val.data(), val.size());
  int64 extra;
  if (!dec.GetInt(&extra)) {
    return MergeError;
  }
  if (extra <= 0) {
    return MergeError;
  }
  SzlValue sum;
  if (!element_ops().Decode(&dec, &sum)) {
    return MergeError;
  }
  if (!dec.done()) {
    element_ops().Clear(&sum);
    return MergeError;
  }

  element_ops().Add(sum, &sum_);
  tot_elems_ += extra;
  element_ops().Clear(&sum);

  return MergeOk;
}
