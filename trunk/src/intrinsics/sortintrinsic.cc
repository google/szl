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

// TODO:
// * implement user supplied cmp function.
// * more regression tests.
// * check for memory problems!

#include <stdlib.h>
#include <assert.h>
#include <algorithm>
#include <functional>

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/frame.h"
#include "engine/proc.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/engine.h"
#include "engine/intrinsic.h"


namespace sawzall {


// A shim for qsort to compare an array of values.
static int qcompare(Val* const* x, Val* const* y) {
  Val* d = (*x)->Cmp(*y);
  if (TaggedInts::is_null(d)) {
    ShouldNotReachHere();  // checked statically
    return TaggedInts::as_int(0);
  } else {
    return TaggedInts::as_int(d);
  }
  ShouldNotReachHere();
}


// A binary predicate for STL compare using a level of indirection.
class CompareIndirect : public binary_function<Val*, Val*, bool> {
 public:
  CompareIndirect(Val** array) : array_(array) { }
  bool operator()(Val* vi, Val* vj);
 private:
  Val** array_;
};


bool CompareIndirect::operator()(Val* vi, Val* vj)  {
  int i;
  int j;
  if (TaggedInts::is_null(vi)) {
    ShouldNotReachHere();  // checked statically
    i =  TaggedInts::as_int(0);
  } else {
    i = TaggedInts::as_int(vi);
  }
  if (TaggedInts::is_null(vj)) {
    ShouldNotReachHere();  // checked statically
    j =  TaggedInts::as_int(0);
  } else {
    j = TaggedInts::as_int(vj);
  }
  return qcompare(&array_[i], &array_[j]) < 0;
}


// Sort an Array
static ArrayVal* QSortArray(Proc* proc, ArrayVal* a, ClosureVal* cmp) {
  // Note: cmp is currently not used, because we can't yet reenter the
  // interpreter from an intrinsic.  That will be fixed in a future version.
  const int len = a->length();
  ArrayVal* vals = a->type()->as_array()->form()->NewVal(proc, len);

  for (int i = 0; i < len; ++i) {
    vals->at(i) = a->at(i);
    vals->at(i)->inc_ref();
  }

  if (len > 0)
    qsort(&vals->at(0), len, sizeof(Val*),
          reinterpret_cast<int(*)(const void*, const void*)>(&qcompare));
  return vals;
}


// Compute the permutation that sorts an Array.
static ArrayVal* GradeUp(Proc* proc, ArrayVal* a, ClosureVal* cmp) {
  // Note: cmp is currently not used, because we can't yet reenter the
  // interpreter from an intrinsic.  That will be fixed in a future version.
  const int len = a->length();
  ArrayVal* indices = Factory::NewIntArray(proc, len);

  // The idea is that we can qsort the array of Val*s, but
  // we also want the sorting permutation.  So, we introduce
  // a layer of indirection: an array of pointers to Val*s,
  // qsort that, and then compute where the pointers moved.

  for (int i = 0; i < len; ++i)
    indices->at(i) = Factory::NewInt(proc, i);

  if (len > 0)
    sort(&indices->at(0), &indices->at(0) + len, CompareIndirect(&a->at(0)));

  return indices;
}


// Sort an array.
static const char sort_doc[] =
  "sort(array of basic_type) -- return the sorted version of an array. "
  "Only scalar values can be sorted. "
  "Values will be arranged in increasing order. "
  "(An optional comparison function, which takes two elements and "
  "returns int {-,0,+}, is accepted as a second argument, "
  "but it is curently ignored.) ";

static void sort(Proc* proc, Val**& sp) {
  ArrayVal* aval = Engine::pop_array(sp);
  Val* v = Engine::pop(sp);
  ClosureVal* cval = v != NULL ? v->as_closure() : NULL;
  // Notice that although the szl sort function is variadic, this is not.
  // The szl compiler is responsible for supplying a NULL value for the
  // closure if necessary.

  ArrayVal* bval = QSortArray(proc, aval, cval);

  aval->dec_ref();
  cval->dec_ref();
  Engine::push(sp, bval);
}


// Sort an array, return index vector.
static const char sortx_doc[] =
  "sortx(array of basic_type) -- return the index vector that sorts an array. "
  "Only scalar values can be sorted. "
  "The index vector arranges array values in increasing order. "
  "(An optional comparison function, which takes two elements and "
  "returns int {-,0,+}, is accepted as a second argument, "
  "but it is curently ignored.) ";

static void sortx(Proc* proc, Val**& sp) {
  ArrayVal* aval = Engine::pop_array(sp);
  Val* v = Engine::pop(sp);
  ClosureVal* cval = v ? v->as_closure() : NULL;

  ArrayVal* bval = GradeUp(proc, aval, cval);

  aval->dec_ref();
  cval->dec_ref();
  Engine::push(sp, bval);
}


static void Initialize() {
  // make sure the SymbolTable is initialized
  assert(SymbolTable::is_initialized());

  // sort an array of integers, return sorted array
  SymbolTable::RegisterIntrinsic(
    "sort", Intrinsic::SORT, SymbolTable::incomplete_type(),
    sort, sort_doc, Intrinsic::kNormal);

  // sort an array of integers, return sorting vector
  SymbolTable::RegisterIntrinsic(
    "sortx", Intrinsic::SORTX, SymbolTable::array_of_int_type(),
    sortx, sortx_doc, Intrinsic::kNormal);
}

}  // namespace sawzall

// Note: This must happen outside the namespace
// to avoid linker/name mangling problems.
REGISTER_MODULE_INITIALIZER(
  SortIntrinsic, {
    REQUIRE_MODULE_INITIALIZED(Sawzall);
    sawzall::Initialize();
  }
);
