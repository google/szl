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

// Implementation of SzlTabWriter and SzlTabEntry for unique tables in Sawzall.
// Structure for calculating the number of unique elements.
// The technique used is:
// 1) Convert all elements to unique evenly spaced hash keys.
// 2) Keep track of the smallest N element ("nelems") of these elements.
// 3) "nelems" cannot glow beyond maxelems.
// 4) Based on the coverage of the space, compute an estimate
//    of the total number of unique elements, where biggest-small-elem
//    means largest element among kept "maxelems" elements.
//    unique = nelems < maxelems
//           ? nelems
//           : (maxelems << bits-in-hash) / biggest-small-elem

#include <stdio.h>
#include <string>
#include <vector>

#include "openssl/md5.h"

#include "public/porting.h"
#include "public/hash_set.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/szldecoder.h"
#include "public/szlencoder.h"
#include "public/szlresults.h"
#include "public/szltabentry.h"


// Implementatiopn of unique table objects.
class SzlUnique: public SzlTabWriter {
 private:
  explicit SzlUnique(const SzlType& type)
    : SzlTabWriter(type, true, false)  { }

 public:
  static SzlTabWriter* Create(const SzlType& type, string* error) {
    return new SzlUnique(type);
  }

  virtual SzlTabEntry* CreateEntry(const string& index) const {
    return new SzlUniqueEntry(param());
  }

 private:
  class SzlUniqueEntry: public SzlTabEntry {
   public:
    explicit SzlUniqueEntry(int param)
      : heap_(),
        exists_(10),   // STL defaults to 100 buckets, which is a lot.
        maxElems_(param),
        isSorted_(false) {
    }

    virtual int AddElem(const string& elem);
    virtual void Flush(string* output);
    virtual void FlushForDisplay(vector<string>* output);
    virtual SzlTabEntry::MergeStatus Merge(const string& val);

    virtual void Clear() {
      tot_elems_ = 0;
      heap_.clear();
      exists_.clear();
    }

    virtual int Memory();
    virtual int TupleCount()  { return 1; }

   private:
    // type of hash values kept.
    typedef uint64 HashVal;

    // Estimate the distinct count.
    int64 Estimate() const;

    // Add a pre-hashed element to the unique table.
    int AddHash(HashVal hash);

    static HashVal PackUniqueHash(const uint8* mem);
    static void UnpackUniqueHash(HashVal hash, uint8* mem);

    vector<HashVal> heap_;
    void FixHeapUp(int h);
    void FixHeapDown(int h, int nheap);
    void MakeSortedHeap();
    void Sort();
    void ReHeap();
    // Validity check.
    bool IsHeap();

    template <typename T>
    struct identity {
      inline const T& operator()(const T& t) const { return t; }
    };

    // This also keeps only smallest "maxElems_" elements.
    typedef hash_set<HashVal, identity<HashVal> > Exists;
    Exists exists_;

    // Size of the hash we keep.
    static const int kHashSize = 24;

    // Max elements we keep track of.
    // This needs to be a constant to maintain estimate accuracy.
    const int maxElems_;

    // Is heap_ actually a sorted array, biggest to smallest?
    bool isSorted_;
  };
};


REGISTER_SZL_TAB_WRITER(unique, SzlUnique);


inline SzlUnique::SzlUniqueEntry::HashVal
SzlUnique::SzlUniqueEntry::PackUniqueHash(const uint8* mem) {
  uint32 hhi = (mem[0] << 24) | (mem[1] << 16) | (mem[2] << 8) | mem[3];
  uint32 hlo = (mem[4] << 24) | (mem[5] << 16) | (mem[6] << 8) | mem[7];
  uint64 h = hhi;
  return (h << 32) | hlo;
}


inline void
SzlUnique::SzlUniqueEntry::UnpackUniqueHash(HashVal hash, uint8* mem) {
  uint32 hhi = hash >> 32;
  uint32 hlo = hash;
  mem[0] = hhi >> 24;
  mem[1] = hhi >> 16;
  mem[2] = hhi >> 8;
  mem[3] = hhi;
  mem[4] = hlo >> 24;
  mem[5] = hlo >> 16;
  mem[6] = hlo >> 8;
  mem[7] = hlo;
}


int SzlUnique::SzlUniqueEntry::AddElem(const string& elem) {
  COMPILE_ASSERT(MD5_DIGEST_LENGTH < kHashSize, MD5_DIGEST_LENGTH_too_big);
  uint8 digest[MD5_DIGEST_LENGTH];
  MD5Digest(elem.data(), elem.size(), &digest);
  return AddHash(PackUniqueHash(digest));
}


int SzlUnique::SzlUniqueEntry::AddHash(HashVal hash) {
  ++tot_elems_;
  if (maxElems_ <= 0)
    return 0;

  // Add it only if it isn't already there.
  if (exists_.find(hash) != exists_.end())
    return 0;

  // Add it if the heap isn't full.
  if (heap_.size() < maxElems_) {
    int memory = Memory();
    isSorted_ = false;
    heap_.push_back(hash);
    FixHeapUp(heap_.size() - 1);
    exists_.insert(hash);
    return Memory() - memory;
  } else if (hash < heap_[0]) {
    // Otherwise, replace the biggest of stored if the new value is smaller.
    isSorted_ = false;
    exists_.erase(heap_[0]);               ;
    heap_[0] = hash;
    FixHeapDown(0, heap_.size());
    exists_.insert(hash);
    return 0;
  }
}

// Move an element up the heap to its proper position.
void SzlUnique::SzlUniqueEntry::FixHeapUp(int h) {
  if (h >= 0 && h < heap_.size()) {
    HashVal e = heap_[h];

    while (h != 0) {
      int parent = (h - 1) >> 1;
      HashVal pe = heap_[parent];
      if (!(e > pe))
        break;
      heap_[h] = pe;
      h = parent;
    }
    heap_[h] = e;
  } else {
    fputs("heap error in unique table\n", stderr);
  }
}

// Move an element down the heap to its proper position.
void SzlUnique::SzlUniqueEntry::FixHeapDown(int h, int nheap) {
  if (h >= 0 && h < nheap) {
    HashVal e = heap_[h];
    for (;;) {
      int kid = (h << 1) + 1;
      if (kid >= nheap)
        break;
      HashVal ke = heap_[kid];
      if (kid + 1 < nheap) {
        HashVal ke1 = heap_[kid + 1];
        if (ke1 > ke) {
          ke = ke1;
          ++kid;
        }
      }
      if (!(ke > e))
        break;
      heap_[h] = ke;
      h = kid;
    }
    heap_[h] = e;
  } else {
    fputs("heap error in unique table\n", stderr);
  }
}

// Check that the heap really is a heap.
bool SzlUnique::SzlUniqueEntry::IsHeap() {
  for (int i = 1; i < heap_.size(); ++i) {
    int parent = i - 1;
    parent >>= !isSorted_;

    // In addition to being a heap, we need to have no duplicates.
    if (heap_[i] >= heap_[parent])
      return false;
  }
  return true;
}

// Sort, destroying heap.  Resulting array is smallest first.
void SzlUnique::SzlUniqueEntry::Sort() {
  int ne = heap_.size();
  if (!ne)
    return;

  while (--ne > 0) {
    HashVal e = heap_[0];
    heap_[0] = heap_[ne];
    heap_[ne] = e;
    FixHeapDown(0, ne);
  }
}

// Restore to a heap after sorting; simply reverse the sort.
void SzlUnique::SzlUniqueEntry::ReHeap() {
  int ne = heap_.size();
  int nswap = ne >> 1;
  for (int i = 0; i < nswap; i++) {
    HashVal e = heap_[i];
    heap_[i] = heap_[ne - 1 - i];
    heap_[ne - 1 - i] = e;
  }
  if (!IsHeap()) {
    fputs("heap error in unique table\n", stderr);
  }
}

// Make sure the heap is sorted.
void SzlUnique::SzlUniqueEntry::MakeSortedHeap() {
  if (!isSorted_) {
    Sort();
    ReHeap();
    isSorted_ = true;
  }
}

// Estimate the number of unique entries.
// estimate = (maxelems << bits-in-hash) / biggest-small-elem
int64 SzlUnique::SzlUniqueEntry::Estimate() const {
  if (maxElems_ <= 0) {
    return 0;
  }
  if (heap_.size() < maxElems_) {
    return heap_.size();
  }

  // The computation is a 64bit / 32bit, which will have
  // approx. msb(num) - msb(denom) bits of precision,
  // where msb is the most significant bit in the value.
  // We try to make msb(num) == 63, 24 <= msb(denom) < 32,
  // which gives about 32 bits of precision in the intermediate result,
  // and then rescale.
  //
  // Strip leading zero bytes to maintain precision.
  // Do this by byte to maintain same estimate.
  uint8 unpacked[MD5_DIGEST_LENGTH];
  UnpackUniqueHash(heap_[0], unpacked);
  int z = 0;
  // Number of leading denom. bytes of zeros stripped.
  for (; z < MD5_DIGEST_LENGTH; ++z) {
    if (unpacked[z]) {
      break;
    }
  }
  uint32 biggestsmall = (unpacked[z] << 24) | (unpacked[z + 1] << 16)
                      | (unpacked[z + 2] << 8) | unpacked[z + 3];
  if (biggestsmall == 0) {
    biggestsmall = 1;
  }
  int msbnum = Log2Int(heap_.size());
  uint64 r = (static_cast<uint64>(heap_.size() << (31 - msbnum)) << 32)
      / biggestsmall;

  int renorm = z * 8 - (31 - msbnum);
  if (renorm < 0) {
    r >>= -renorm;
  } else {
    // Make sure we don't overflow.
    // This test isn't strictly an overflow test, but assures that r
    // won't be bigger than the max acceptable value afer normalization.
    if (r > (TotElems() >> renorm))
      return TotElems();
    r <<= renorm;
  }
  // Although this will introduce skew, never generate an estimate larger
  // than total elements added to the table.
  if (r > TotElems()) {
    return TotElems();
  }
  return r;
}


// Produce the encoded string that represents the data in this entry. This
// value is used for merge operations as it contains all information
// needed for the merge. The SzlEncoding produced is a concatenation of the
// counts and encoding of the sorted elements.
void SzlUnique::SzlUniqueEntry::Flush(string* output) {
  int ne = heap_.size();
  if (ne == 0) {
    output->clear();
    return;
  }

  SzlEncoder enc;
  enc.PutInt(TotElems() - ne);  // Number of emits not in string.
  enc.PutInt(ne);              // Number of hashes encoded.
  // Emit in biggest to smallest order.
  MakeSortedHeap();
  uint8 buf[kHashSize];
  for (vector<HashVal>::iterator it = heap_.begin(); it <  heap_.end(); ++it) {
    UnpackUniqueHash(*it, buf);
    enc.PutBytes(reinterpret_cast<char*>(buf), kHashSize);
  }
  enc.Swap(output);
  Clear();
}


// Get the encoded string representation of this entry for display
// purposes. Actually the estimate.
void SzlUnique::SzlUniqueEntry::FlushForDisplay(vector<string>* output) {
  output->clear();
  if (TotElems() == 0) {
    output->push_back("");
    return;
  }

  int64 estimate = Estimate();
  SzlEncoder enc;
  enc.PutInt(estimate);  // Number of emits not in the string.
  string encoded;
  enc.Swap(&encoded);
  output->push_back(encoded);
}


// Merge a flushed state into the current state.
// TODO: Bulk add instead of AddHash to reduce overhead.
SzlTabEntry::MergeStatus SzlUnique::SzlUniqueEntry::Merge(const string& val) {
  if (val.empty())
    return MergeOk;

  SzlDecoder dec(val.data(), val.size());
  int64 extra;
  if (!dec.GetInt(&extra))
    return MergeError;
  int64 nvals;
  if (!dec.GetInt(&nvals))
    return MergeError;

  if (nvals == 0)
    return MergeOk;

  // Decode stored hash values and AddHash.
  for (int i = 0; i < nvals; ++i) {
    string s;
    if (dec.peek() != SzlType::BYTES || !dec.GetBytes(&s)
        || s.size() != kHashSize)
      return MergeError;
    AddHash(PackUniqueHash(reinterpret_cast<const uint8*>(s.data())));
  }
  if (!dec.done())
    return MergeError;
  tot_elems_ += extra;

  return MergeOk;
}


int SzlUnique::SzlUniqueEntry::Memory() {
  // Estimate of storage.
  // Assumes that the hash table consumes one hash value per
  // inserted entry and one pointer per bucket.
  return sizeof(SzlUniqueEntry) +
         exists_.bucket_count() * (sizeof(void*)) +  // hash buckets
         exists_.size() * sizeof(HashVal) +          // hash values
         heap_.size() * sizeof(HashVal);         // heap storage
}
