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

// Parses the Boot Sum table result for mill output, see
// sawbootstrapsum.cc for details of the table.

#include <string>
#include <vector>

#include "public/porting.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlresults.h"
#include "public/szltype.h"
#include "public/szlvalue.h"

// Reader for SzlBootstrapsum output.
// See SzlBootstrapsum::Flush for format.
class SzlBootstrapsumResults: public SzlResults {
 public:
  static SzlResults* Create(const SzlType& type, string* error) {
    return new SzlBootstrapsumResults(type);
  }

  explicit SzlBootstrapsumResults(const SzlType& type)
      : ops_(type.element()->type()),
        numSamples_(type.param()),
        total_elements_(0) {
  }

  static bool Validate(const SzlType& type, string* error) {
    if (!SzlOps::IsNumeric(type.element()->type())) {
      *error = "element must be an int, float, or tuple thereof";
      return false;
    }
    return true;
  }

  static void Props(const char* kind, SzlType::TableProperties* props) {
    props->name = kind;
    props->has_param = true;
    props->has_weight = true;
  }

  static void ElemFields(const SzlType &t, vector<SzlField>* fields) {
    AppendField(t.element(), kValueLabel, fields);
  }

  // Read a value string.  Returns true if string successfully decoded.
  virtual bool ParseFromString(const string& val)  {
    elems_.clear();
    total_elements_ = 0;

    if (val.empty())
      return true;

    SzlDecoder dec(val.data(), val.size());
    int64 num_elements;
    if (!dec.GetInt(&num_elements) || num_elements <= 0)
      return false;

    // Load into a temporary buffer and then swap to real location.
    vector<string> stage;
    // Convert the error bounds to SzlEncoder form.
    for (int i = 0; i < numSamples_; ++i) {
      const unsigned char *const p = dec.position();
      if (!ops_.Skip(&dec)) return false;
      stage.push_back(string(reinterpret_cast<const char*>(p), dec.position() - p));
    }
    if (!dec.done()) return false;

    // Loaded successfully, so put in place.
    elems_.swap(stage);
    total_elements_ = num_elements;

    return true;
  }

  // Get the individual results.
  virtual const vector<string>* Results() { return &elems_; }

  // Report the total elements added to the table.
  virtual int64 TotElems() const { return total_elements_; }

 private:
  SzlOps ops_;
  int numSamples_;
  int64 total_elements_;
  vector<string> elems_;
};

REGISTER_SZL_RESULTS(bootstrapsum, SzlBootstrapsumResults);
