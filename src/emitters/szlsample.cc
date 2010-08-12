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

// Structure for sampling some elements stored in a table.
// The technique used is:
// 1) Add a random tag for each element.
// 2) Keep the elements with the smallest tags.

// We need PRId64, which is only defined if we explicitly ask for it.
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <stdio.h>
#include <assert.h>
#include <string>
#include <vector>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/random_base.h"
#include "utilities/acmrandom.h"

#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szltabentry.h"

#include "emitters/szlheap.h"


class SzlSample: public SzlTabWriter {
 private:
  explicit SzlSample(const SzlType& type)
    : SzlTabWriter(type, true, false)  { }
  ~SzlSample()  { }
  class SzlSampleEntry;

 public:
  static SzlTabWriter* Create(const SzlType& type, string* error) {
    return new SzlSample(type);
  }

  virtual SzlTabEntry* CreateEntry(const string& index) const {
    return new SzlSampleEntry(weight_ops(), param());
  }

 private:
  class SzlSampleEntry: public SzlTabEntry {
   public:
    // Note that these will be correlated across tasks.  Perhaps better
    // would be an interface that allows the user to set the seeds.
    SzlSampleEntry(const SzlOps& weight_ops, int param)
      : cmp_(&weight_ops), heap_(weight_ops, cmp(), param),
        random_(SzlACMRandom::HostnamePidTimeSeed())  { }

    virtual int AddElem(const string& elem);
    virtual void Flush(string* output);
    virtual void FlushForDisplay(vector<string>* output);
    virtual SzlTabEntry::MergeStatus Merge(const string& val);

    virtual void Clear() {
      tot_elems_ = 0;
      heap_.Clear();
    }
    
    virtual int Memory()  { return sizeof(SzlSampleEntry) + heap_.Memory(); }
    virtual int64 TotElems() const { return tot_elems_; }
    virtual int TupleCount()  { return heap_.nElems(); }

    const SzlValueLess* cmp() const  { return &cmp_; }

   private:
    // The comparison we want for our heap.
    const SzlValueLess cmp_;

    // Heap of samples.
    SzlHeap heap_;

    // Random number generator.
    // TODO: Consider MT.
    SzlACMRandom random_;
  };
};


REGISTER_SZL_TAB_WRITER(sample, SzlSample);


// Assign a random tag to an element, and add it to the heap.
// The heap might throw it away in the event of overflow.
int SzlSample::SzlSampleEntry::AddElem(const string& elem) {
  tot_elems_++;
  return heap_.AddElem(elem, SzlValue(static_cast<int64>(random_.Next())));
}


// Produce the encoded string that represents the data in this entry. This
// value is used for merge operations as it contains all information
// needed for the merge.
void SzlSample::SzlSampleEntry::Flush(string* output) {
  if (heap_.nElems() == 0) {
    output->clear();
    return;
  }
  SzlEncoder enc;
  enc.PutInt(TotElems() - heap_.nElems());
  enc.PutInt(heap_.nElems());
  for (int i = 0; i < heap_.nElems(); ++i) {
    const SzlHeap::Elem* e = heap_.Element(i);
    enc.PutBytes(e->value.data(), e->value.size());
  }
  enc.Swap(output);
  Clear();
}


// Get the encoded string representation of this entry for display
// purposes. This value doesn't have things like an additional count of
// elements with it.
void SzlSample::SzlSampleEntry::FlushForDisplay(vector<string>* output) {
  output->clear();
  if (heap_.nElems() == 0) {
    output->push_back("");
    return;
  }

  for (int i = 0; i < heap_.nElems(); ++i) {
    const SzlHeap::Elem* e = heap_.Element(i);
    output->push_back(e->value);
  }
}


// Merge a flushed state into the current state.
// Returns whether the string looked like a valid SzlSampleEntry state.
SzlTabEntry::MergeStatus SzlSample::SzlSampleEntry::Merge(const string& val) {
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
    if (!dec.Skip(SzlType::BYTES)) {
      fprintf(stderr, "Less values:%d encoded than nvals:%"PRId64"\n", i, nvals);
      return MergeError;
    }
  }
  if (!dec.done()) {
    fprintf(stderr, "More values than nvals:%"PRId64"\n", nvals);
    return MergeError;
  }

  // Now we know the string is ok, sample all of its elements.
  dec.Restart();

  for (int i = 0; i < 2; i++) {
    if (!dec.Skip(SzlType::INT))
      return MergeError;
  }

  for (int i = 0; i < nvals; ++i) {
    string s;
    if (!dec.GetBytes(&s))
      return MergeError;
    AddElem(s);
  }
  tot_elems_ += extra;
  return MergeOk;
}
