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

#include <assert.h>
#include <string>
#include <vector>
#include <map>

#include "openssl/md5.h"

#include "public/porting.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "public/szlvalue.h"
#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szltabentry.h"

#include "emitters/szlheap.h"


void ComputeInverseHistogram(const SzlOps& weight_ops, const string& last_elem,
                             const SzlValue** wlist, int64 nElems,
                             int64 maxElems, int64 totElems,
                             vector<string>* output);


// This is an implementation of the distinctsample and inversehistogram
// aggregators. For a table with parameter k, we keep a list of k distinct
// values with minimum hash value. The hash function we use is MD5 applied
// to the string encoding of each value.
// We use an STL map to store the samples, ordered by the hash value
// of the encoding. For each key in the sample, we keep track of
// the sum of weights associated with all occurences of the key.


namespace {

// Keep a sample of elements (+sum of weights) with minimum hash.
class SzlDistinctSample: public SzlTabWriter {
 protected:
  explicit SzlDistinctSample(const SzlType& type)
    : SzlTabWriter(type, true, false) { }
  virtual ~SzlDistinctSample()  { }
  class SzlDistinctSampleEntry;

 public:
  static SzlTabWriter* Create(const SzlType& type, string* error) {
    if (type.weight() == NULL)
      LOG(ERROR) << "Internal error - distinctsample table without weight";
    if (!SzlOps::IsAddable(type.weight()->type())) {
      *error = "The weights must be addable (ints, floats, or tuples thereof)";
      return NULL;
    }
    return new SzlDistinctSample(type);
  }

  virtual SzlTabEntry* CreateEntry(const string& index) const {
    return new SzlDistinctSampleEntry(weight_ops(), param());
  }

 protected:
  class SzlDistinctSampleEntry : public SzlTabEntry {
   public:
    explicit SzlDistinctSampleEntry(const SzlOps& weight_ops, int param)
      : weight_ops_(weight_ops),
        nElems_(0),
        maxElems_(param),
        list_(new Elem[maxElems_]) {
      CHECK(list_ != NULL) << "Unable to allocate memory";
    }
    virtual ~SzlDistinctSampleEntry() {
      map_.clear();
      for (int i = 0; i < nElems_; i++) {
        weight_ops().Clear(&(list_[i].weight));
      }
      delete[] list_;
      list_ = NULL;
    }

    // Can return negative value: net memory deallocation.
    virtual int AddElem(const string& elem) {
      return AddWeightedElem(elem, SzlValue(static_cast<int64>(1)));
    }

    // Can return negative value: net memory deallocation.
    virtual int AddWeightedElem(const string& elem, const SzlValue& weight);

    virtual void Flush(string* output);
    virtual void FlushForDisplay(vector<string>* output);
    virtual SzlTabEntry::MergeStatus Merge(const string& val);

    virtual void Clear() {
      tot_elems_ = 0;
      map_.clear();
      for (int i = 0; i < nElems_; i++) {
        list_[i].value.clear();
        weight_ops().Clear(&(list_[i].weight));
      }
      nElems_ = 0;
    }

    const SzlOps& weight_ops() const { return weight_ops_; }

    virtual int Memory();
    virtual int TupleCount()  { return map_.size(); }

   protected:
    const SzlOps& weight_ops_;

    // Helper to verify consistency of structures
    bool IsValid();

    // structure for keeping track of the current sample
    struct Elem {
      string value;
      SzlValue weight;
    };

    int nElems_;
    int maxElems_;

    // Comparison function used by the STL map to keep track of
    // up to maxElems_ distinct entries with minimum hash value
    struct HashCmp {
      bool operator()(const string *s1, const string *s2) const;
    };

    // Data structure to store <sample, aggregated_weight> pairs.
    // We allocate an array list_[maxElems] to avoid frequent memory
    // allocation. On top of that, map_ maps value to index in list_
    // that stores the <value,weight> pair.
    // It also allows fast deletion of key with largest hash.
    typedef map<const string*, int, HashCmp> MyMapType;
    MyMapType map_;
    Elem* list_;  // array of Elem's, allocated to fixed size maxElems_
  };
};

// Register the distinctsample table.
// General mechanism for registering Szl writers with Szl.
// See sawtabentry.h for macro definition.

REGISTER_SZL_TAB_WRITER(distinctsample, SzlDistinctSample);


int SzlDistinctSample::SzlDistinctSampleEntry::Memory() {
  int mem;
  int ii;

  mem = sizeof(SzlDistinctSampleEntry) +
        sizeof(map_) + sizeof(MyMapType::value_type) * map_.size() +
        sizeof(list_) + sizeof(Elem) * maxElems_;

  for (ii = 0; ii < nElems_; ii++) {
    mem += list_[ii].value.size() +
           weight_ops().Memory(list_[ii].weight);
  }
  return mem;
  // The memory consumption of the STL map map_ is only estimated, as
  // I don't know the details of its internals -- the inaccuracy is on
  // the order of space for a pointer or two per element stored.
}


// Comparison operator for the map_. Instead of storing the hash with
// each key and doing string comparison, we recompute MD5 every time
// it is needed. Slower, but preserves memory.
bool SzlDistinctSample::SzlDistinctSampleEntry::HashCmp::operator()(
                        const string *s1, const string *s2) const {
  uint8 digest1[MD5_DIGEST_LENGTH];
  uint8 digest2[MD5_DIGEST_LENGTH];
  MD5Digest(s1->data(), s1->size(), &digest1);
  MD5Digest(s2->data(), s2->size(), &digest2);

  int cmp = memcmp(digest1, digest2, MD5_DIGEST_LENGTH);
  if (cmp<0) return true;
  if (cmp>0) return false;
  // for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
  //   if (digest1[i] < digest2[i]) return true;
  //   else if (digest1[i] > digest2[i]) return false;
  if (*s1 < *s2) return true;
  return false;
}

int SzlDistinctSample::SzlDistinctSampleEntry::AddWeightedElem(
                        const string& elem, const SzlValue& w) {
  tot_elems_++;

  MyMapType::iterator ii;
  ii = map_.lower_bound(&elem);

  // if table full & new elem too big, drop it
  if (ii == map_.end() && nElems_ >= maxElems_)
    return 0;

  // if element exists, add to its weight
  if (ii != map_.end() && *(ii->first) == elem) {
    weight_ops().Add(w, &(list_[ii->second].weight));
    return 0;
  }

  // if table not full, add elem, otherwise replace largest hash
  int mem;
  if (nElems_ < maxElems_) {
    list_[nElems_].value = elem;
    weight_ops().Assign(w, &(list_[nElems_].weight));
    map_[&list_[nElems_].value] = nElems_;
    mem = weight_ops().Memory(w) + elem.size();
    nElems_++;
  } else {
    int ee;  // index of Elem in list_ to be replaced
    ii = map_.end();
    ii--;
    ee = ii->second;
    mem = - (weight_ops().Memory(list_[ee].weight) + list_[ee].value.size());
    map_.erase(ii);

    list_[ee].value = elem;
    weight_ops().Assign(w, &(list_[ee].weight));
    map_[&list_[ee].value] = ee;
   mem += weight_ops().Memory(w) + elem.size();
  }
  assert(map_.size() == nElems_);
  return mem;
}


// Dump the current state into a string suitable for later merging,
// and reset the current state.
void SzlDistinctSample::SzlDistinctSampleEntry::Flush(string* output) {
  assert(IsValid());

  if (nElems_ == 0) {
    output->clear();
    return;
  }

  // combine all of the counts and tags into a single sorted string.
  SzlEncoder enc;
  enc.PutInt(tot_elems_ - nElems_);
  enc.PutInt(nElems_);

  // Output <sample,weight> pairs ordered by increasing hash
  for (MyMapType::iterator ii = map_.begin(); ii != map_.end(); ii++) {
    int ee = ii->second;  // index of an Elem in list_[]
    enc.PutBytes(list_[ee].value.data(), list_[ee].value.size());
    weight_ops().Encode(list_[ee].weight, &enc);
  }
  enc.Swap(output);
  Clear();
}


void SzlDistinctSample::SzlDistinctSampleEntry::FlushForDisplay(
                        vector<string>* output) {
  output->clear();
  if (nElems_ == 0) {
    output->push_back("");
    return;
  }

  // Output <sample,weight> pairs ordered by increasing hash
  for (MyMapType::iterator ii = map_.begin(); ii != map_.end(); ii++) {
    int ee = ii->second;  // index of an Elem in list_[]
    // Note that Flush() emits the value in a bytes value, this does not.
    SzlEncoder enc;
    weight_ops().Encode(list_[ee].weight, &enc);
    string encoded;
    enc.Swap(&encoded);
    output->push_back(list_[ee].value + encoded);
  }
}


// Merge the current state with another sample Flush-ed into a string.
SzlTabEntry::MergeStatus SzlDistinctSample::SzlDistinctSampleEntry::Merge(
                         const string& val) {
  if (val.empty())
    return MergeOk;

  SzlDecoder dec(val.data(), val.size());
  int64 extra;
  if (!dec.GetInt(&extra))
    return MergeError;
  int64 nvals;
  if (!dec.GetInt(&nvals))
    return MergeError;

  // check for consistent params
  if (nvals > maxElems_)
    return MergeError;

  // first check string for validity
  for (int i = 0; i < nvals; ++i) {
    if (!dec.Skip(SzlType::BYTES) || !weight_ops().Skip(&dec))
      return MergeError;
  }
  if (!dec.done())
    return MergeError;

  // Now that we know it the string is ok, merge its content
  // with the current sample.
  dec.Restart();
  CHECK(dec.Skip(SzlType::INT));
  CHECK(dec.Skip(SzlType::INT));
  SzlValue w;
  for (int i = 0; i < nvals; ++i) {
    string s;
    CHECK(dec.GetBytes(&s));
    CHECK(weight_ops().Decode(&dec, &w));
    AddWeightedElem(s, w);
  }
  weight_ops().Clear(&w);
  tot_elems_ += extra;

  assert(IsValid());
  return MergeOk;
}

bool SzlDistinctSample::SzlDistinctSampleEntry::IsValid() {
  int64 nelems = map_.size();
  if (nElems_ != nelems) return false;
  return (tot_elems_ >= nelems) && (nelems <= maxElems_);
}


// ===========================================================================


// Same output as distinctsample, but we override FlushForDisplay()
// to generate the distribution instead of the raw data.

class SzlInverseHistogram : public SzlDistinctSample {
 private:
  explicit SzlInverseHistogram(const SzlType& type)
    : SzlDistinctSample(type)  { }
  virtual ~SzlInverseHistogram()  { }
  class SzlInverseHistogramEntry;

 public:
  static SzlTabWriter* Create(const SzlType& type, string* error) {
    if (type.weight() == NULL)
      LOG(ERROR) << "Internal error - inversehistogram table without weight";
    if (!SzlOps::IsAddable(type.weight()->type())) {
      *error = "The weights must be addable (ints, floats, or tuples thereof)";
      return NULL;
    }
    return new SzlInverseHistogram(type);
  }

  virtual SzlTabEntry* CreateEntry(const string& index) const {
    return new SzlInverseHistogramEntry(weight_ops(), param());
  }

 private:
  class SzlInverseHistogramEntry : public SzlDistinctSampleEntry {
   public:
    SzlInverseHistogramEntry(const SzlOps& weight_ops, int param)
      : SzlDistinctSampleEntry(weight_ops, param)  { }

    virtual void FlushForDisplay(vector<string>* output);
  };
};


// Register the inversehistogram table.
// General mechanism for registering Szl writers with Szl.
// See sawtabentry.h for macro definition.

REGISTER_SZL_TAB_WRITER(inversehistogram, SzlInverseHistogram);


void SzlInverseHistogram::SzlInverseHistogramEntry::FlushForDisplay(
                          vector<string>* output) {
  // Compute the inverse histogram and return it as the result.
  // There is always a result; even when there are no elements,
  // the first pair is (0,#unique) which is (0,0).

  // First get all the weights and the last element.
  string last_elem;
  const SzlValue** wlist = new const SzlValue*[nElems_];
  assert(map_.size() == nElems_);
  int index = 0;
  for (MyMapType::iterator ii = map_.begin(); ii != map_.end(); ii++) {
    int ee = ii->second;  // index of an Elem in list_[]
    last_elem = list_[ee].value;
    wlist[index++] = &list_[ee].weight;
  }

  ComputeInverseHistogram(weight_ops(), last_elem, wlist, nElems_, maxElems_,
                          tot_elems_,  output);
  delete [] wlist;
}


}  // unnamed namespace
