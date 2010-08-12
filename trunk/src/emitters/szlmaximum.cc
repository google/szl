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

// Implementation of SzlTabWriter and SzlTabEntry for both "maximum" and
// "minimum" tables in Sawzall.

#include <string>
#include <vector>

#include "public/porting.h"
#include "public/logging.h"

#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/szldecoder.h"
#include "public/szlencoder.h"
#include "public/szltabentry.h"

#include "emitters/szlheap.h"


// Keep the biggest (or smallest) weighted elements.
class SzlMaximum: public SzlTabWriter {
 private:
  explicit SzlMaximum(const SzlType& type)
    : SzlTabWriter(type, true, false),
      cmp_(type.table() == "maximum"
           ? static_cast<SzlValueCmp*>(new SzlValueLess(&weight_ops()))
         : static_cast<SzlValueCmp*>(new SzlValueGreater(&weight_ops())))  { }
  virtual ~SzlMaximum()  { delete cmp_; }
  class SzlMaximumEntry;

 public:
  static SzlTabWriter* Create(const SzlType& type, string* error) {
    if (type.weight() == NULL)
      LOG(ERROR) << "Internal error - maximum/minimum without weight";
    if (!SzlOps::IsOrdered(type.weight()->type())) {
      *error = "can't compare weights";
      return NULL;
    }
    return new SzlMaximum(type);
  }

  virtual SzlTabEntry* CreateEntry(const string& index) const {
    return new SzlMaximumEntry(weight_ops(), param(), cmp());
  }

  // Accessors
  const SzlValueCmp* cmp() const { return cmp_; }

 private:
  // The comparison we want for our heap.
  SzlValueCmp* cmp_;

  class SzlMaximumEntry : public SzlTabEntry {
   public:
    SzlMaximumEntry(const SzlOps& weight_ops, int param, const SzlValueCmp* cmp)
      : weight_ops_(weight_ops), heap_(weight_ops, cmp, param)  { }

    virtual int AddElem(const string& elem) {
      return AddWeightedElem(elem, SzlValue(static_cast<int64>(1)));
    }

    virtual int AddWeightedElem(const string& elem, const SzlValue& weight) {
      ++tot_elems_;
      return heap_.AddElem(elem, weight);
    }

    virtual void Flush(string* output);
    virtual void FlushForDisplay(vector<string>* output);
    virtual SzlTabEntry::MergeStatus Merge(const string& val);

    virtual void Clear() {
      tot_elems_ = 0;
      heap_.Clear();
    }

    virtual int Memory() {
      return sizeof(SzlMaximumEntry) + heap_.Memory() + sizeof(SzlValueCmp);
    }

    virtual int64 TotElems() const { return tot_elems_; }
    virtual int TupleCount() { return heap_.nElems(); }

    const SzlOps& weight_ops() const  { return weight_ops_; }

   private:
    const SzlOps& weight_ops_;

    // Structure for keeping track of the current biggest (or smallest) elements.
    // TODO: Add an iterator for sorted output to improve
    // FlushForDisplay performance.
    SzlHeap heap_;

    // Total elements ever added to the table.
    int64 tot_elems_;
  };
};


// Register for both minimum and maximum tables.
REGISTER_SZL_TAB_WRITER(maximum, SzlMaximum);
REGISTER_SZL_TAB_WRITER(minimum, SzlMaximum);


void SzlMaximum::SzlMaximumEntry::Flush(string* output) {
  if (heap_.nElems() == 0) {
    output->clear();
    return;
  }
  // Combine all of the counts and tags into a single sorted string.
  SzlEncoder enc;
  enc.PutInt(tot_elems_ - heap_.nElems());
  enc.PutInt(heap_.nElems());

  // ReHeap is necessary after the Sort.
  heap_.Sort();
  for (int i = 0; i < heap_.nElems(); ++i) {
    const SzlHeap::Elem* elem = heap_.Element(i);
    enc.PutBytes(elem->value.data(), elem->value.size());
    weight_ops().Encode(elem->weight, &enc);
  }

  enc.Swap(output);
  Clear();
}


void SzlMaximum::SzlMaximumEntry::FlushForDisplay(vector<string>* output) {
  output->clear();
  if (heap_.nElems() == 0) {
    output->push_back("");
    return;
  }

  // ReHeap is necessary after the sort.
  heap_.Sort();
  for (int i = 0; i < heap_.nElems(); ++i) {
    // TODO: check this and similar cases.
    // Encoding code and decoding code does not match because "elem->value" is
    // already SzlEncode'ed. SzlEncoder string handling relies on '\0' and
    // does not allow more than one wrapping.
    SzlEncoder enc;
    const SzlHeap::Elem* elem = heap_.Element(i);
    weight_ops().Encode(elem->weight, &enc);
    string encoded;
    enc.Swap(&encoded);
    output->push_back(elem->value + encoded);
  }
  heap_.ReHeap();
}


SzlTabEntry::MergeStatus SzlMaximum::SzlMaximumEntry::Merge(const string& val) {
  if (val.empty())
    return MergeOk;

  SzlDecoder dec(val.data(), val.size());
  int64 extra;
  if (!dec.GetInt(&extra))
    return MergeError;
  int64 nvals;
  if (!dec.GetInt(&nvals))
    return MergeError;

  // Check for consistent params.
  if (nvals > heap_.maxElems() || (nvals < heap_.maxElems() && extra != 0))
    return MergeError;

  // Check input validity.
  for (int i = 0; i < nvals; ++i) {
    if (!dec.Skip(SzlType::BYTES) || !weight_ops().Skip(&dec))
      return MergeError;
  }
  if (!dec.done())
    return MergeError;

  // Now we know the string is ok, sample all of its elements.
  dec.Restart();
  if (!dec.Skip(SzlType::INT))
    return MergeError;
  if (!dec.Skip(SzlType::INT))
    return MergeError;
  SzlValue w;
  for (int i = 0; i < nvals; ++i) {
    string s;
    if (!dec.GetBytes(&s))
      return MergeError;
    if (!weight_ops().Decode(&dec, &w))
      return MergeError;
    AddWeightedElem(s, w);
  }
  weight_ops().Clear(&w);

  tot_elems_ += extra;

  return MergeOk;
}
