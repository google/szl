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


// Reader for SzlUnique output.
// See SzlUnique for more details.
// See SzlUnique::Flush for format.
class SzlUniqueResults: public SzlResults {
 public:
  // length of hash stored in intermediate results.
  static const int kUniqueLen = 8;

  // factory for creating all SzlUniqueResults instances.
  static SzlResults* Create(const SzlType& type, string* error) {
    return new SzlUniqueResults(type);
  }

  explicit SzlUniqueResults(const SzlType& type)
    : uniques_(1, ""), totElems_(0), maxUniques_(type.param()) {
  }

  // Check if the mill type is a valid instance of this table kind.
  // If not, a reason is returned in error.
  // We already know all indices are valid, as are the types for the
  // element and the weight, which is present iff it's needed.
  static bool Validate(const SzlType& type, string* error) {
    return true;
  }

  // Retrieve the properties for this kind of table.
  static void Props(const char* kind, SzlType::TableProperties* props) {
    props->name = kind;
    props->has_param = true;
    props->has_weight = false;
  }

  // Fill in fields with the non-index fields in the result.
  // Type is valid and of the appropriate kind for this table.
  static void ElemFields(const SzlType &t, vector<SzlField>* fields) {
    // Unique table is a special type.
    // It always has exactly one output value, and int.
    string label = t.element()->label();
    if (label.empty())
      label = "unique_";
    fields->push_back(SzlField(label, SzlType::kInt));
  }

  // Read a value string.  Returns true if string successfully decoded.
  virtual bool ParseFromString(const string& val);

  // Get the individual results.
  virtual const vector<string>* Results() { return &uniques_; }

  // Report the total elements added to the table.
  virtual int64 TotElems() const { return totElems_; }

 protected:
  // A helper for ParseFromString.  Sets totElems_.
  int64 UnpackAndEstimate(const string& val);
  // A helper for ParseFromString.  Writes the value to uniques_.
  void StoreResult(int64 unique);

 private:
  vector<string> uniques_;
  int64 totElems_;
  int maxUniques_;
};

REGISTER_SZL_RESULTS(unique, SzlUniqueResults);

// Estimate the number of unique entries.
// estimate = (maxelems << bits-in-hash) / biggest-small-elem
static int64 Estimate(int64 nelems, int64 maxElems, int64 totElems,
                      const uint8* bigsmall) {
  if (nelems < maxElems)
    return nelems;

  // The computation is a 64bit / 32bit, which will have
  // approx. msb(num) - msb(denom) bits of precision,
  // where msb is the most significant bit in the value.
  // We try to make msb(num) == 63, 24 <= msb(denom) < 32,
  // which gives about 32 bits of precision in the intermediate result,
  // and then rescale.

  // Compute the biggest of the small elements.
  // Strip leading zero bytes to maintain precision.
  int z = 0;            // number of leading denom. bytes of zeros stripped
  for (; z + 4 < SzlUniqueResults::kUniqueLen; ++z) {
    if (bigsmall[z]) {
      break;
    }
  }
  uint32 biggestsmall = (bigsmall[z] << 24) | (bigsmall[z + 1] << 16)
                      | (bigsmall[z + 2] << 8) | bigsmall[z + 3];

  if (biggestsmall == 0)
    biggestsmall = 1;

  uint32 n = nelems;
  int msbnum = 31;
  for ( ; !(n & (1 << msbnum)); --msbnum)
    ;

  // Note: since biggestsmall < (1 << 32), we know r >= n;
  // therefore, we will never generate an estimate of fewer than
  // the number of samples we keep in the table.
  uint64 r = (static_cast<uint64>(n << (31 - msbnum)) << 32) / biggestsmall;

  int renorm = z * 8 - (31 - msbnum);
  if (renorm < 0) {
    r >>= -renorm;
  } else {
    // Make sure we don't overflow.
    // This test isn't strictly an overflow test, but assures that r
    // won't be bigger than the max acceptable value afer normalization.
    if (r > (totElems >> renorm))
      return totElems;
    r <<= renorm;
  }

  // Never generate an estimate > tot elements added to the table.
  if (r > totElems)
    return totElems;

  return r;
}

// Read a value string, and return the number of estimated elements.  Returns -1
// if there is an error parsing the string.
int64 SzlUniqueResults::UnpackAndEstimate(const string& val) {
  totElems_ = 0;
  if (!val.empty()) {
    SzlDecoder dec(val.data(), val.size());
    int64 extra;
    if (!dec.GetInt(&extra))
      return -1;
    int64 nvals;
    if (!dec.GetInt(&nvals))
      return -1;

    // check for consistent param
    if (nvals > maxUniques_)
      return -1;

    // first check string for validity
    for (int i = 0; i < nvals; ++i) {
      string s;
      if (dec.peek() != SzlType::BYTES
      || !dec.GetBytes(&s)
      || s.size() != kUniqueLen)
        return -1;
    }
    if (!dec.done())
      return -1;

    totElems_ = nvals + extra;

    // Now estimate based on the number of elements seen
    // and the biggest of the small elements, which is the first.
    if (nvals) {
      dec.Restart();
      CHECK(dec.Skip(SzlType::INT));
      CHECK(dec.Skip(SzlType::INT));
      string s;
      CHECK(dec.GetBytes(&s) && s.size() == kUniqueLen);
      return Estimate(nvals, maxUniques_, totElems_,
                      reinterpret_cast<const uint8*>(s.data()));
    }
  }
  return 0;
}

void SzlUniqueResults::StoreResult(int64 unique) {
  SzlEncoder enc;
  enc.Reset();
  enc.PutInt(unique);
  uniques_[0] = enc.data();
}

// Read a value string.  Returns true if string successfully decoded.
bool SzlUniqueResults::ParseFromString(const string& val) {
  int64 unique = UnpackAndEstimate(val);
  if (unique < 0)
    return false;
  StoreResult(unique);
  return true;
}
