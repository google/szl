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

#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/szlencoder.h"
#include "public/szldecoder.h"

#include "emitters/weighted-reservoir-sampler-impl.h"
#include "emitters/weighted-reservoir-sampler.h"
#include "emitters/szlweightedsampleadapter.h"


// static
bool SzlWeightedSampleAdapter::TableTypeValid(const SzlType& type,
                                              string* error) {
  if (type.param() <= 0) {
    *error = "parameter of weightedsample tables must be positive.";
    return false;
  }
  if (type.weight() == NULL ||
      (type.weight()->type().kind() != SzlType::INT &&
       type.weight()->type().kind() != SzlType::FLOAT)) {
    *error = "weight type must be int or float.";
    return false;
  }
  return true;
}

int SzlWeightedSampleAdapter::ExtraMemory() const {
  int mem = sampler_.extra_memory();
  for (int i = 0; i < nElems(); ++i)
    mem += Element(i).size();
  return mem;
}

void SzlWeightedSampleAdapter::Encode(string* encoded) const {
  CHECK(IsValid());
  encoded->clear();
  // If nElems() == 0 but TotElems() > 0, we still need to encode.
  if (TotElems() == 0)
    return;
  SzlEncoder enc;
  enc.PutInt(TotElems() - nElems());
  enc.PutInt(nElems());
  for (int i = 0; i < nElems(); ++i) {
    const string& e = Element(i);
    enc.PutBytes(e.data(), e.size());
    enc.PutFloat(ElementTag(i));
  }
  enc.Swap(encoded);
}

void SzlWeightedSampleAdapter::EncodeForDisplay(vector<string>* output) const {
  CHECK(IsValid());
  output->resize(nElems());
  for (int i = 0; i < nElems(); ++i) {
    (*output)[i] = Element(i);
    SzlEncoder enc;
    enc.PutFloat(ElementTag(i));
    (*output)[i] += enc.data();
  }
}

static bool DecoderValid(int max_elems, SzlDecoder* dec,
                         int64* num_elems, int64* extra_elems) {
  if (!dec->GetInt(extra_elems) || !dec->GetInt(num_elems)) {
    LOG(ERROR) << "Cannot get counts.";
    return false;
  }

  // Check for consistent params.
  // if some inputs have non-positive weights, extra_elems may be > 0 even when
  // num_elems < max_elems.
  if (*num_elems > max_elems) {
    LOG(ERROR) << "Unexpected counts: num_elems = " << *num_elems
               << ", max_elems = " << max_elems
               << "; extra_elems =" << *extra_elems;
    return false;
  }

  // Check input validity.
  for (int i = 0; i < *num_elems; ++i)
    if (!dec->Skip(SzlType::BYTES) || !dec->Skip(SzlType::FLOAT)) {
      LOG(ERROR) << "Cannot get values.";
      return false;
    }
  if (!dec->done()) {
    LOG(ERROR) << "Unexpected extra bytes in decoder.";
    return false;
  }

  // Now we know the string is ok, sample all of its elements.
  dec->Restart();
  return dec->Skip(SzlType::INT) && dec->Skip(SzlType::INT);
}

bool SzlWeightedSampleAdapter::Merge(const string& encoded) {
  if (encoded.empty())
    return true;

  SzlDecoder dec(encoded.data(), encoded.size());
  int64 num_elems;
  int64 extra_elems;
  if (!DecoderValid(maxElems(), &dec, &num_elems, &extra_elems))
    return false;
  string value;
  double tag;
  ElemSrc src = {&value, 0};
  for (int i = 0; i < num_elems; ++i) {
    CHECK(dec.GetBytes(&value));
    CHECK(dec.GetFloat(&tag));
    sampler_.ConsiderSampledDatum(tag, &src);
  }
  totElems_ += extra_elems + num_elems;
  CHECK(IsValid());
  return true;
}

// static
bool SzlWeightedSampleAdapter::SplitEncodedStr(
    const string& encoded, int max_elems,
    vector<string>* output, int64* total_elems) {
  if (encoded.empty()) {
    *total_elems = 0;
    output->clear();
    return true;
  }

  SzlDecoder dec(encoded.data(), encoded.size());
  int64 num_elems;
  int64 extra_elems;
  if (!DecoderValid(max_elems, &dec, &num_elems, &extra_elems))
    return false;
  output->resize(num_elems);
  for (int i = 0; i < num_elems; ++i) {
    CHECK(dec.GetBytes(&(*output)[i]));
    unsigned const char* weight_start = dec.position();
    CHECK(dec.Skip(SzlType::FLOAT));
    (*output)[i].append(reinterpret_cast<const char*>(weight_start),
                        dec.position() - weight_start);
  }
  *total_elems = num_elems + extra_elems;
  return true;
}
