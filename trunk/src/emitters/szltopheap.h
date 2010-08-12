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

#include "public/hash_map.h"

class SzlOps;
class SzlValueCmp;
struct SzlTopHeapEq;
struct SzlTopHeapHash;

// Structure for keeping track of the biggest (or smallest) weighted elements,
// with the ability to look for those elements and adjust their weights.
// Create with less == SzlValueLess for keeping track of the biggest,
// less == SzlValueGreater for keeping track of the smallest.
class SzlTopHeap {
 public:
  // Combination of a value & weight.
  struct Elem {
    string value;
    SzlValue weight;

   private:
    friend class SzlTopHeap;
    int heap;           // position in heap for fixups on reweighting
  };

  // Create a SzlTopHeap.
  // maxElems is the number of element to keep track ok.
  SzlTopHeap(const SzlOps& weight_ops, const SzlValueCmp* less, int maxElems);

  ~SzlTopHeap();

  // Number of candidate biggest elements current held.
  int nElems() const { return heap_->size(); }

  // Max. elements we ever hold.
  int maxElems() const { return maxElems_; }

  // Return an ordered element.
  // The elements are reordered by AddElem or Sort.
  // REQUIRES 0 <= i < nElems().
  Elem* Element(int i) const { return (*heap_)[i]; }

  // Find a candidate by value.  returns NULL if not present.
  // REQUIRES: created with hasFind.
  Elem* Find(const string& s);

  // Add a new element to the heap.
  // REQUIRES: heap not full.
  // Returns the amount of extra memory allocated .
  int AddNewElem(const string& value, const SzlValue& w);

  // Add w to the weight of an existing candidate.
  int AddToWeight(const SzlValue& w, Elem* e);

  // Replace the smallest candidate.
  // Returns the amount of extra memory allocated (or deallocated).
  int ReplaceSmallest(const string& value, const SzlValue& w);

  // Return the candidate with the smallest value.
  Elem* Smallest() const { return (*heap_)[0]; }

  // Sort in place so biggest element is first.
  // After sorting, !IsHeap, so can't call AddElem.
  void Sort();

  // Reverses Sort() so smallest element is first.
  // This restores the heap as a side-effect.
  void ReHeap();

  // Estimate memory currently allocated.
  int Memory();

  // Validity check.
  bool IsHeap();

  // Clear all stored elements.
  void Clear();

 private:
  // Heap of elements, smallest at the top.
  // The values are indices in elems_ of the corresponding element.
  vector<Elem*>* heap_;

  // Manipulations of the heap
  void FixHeap(int h);
  void FixHeapUp(int h);
  void FixHeapDown(int h, int nheap);

  // Set of all elements in the heap.
  typedef hash_map<const string*, Elem*, SzlTopHeapHash, SzlTopHeapEq> TopHash;
  TopHash* hash_;

  // Operations other than comparison (Clear is used).
  const SzlOps &weight_ops_;

  // Comparison for the heap.
  const SzlValueCmp* less_;

  // max. elements we keep track of in the candidate list
  int maxElems_;
};
