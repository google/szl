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

#include "openssl/md5.h"

#include "public/porting.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlresults.h"
#include "public/szlvalue.h"


double EstimateUniqueCount(const string &elem, int64 nElems, int64 maxElems,
                           int64 totElems);

void ComputeInverseHistogram(const SzlOps& ops, const string& last_elem,
                             const SzlValue** wlist, int64 nElems,
                             int64 maxElems, int64 totElems,
                             vector<string>* output);


// Output routines for distinctsample and inversehistogram aggregators.
// The Flush-ed format is identical for both distinctsample and 
// inversehistogram, output routines are in separate classes.
namespace {

// Reader for SzlDistinctSample output.
// See SzlDistinctSample::Flush for format.
class SzlDistinctSampleResults: public SzlResults {
 public:
  // factory for creating all SzlDistinctSampleResults instances.
  static SzlResults* Create(const SzlType& type, string* error) {
    return new SzlDistinctSampleResults(type);
  }

  explicit SzlDistinctSampleResults(const SzlType& type)
    : ops_(type.weight() != NULL ? type.weight()->type() : SzlType::kInt),
      maxElems_(type.param()),
      totElems_(0) {
  }

  // Check if the mill type is a valid instance of this table kind.
  // If not, a reason is returned in error.
  // We already know all indices are valid, as are the types for the
  // element and the weight, which is present iff it's needed.
  static bool Validate(const SzlType& type, string* error) {
    if (SzlOps::IsAddable(type.weight()->type()))
      return true;
    *error = "weight must be addable (i.e. int, float, or tuple thereof)";
    return false;
  }

  // Retrieve the properties for this kind of table.
  static void Props(const char* kind, SzlType::TableProperties* props) {
    props->has_param = true;
    props->has_weight = true;
    props->name = kind;
  }

  // Fill in fields with the non-index fields in the result.
  // Type is valid and of the appropriate kind for this table.
  static void ElemFields(const SzlType& t, vector<SzlField>* fields) {
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
    int64 extra;  // nvals + extra == total number of elements seen
    if (!dec.GetInt(&extra))
      return false;
    int64 nvals;  // actual size of sample
    if (!dec.GetInt(&nvals))
      return false;

    // check for consistent params
    if (nvals > maxElems_)
      return false;

    // first check string for validity
    for (int i = 0; i < nvals; ++i) {
      if (!dec.Skip(SzlType::BYTES) || !ops_.Skip(&dec))
        return false;
    }
    if (!dec.done())
      return false;

    // now that we know it the string is ok, read all of its elements.
    dec.Restart();
    CHECK(dec.Skip(SzlType::INT));
    CHECK(dec.Skip(SzlType::INT));
    for (int i = 0; i < nvals; ++i) {
      string s;
      CHECK(dec.GetBytes(&s));

      // Need to combine the value and weight into one saw encoded string.
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

 protected:
  SzlOps ops_;
  vector<string> elems_;
  int maxElems_;
  int64 totElems_;
};


// Reads SzlDistinctSample output, creates InverseHistogram results out of it.
// See SzlDistinctSample::Flush for format.
class SzlInverseHistogramResults: public SzlDistinctSampleResults {
 public:
  // factory for creating all SzlDistinctSampleResults instances.
  static SzlResults* Create(const SzlType& type, string* error) {
    return new SzlInverseHistogramResults(type);
  }

  explicit SzlInverseHistogramResults(const SzlType& type)
      : SzlDistinctSampleResults(type) {
  }

  // Fill in fields with the non-index fields in the result.
  // Type is valid and of the appropriate kind for this table.
  static void ElemFields(const SzlType& t, vector<SzlField>* fields) {
    AppendField(t.weight(), kValueLabel, fields);
    SzlField f(kWeightLabel, SzlType::kFloat);
    AppendField(&f, kWeightLabel, fields);
  }

  // Get the individual results.
  virtual const vector<string>* Results() { return &ihist_; }

  virtual bool ParseFromString(const string& val)  {
    ihist_.clear();

    string last_elem;
    SzlValue* wlist = NULL;           // list of weights in the sample
    const SzlValue** wplist = NULL;   // list of pointers to weights
    int64 nElems = 0;
    totElems_ = 0;

    if (!val.empty()) {
      SzlDecoder dec(val.data(), val.size());
      int64 extra;
      if (!dec.GetInt(&extra))
        return false;
      if (!dec.GetInt(&nElems))
        return false;
      totElems_ = extra + nElems;

      // check for consistent params
      if (nElems > maxElems_)
        return false;

      wlist = new SzlValue[nElems];
      wplist = new const SzlValue*[nElems];
      for (int i = 0; i < nElems; ++i) {
        // First read value and ignore it, then read weight and store
        CHECK(dec.GetBytes(&last_elem));
        CHECK(ops_.Decode(&dec, &wlist[i]));
        wplist[i] = &wlist[i];
      }
      CHECK(dec.done());
    }

    ComputeInverseHistogram(ops_, last_elem, wplist,
                            nElems, maxElems_, totElems_, &ihist_);

    for (int i = 0; i < nElems; i++)
      ops_.Clear(&wlist[i]);
    delete[] wlist;
    delete[] wplist;
    return true;
  }

 private:
  // Estimate the number of unique elements seen. Idea:
  // Let elem be the element with the k-th smallest hash.
  // Interpret the hash values as numbers between 0 and MAX_HASH-1.
  // Return k * MAX_HASH / hash(elem).
  // Works the same way as estimation in sawuniqueresults.cc,
  // except using floating point instead of integer arithmetic.
  double EstimateUniqueCount(const string &elem,
                             int64 nElems,
                             int64 maxElems,
                             int64 totElems) {
    if (nElems < maxElems)
      return nElems;

    uint8 digest[MD5_DIGEST_LENGTH];
    MD5Digest(elem.data(), elem.size(), &digest);

    double a, b, c;
    a = 0;
    b = 1;
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
      a = 256*a + digest[i];
      b *= 256;
    }
    c =  b/a * maxElems;
    if (c > totElems) c = totElems;
    return c;
  }

  vector<string> ihist_;
};

REGISTER_SZL_RESULTS(distinctsample, SzlDistinctSampleResults);
REGISTER_SZL_RESULTS(inversehistogram, SzlInverseHistogramResults);

}  // end namespace
