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

// Structure for sampling some elements with weights (see
// util/random/weighted-reservoir-sampler.h for algorithm description).

#include <assert.h>
#include <string>
#include <vector>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/random_base.h"
#include "utilities/mt_random.h"

#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/szltabentry.h"

#include "emitters/weighted-reservoir-sampler-impl.h"
#include "emitters/weighted-reservoir-sampler.h"
#include "emitters/szlweightedsampleadapter.h"


class SzlWeightedSample : public SzlTabWriter {
 private:
  explicit SzlWeightedSample(const SzlType& type)
    : SzlTabWriter(type, true, false)  { }
  virtual ~SzlWeightedSample()  { }

 public:
  static SzlTabWriter* Create(const SzlType& type, string* error) {
    return SzlWeightedSampleAdapter::TableTypeValid(type, error) ?
        new SzlWeightedSample(type) : NULL;
  }

  virtual SzlTabEntry* CreateEntry(const string& index) const {
    return new SzlWeightedSampleEntry(weight_ops(), param(), &random_);
  }

 private:
  // Random number generator instance shared among all entries created by this
  // writer. Because getting a random number is a mutable method to this
  // instance, the entries' AddElem and AddWeightedElem are not thread-safe.
  mutable MTRandom random_;

  class SzlWeightedSampleEntry : public SzlTabEntry {
   public:
    SzlWeightedSampleEntry(const SzlOps& weight_ops, int param,
                           RandomBase* random)
      : sampler_(weight_ops, param, random)  { }
    virtual ~SzlWeightedSampleEntry()  { }

    // Not thread-safe; see above.
    virtual int AddElem(const string& elem)  { return sampler_.AddElem(elem); }

    // Add a new weighted element to this entry in the table.
    // Not thread-safe; see above.
    virtual int AddWeightedElem(const string& elem, const SzlValue& weight) {
      return sampler_.AddWeightedElem(elem, weight);
    }

    virtual void Flush(string* output) {
      sampler_.Encode(output);
      Clear();
    }

    virtual void FlushForDisplay(vector<string>* output) {
      sampler_.EncodeForDisplay(output);
    }

    virtual SzlTabEntry::MergeStatus Merge(const string& val) {
      return sampler_.Merge(val) ? MergeOk : MergeError;
    }

    // Report the total elements added to this entry in the table.
    virtual int64 TotElems() const  { return sampler_.TotElems(); }

    virtual void Clear()  { sampler_.Clear(); }
    virtual int Memory()  { return sizeof(*this) + sampler_.ExtraMemory(); }
    virtual int TupleCount()  { return sampler_.nElems(); }

   private:
    // The underlying structure that manages the samples.
    SzlWeightedSampleAdapter sampler_;
  };
};


REGISTER_SZL_TAB_WRITER(weightedsample, SzlWeightedSample);
