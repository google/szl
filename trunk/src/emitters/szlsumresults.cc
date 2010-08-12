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
#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlresults.h"
#include "public/szlvalue.h"

// Reader for SzlSum output.
// See SzlSum::Flush for format.
class SzlSumResults: public SzlResults {
 public:
  // factory for creating all SzlSumResults instances.
  static SzlResults* Create(const SzlType& type, string* error) {
    return new SzlSumResults(type);
  }

  explicit SzlSumResults(const SzlType& type)
    : ops_(type.element()->type()), totElems_(0) {
  }

  // Check if the mill type is a valid instance of this table kind.
  // If not, a reason is returned in error.
  // We already know all indices are valid, as are the types for the
  // element and the weight, which is present iff it's needed.
  static bool Validate(const SzlType& type, string* error) {
    if (!SzlOps::IsAddable(type.element()->type())) {
      *error = "cannot sum elements of type " + type.element()->type().PPrint();
      return false;
    }
    return true;
  }

  // Retrieve the properties for this kind of table.
  static void Props(const char* kind, SzlType::TableProperties* props) {
    props->name = "sum";
    props->has_param = false;
    props->has_weight = false;
  }

  // Fill in fields with the non-index fields in the result.
  // Type is valid and of the appropriate kind for this table.
  static void ElemFields(const SzlType &t, vector<SzlField>* fields) {
    AppendField(t.element(), kValueLabel, fields);
  }

  // Read a value string.  Returns true if string successfully decoded.
  virtual bool ParseFromString(const string& val)  {
    sum_.clear();
    totElems_ = 0;

    if (val.empty())
      return true;

    SzlDecoder dec(val.data(), val.size());
    if (!dec.GetInt(&totElems_))
      return false;
    if (totElems_ <= 0)
      return false;

    unsigned const char* p = dec.position();
    if (!ops_.Skip(&dec))
      return false;
    if (!dec.done())
      return false;

    sum_.push_back(string(reinterpret_cast<const char*>(p), dec.position() - p));

    return true;
  }

  // Get the individual results.
  virtual const vector<string>* Results() { return &sum_; }

  // Report the total elements added to the table.
  virtual int64 TotElems() const { return totElems_; }

 private:
  SzlOps ops_;
  vector<string> sum_;
  int64 totElems_;
};

REGISTER_SZL_RESULTS(sum, SzlSumResults);
