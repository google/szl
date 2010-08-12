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

#include <math.h>
#include <vector>
#include <string>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/strutils.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlresults.h"
#include "public/szlvalue.h"


void ComputeQuantiles(const vector<vector<string>* >& buffer,
  const string& min_string, const string& max_string,
  int num_quantiles, int64 tot_elems, vector<string>* quantiles);

namespace {

// Inline class that reads an encoded string produced
// by SzlQuantileEntry::Flush() (sawquantile.cc) and
// produces a results vector from it.
// The output of the quantile table (for each key) is a list
// of N elements with (approximate) ranks 1 (min element), tot_elems_/(N-1),
// tot_elems_*2/(N-1), ..., tot_elems*(N-2)/(N-1), tot_elems_ (max)
// Let r_min, r_max denote the min and max
// ranks that an element can assume. Then it is
// said to have an approximate rank of X provided
// X - error >= r_min and X + error <= r_max, where
// error = eps_*tot_elems_ is the maximum absolute
// error we are ready to tolerate.

class SzlQuantileResults : public SzlResults {
 public:
  static SzlResults* Create(const SzlType& type, string* error) {
    return new SzlQuantileResults(type);
  }

  explicit SzlQuantileResults(const SzlType& type)
      : type_(type), ops_(type.element()->type()),
        num_quantiles_(MaxInt(type.param(), 2)),
        tot_elems_(0)  { }

  ~SzlQuantileResults()  { }

  // Check if the mill type is a valid instance of this table kind.
  // If not, a reason is returned in error.
  // We already know all indices are valid, as are the types for the
  // element and the weight, which is present iff it's needed.
  static bool Validate(const SzlType& type, string* error) {
    if (!SzlOps::IsOrdered(type.element()->type())) {
      *error = "can't build quantile for unordered types";
      return false;
    }
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
  static void ElemFields(const SzlType& t, vector<SzlField>* fields) {
    AppendField(t.element(), kValueLabel, fields);
  }

  virtual bool ParseFromString(const string& val);

  virtual const vector<string>* Results()  { return &quantiles_; }

  virtual int64 TotElems() const  { return tot_elems_; }

 private:
  SzlType type_;  // the type of the table
  SzlOps ops_;   // Operations on our element type, for parsing.
  vector<string> quantiles_;
  const int num_quantiles_;
  int64 tot_elems_;

  void Clear() {
    quantiles_.clear();
    tot_elems_ = 0;
  }
  bool EncodingToString(SzlDecoder *const dec, string *const output);
};

REGISTER_SZL_RESULTS(quantile, SzlQuantileResults);


bool SzlQuantileResults::EncodingToString(SzlDecoder *const dec,
                                          string *const output) {
  // Record the starting position of the next value.
  unsigned const char* p1 = dec->position();
  // Skip past it
  if (!ops_.Skip(dec)) {
    return false;
  }
  // Now we know the end of the encoded value.
  // Record the new position
  unsigned const char* p2 = dec->position();
  output->assign(reinterpret_cast<const char*>(p1), p2 - p1);
  return true;
}

// Parse the string, just like SzlQuantileEntry::Merge
// and compute "quantiles_".  Basically, "val" contains a bunch of buffers (see
// the beginning of sawquantile.cc for a brief description of Munro-Paterson's
// "tree of buffers").
bool SzlQuantileResults::ParseFromString(const string& val) {
  Clear();
  SzlDecoder dec(val.data(), val.size());
  int64 tot_elems, num_quantiles, k, num_buffers;

  double dummy_epsilon;
  if (!val.empty()) {
    // Format errors and sanity checks
    if ((!dec.GetInt(&tot_elems)) || (tot_elems < 0)
        || (!dec.GetInt(&num_quantiles)) || (num_quantiles != num_quantiles_)
        || (!dec.GetFloat(&dummy_epsilon))) {
      LOG(ERROR) << "Failed to parse header in ParseFromString()";
      return false;
    }
  }

  if ((!dec.GetInt(&k)) || (!dec.GetInt(&num_buffers))) {
    LOG(ERROR) << "Failed to parse header in ParseFromString()";
    return false;
  }
  if (tot_elems == 0) {
    VLOG(2) << "ParseFromString() encountered tot_elems == 0";
    return true;
  }
  tot_elems_ = tot_elems;
  VLOG(2) << StringPrintf("ParseFromString(): has updated tot_elems_=%lld",
                          tot_elems_);

  string min_string, max_string;
  if (!EncodingToString(&dec, &min_string)
      || !EncodingToString(&dec, &max_string)) {
    return false;
  }
  VLOG(2) << StringPrintf("ParseFromString() retrieved min=%s max=%s",
                          min_string.c_str(), max_string.c_str());

  vector<vector<string>* > buffer;
  buffer.resize(num_buffers, NULL);

  VLOG(2) << StringPrintf("ParseFromString(): Now de-serializing %lld buffers",
                          num_buffers);
  // De-serialize "num_buffers" buffers in "dec" into "buffer".
  for (int level = 0; level < num_buffers; ++level) {
    int64 count;
    if (!dec.GetInt(&count) || (count < 0)) {
      return false;
    }
    VLOG(2) << StringPrintf(
        "ParseFromString() de-serializing buffer at level %d with %lld members",
        level, count);

    if (level >= 2 && count != 0) {
      CHECK_EQ(count, k);
    }
    if (count == 0) {
      continue;
    }

    buffer[level] = new vector<string>(k);
    buffer[level]->clear();

    while (count-- > 0) {
      buffer[level]->push_back("");
      if (!EncodingToString(&dec, & (buffer[level]->back()))) {
        // We failed to parse ... so cleanup ... and return "false".
        for (int i = 0; i < buffer.size(); i++)
          delete buffer[i];
        return false;
      }
    }
  }
  VLOG(2) << "ParseFromString(): succeeded in de-serializing all buffers";

  // Verify that tot_elems_ is okay
  int64 N = 0;
  for (int i = 0; i < buffer.size(); ++i) {
    if (buffer.at(i) != NULL) {
      if (i == 0) {
        N += buffer.at(i)->size();
      } else {
        N += buffer.at(i)->size() * (0x1LL << (i - 1));
      }
    }
  }
  CHECK_EQ(N, tot_elems_);
  VLOG(2) << StringPrintf(
      "ParseFromString(): verified that N == tot_elems == %lld", tot_elems);

  if (tot_elems_ > 0) {
    ComputeQuantiles(buffer, min_string, max_string, num_quantiles_, tot_elems_,
                     &quantiles_);
  } else {
    VLOG(2) << "ParseFromString(): tot_elems_ == 0";
  }

  VLOG(2) << "ParseFromString(): cleaning up ...";
  for (int i = 0; i < buffer.size(); i++)
    delete buffer[i];
  return true;
}

}  // namespace
