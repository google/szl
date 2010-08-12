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


// Structure for keeping track of the biggest (or smallest) weighted elements.
// Create with less == SzlValueLess for keeping track of the biggest,
// less == SzlValueGreater for keeping track of the smallest.
class SzlHeap {
 public:
  // Combination of a value & weight.
  struct Elem {
    string value;
    SzlValue weight;
  };

  // Create a SzlHeap.
  // maxElems is the number of element to keep track ok.
  SzlHeap(const SzlOps& weight_ops, const SzlValueCmp* less, int maxElems);

  ~SzlHeap();

  // Number of candidate biggest elements current held.
  int nElems() const { return heap_->size(); }

  // Max. elements we ever hold.
  int maxElems() const { return maxElems_; }

  // Return an ordered element.
  // The elements are reordered by AddElem or Sort.
  // REQUIRES 0 <= i < nElems().
  const Elem* Element(int i) const { return (*heap_)[i]; }

  // Add a new element to the heap.
  // It is only added if it is bigger than the smallest element currently held.
  // Returns the amount of extra memory allocated (or deallocated).
  int AddElem(const string& value, const SzlValue& w);

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

  // Operations other than comparison (Clear is used).
  const SzlOps& weight_ops_;

  // Comparison for the heap.
  const SzlValueCmp* less_;

  // max. elements we keep track of in the candidate list
  int maxElems_;
};
