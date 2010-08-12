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

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/random_base.h"
#include "emitters/weighted-reservoir-sampler-impl.h"
#include "emitters/weighted-reservoir-sampler.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlresults.h"
#include "public/szlvalue.h"
#include "emitters/szlweightedsampleadapter.h"


// Reader for SzlWeightedSample output.
// See SzlWeightedSample::Flush for format.
class SzlWeightedSampleResults: public SzlResults {
 public:
  // factory for creating all SzlWeightedSampleResults instances.
  static SzlResults* Create(const SzlType& type, string* error) {
    return Validate(type, error) ? new SzlWeightedSampleResults(type) : NULL;
  }

  explicit SzlWeightedSampleResults(const SzlType& type)
    : maxElems_(type.param()), totElems_(0) {
  }

  // Check if the mill type is a valid instance of this table kind.
  // If not, a reason is returned in error.
  // We already know all indices are valid, as are the types for the
  // element and the weight, which is present iff it's needed.
  static bool Validate(const SzlType& type, string* error) {
    return SzlWeightedSampleAdapter::TableTypeValid(type, error);
  }

  // Retrieve the properties for this kind of table.
  static void Props(const char* kind, SzlType::TableProperties* props) {
    props->has_param = true;
    props->has_weight = true;
    props->name = kind;
  }

  // Fill in fields with the non-index fields in the result.
  // Type is valid and of the appropriate kind for this table.
  static void ElemFields(const SzlType &t, vector<SzlField>* fields) {
    AppendField(t.element(), kValueLabel, fields);
    SzlField tag_field("", SzlType::kFloat);
    AppendField(&tag_field, kTagFieldName, fields);
  }

  // Read a value string.  Returns true if string successfully decoded.
  virtual bool ParseFromString(const string& encoded)  {
    return SzlWeightedSampleAdapter::SplitEncodedStr(
        encoded, maxElems_, &elems_, &totElems_);
  }

  // Get the individual results.
  virtual const vector<string>* Results() { return &elems_; }

  // Report the total elements added to the table.
  virtual int64 TotElems() const { return totElems_; }

 private:
  int maxElems_;
  vector<string> elems_;
  int64 totElems_;
  static const char kTagFieldName[];
};

const char SzlWeightedSampleResults::kTagFieldName[] = "tag";

REGISTER_SZL_RESULTS(weightedsample, SzlWeightedSampleResults);
