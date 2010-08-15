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

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlresults.h"
#include "public/szlvalue.h"

// Reader for SzlMaximum output.
// See SzlMaximum::Flush for format.
class SzlMaximumResults: public SzlResults {
 public:
  // factory for creating all SzlMaximumResults instances.
  static SzlResults* Create(const SzlType& type, string* error) {
    return new SzlMaximumResults(type);
  }

  explicit SzlMaximumResults(const SzlType& type)
    : ops_(type.weight() != NULL ? type.weight()->type() : SzlType::kInt),
      maxElems_(type.param()),
      totElems_(0) {
  }

  // Check if the mill type is a valid instance of this table kind.
  // If not, a reason is returned in error.
  // We already know all indices are valid, as are the types for the
  // element and the weight, which is present iff it's needed.
  static bool Validate(const SzlType& type, string* error) {
    if (!SzlOps::IsOrdered(type.weight()->type())) {
      *error = "can't compare weights";
      return false;
    }
    return true;
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
    AppendField(t.weight(), kWeightLabel, fields);
  }

  // Read a value string.  Returns true if string successfully decoded.
  virtual bool ParseFromString(const string& val)  {
    elems_.clear();
    totElems_ = 0;

    if (val.empty())
      return true;

    SzlDecoder dec(val.data(), val.size());
    int64 extra;
    if (!dec.GetInt(&extra))
      return false;
    int64 nvals;
    if (!dec.GetInt(&nvals))
      return false;

    // check for consistent params
    if (nvals > maxElems_ || (nvals < maxElems_ && extra != 0))
      return false;

    // first check string for validity
    for (int i = 0; i < nvals; ++i) {
      if (!dec.Skip(SzlType::BYTES) || !ops_.Skip(&dec))
        return false;
    }
    if (!dec.done())
      return false;

    // now that we know it the string is ok, sample all of its elements.
    dec.Restart();
    CHECK(dec.Skip(SzlType::INT));
    CHECK(dec.Skip(SzlType::INT));
    for (int i = 0; i < nvals; ++i) {
      string s;
      CHECK(dec.GetBytes(&s));

      // Need to combine the value and weight into one szl encoded string.
      // The weight is already encoded; just copy it.
      unsigned const char* p = dec.position();
      CHECK(ops_.Skip(&dec));
      s += string(reinterpret_cast<const char*>(p), dec.position() - p);

      elems_.push_back(s);
    }

    totElems_ = extra + nvals;

    return true;
  }

  // Get the individual results.
  virtual const vector<string>* Results() { return &elems_; }

  // Report the total elements added to the table.
  virtual int64 TotElems() const { return totElems_; }

 private:
  SzlOps ops_;
  vector<string> elems_;
  int maxElems_;
  int64 totElems_;
};

REGISTER_SZL_RESULTS(maximum, SzlMaximumResults);
REGISTER_SZL_RESULTS(minimum, SzlMaximumResults);
