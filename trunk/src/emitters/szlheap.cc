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

#include "public/porting.h"
#include "public/logging.h"

#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szltabentry.h"

#include "emitters/szlheap.h"


SzlHeap::SzlHeap(const SzlOps& weight_ops, const SzlValueCmp* less, int maxElems)
  : heap_(new vector<Elem*>),
    weight_ops_(weight_ops),
    less_(less),
    maxElems_(maxElems) {
}

SzlHeap::~SzlHeap() {
  Clear();
  delete heap_;
}

void SzlHeap::Clear() {
  for (int i = 0; i < heap_->size(); i++) {
    weight_ops_.Clear(&(*heap_)[i]->weight);
    delete (*heap_)[i];
  }
  heap_->clear();
}

int SzlHeap::Memory() {
  int mem = sizeof(SzlHeap) + sizeof(*heap_);
  for (int i = 0; i < heap_->size(); i++)
    mem += sizeof(Elem) + sizeof(Elem*)
           - sizeof(SzlValue) + weight_ops_.Memory((*heap_)[i]->weight)
           + (*heap_)[i]->value.size();
  return mem;
}

int SzlHeap::AddElem(const string& value, const SzlValue& w) {
  if (nElems() < maxElems()) {
    Elem* e = new Elem;
    e->value = value;
    weight_ops_.Assign(w, &e->weight);
    heap_->push_back(e);
    FixHeapUp(heap_->size() - 1);
    return value.size() + weight_ops_.Memory(w) - sizeof(SzlValue);
  } else if (maxElems() && less_->Cmp((*heap_)[0]->weight, w)) {
    Elem* smallest = (*heap_)[0];
    int mem = smallest->value.size() + weight_ops_.Memory(smallest->weight);
    smallest->value = value;
    weight_ops_.Assign(w, &smallest->weight);
    FixHeapDown(0, heap_->size());
    return value.size() + weight_ops_.Memory(w) - mem;
  }

  return 0;
}

// Sort, destroying heap.
void SzlHeap::Sort() {
  int ne = heap_->size();
  if (!ne)
    return;

  while (--ne > 0) {
    Elem* e = (*heap_)[0];
    (*heap_)[0] = (*heap_)[ne];
    (*heap_)[ne] = e;
    FixHeapDown(0, ne);
  }
}

// Restore to a heap after sorting; simply reverse the sort.
void SzlHeap::ReHeap() {
  int ne = heap_->size();
  int nswap = ne >> 1;
  for (int i = 0; i < nswap; i++) {
    Elem* e = (*heap_)[i];
    (*heap_)[i] = (*heap_)[ne - 1 - i];
    (*heap_)[ne - 1 - i] = e;
  }
  assert(IsHeap());
}

// Move an element up the heap to its proper position.
void SzlHeap::FixHeapUp(int h) {
  assert(h >= 0 && h < heap_->size());
  Elem* e = (*heap_)[h];
  const SzlValue& w = e->weight;

  while (h != 0) {
    int parent = (h - 1) >> 1;
    Elem* pe = (*heap_)[parent];
    assert(pe != NULL);
    if (!less_->Cmp(w, pe->weight))
       break;
    (*heap_)[h] = pe;
    h = parent;
  }

  (*heap_)[h] = e;
}

// Move an element down the heap to its proper position.
void SzlHeap::FixHeapDown(int h, int nheap) {
  assert(h >= 0 && h < nheap);
  Elem* e = (*heap_)[h];
  const SzlValue& w = e->weight;

  for (;;) {
    int kid = (h << 1) + 1;
    if (kid >= nheap)
      break;
    Elem* ke = (*heap_)[kid];
    const SzlValue* kw = &ke->weight;
    if (kid + 1 < nheap) {
      Elem* ke1 = (*heap_)[kid + 1];
      const SzlValue* kw1 = &ke1->weight;
      if (less_->Cmp(*kw1, *kw)) {
        ke = ke1;
        kw = kw1;
        ++kid;
      }
    }
    if (less_->Cmp(w, *kw))
      break;
    (*heap_)[h] = ke;
    h = kid;
  }

  (*heap_)[h] = e;
}

// Element h in the heap needs to be moved to the
// proper position, which may be either up or down.
// It is the only element whose weight has changed
// since the heap was last consistent.
void SzlHeap::FixHeap(int h) {
  assert(h >= 0 && h < heap_->size());

  // Fix up the heap if smaller than parent
  if (h != 0 && less_->Cmp((*heap_)[h]->weight, (*heap_)[(h - 1) >> 1]->weight))
    FixHeapUp(h);
  else
    FixHeapDown(h, heap_->size());
}

// Check that the heap really is a heap.
bool SzlHeap::IsHeap() {
  for (int i = 1; i < heap_->size(); ++i) {
    int parent = (i - 1) >> 1;
    if ((*heap_)[i] == NULL
    || (*heap_)[parent] == NULL
    || less_->Cmp((*heap_)[i]->weight, (*heap_)[parent]->weight))
      return false;
  }
  return true;
}
