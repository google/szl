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

#include "utilities/strutils.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlresults.h"
#include "public/szlvalue.h"
#include "emitters/szlsketch.h"

// Reader for SzlTop output.
// See SzlTop::Flush for format.
class SzlTopResults: public SzlResults {
 public:
  // Max. elements in a top table.
  static const int kMaxTops = 1000;

  // factory for creating all SzlTopResults instances.
  static SzlResults* Create(const SzlType& type, string* error) {
    return new SzlTopResults(type);
  }

  explicit SzlTopResults(const SzlType& type)
    : ops_(type.weight() != NULL ? type.weight()->type() : SzlType::kInt),
      maxElems_(type.param()),
      totElems_(0) {
  }

  // Check if the mill type is a valid instance of this table kind.
  // If not, a reason is returned in error.
  // We already know all indices are valid, as are the types for the
  // element and the weight, which is present iff it's needed.
  static bool Validate(const SzlType& type, string* error) {
    if (!SzlOps::IsNumeric(type.weight()->type())) {
      *error = "weight must be an int, float, or tuple thereof";
      return false;
    }
    if (type.param() > kMaxTops) {
      *error = StringPrintf("can't have more than %d elements", kMaxTops);
      return false;
    }
    return true;
  }

  // Retrieve the properties for this kind of table.
  static void Props(const char* kind, SzlType::TableProperties* props) {
    props->name = kind;
    props->has_param = true;
    props->has_weight = true;
  }

  // Dump a deviation field for every field in t.
  static void DumpDeviations(const SzlType& t, const string& prefix,
                             vector<SzlField>* fields) {
    if (t.kind() != SzlType::TUPLE) {
      fields->push_back(SzlField(prefix, SzlType::kFloat));
      return;
    }

    for (int i = 0; i < t.fields_size(); ++i) {
      string name = prefix + "_";
      if (t.field(i).label().empty())
        StringAppendF(&name, "%d", i);
      else
        name += t.field(i).label();
      DumpDeviations(t.field(i).type(), name, fields);
    }
  }

  // Fill in fields with the non-index fields in the result.
  // Type is valid and of the appropriate kind for this table.
  static void ElemFields(const SzlType &t, vector<SzlField>* fields) {
    AppendField(t.element(), kValueLabel, fields);
    AppendField(t.weight(), kWeightLabel, fields);

    // Top tables have an error bound for every weight element.
    if (t.weight()->type().kind() != SzlType::TUPLE) {
      fields->push_back(SzlField("deviation_", SzlType::kFloat));
    } else {
      DumpDeviations(t.weight()->type(), "deviation", fields);
    }
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

    // adjust params, since top may keep more elements than we report.
    int nv = maxElems_;
    if (nv > nvals)
      nv = nvals;

    // Skip past all the results to get the sketch.
    for (int i = 0; i < nvals; ++i) {
      if (!dec.Skip(SzlType::BYTES) || !ops_.Skip(&dec))
        return false;
    }

    // Get the sketch.
    int64 nTabs, tabSize;
    if (!dec.GetInt(&tabSize) || !dec.GetInt(&nTabs))
      return false;
    int nerrs = ops_.nflats();
    double* err = new double[nerrs];
    if (nTabs) {
      SzlSketch sketch(ops_, nTabs, tabSize);
      if (!sketch.Decode(&dec)) {
        delete[] err;
        return false;
      }
      sketch.StdDeviation(err);
    } else {
      for (int i = 0; i < nerrs; i++)
        err[i] = 0.;
    }

    if (!dec.done()) {
      delete[] err;
      return false;
    }

    // Convert the error bounds to SzlEncoder form.
    SzlEncoder enc;
    for (int i =0; i < nerrs; i++)
      enc.PutFloat(err[i]);
    string errval = enc.data();

    // now that we know the string is ok, sample all of its elements.
    dec.Restart();
    CHECK(dec.Skip(SzlType::INT));
    CHECK(dec.Skip(SzlType::INT));
    for (int i = 0; i < nv; ++i) {
      string s;
      CHECK(dec.GetBytes(&s));

      // Need to combine the value and weight into one saw encoded string.
      // The weight is already encoded; just copy it.
      // Also tack on an error.
      unsigned const char* p = dec.position();
      CHECK(ops_.Skip(&dec));
      s += string(reinterpret_cast<const char*>(p), dec.position() - p);
      s += errval;

      elems_.push_back(s);
    }

    // The total number of elements includes the number explicitly in the
    // structure plus any deleted by earlier processing.
    totElems_ = extra + nvals;

    delete[] err;

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

REGISTER_SZL_RESULTS(top, SzlTopResults);
