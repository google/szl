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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>

#include "engine/globals.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/proc.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"


namespace sawzall {


static const int kMinNCell = 16;  // initial number of cells allocated in a map
static const int kHeadsPerCell = 4;  // number of head pointers per cell

// Maps are implemented as a hash table over an array of MapCells.
// This makes them easy to implement on the heap; the usual
// array of buckets implementation would require a lot more allocation
// and pointers.  The price we pay is a need to resize if the array
// fills.

// We allocate three items for each map:
//   1. map, the Map structure itself, which contains:
//   2. map.heads, an array of MapCell pointers, the heads of the
//      hash chains pointing to elements of map.base.
//   3. map.base, an array of MapCell structures, to hold the data.
// Rather than allocating buckets as we need them, which requires
// a lot of calls to the allocator, we grab one block of MapCells
// and use them up sequentially.  In other words, the hash chains
// are threaded through the MapCell array, which is densely
// allocated; the cells form an array from 0 <= i < occupancy.
// Lookup:
// We hash the key, choose the hash head pointer, and follow the
// chain through the cells.  By choosing many more heads (4X)
// than cells, we keep occupancy really low in the hash heads and
// only have to do 1.25 compares per lookup on average (see Knuth
// Volume 3 2nd Edition page 545).
// Experiments show comparable performance to linear chaining.
// Linear chaining works well on modern machines due to cache
// locality; the chaining used here jumps around more but does
// fewer overall compares. At kHeadsPerCell=4 the result is a
// wash but the memory consumption of the map data structure
// is 40% less than with linear chaining.

class MapCell {
 public:
  // Accessors
  Val*& key() { return key_; }
  Val*& value() { return value_; }
  void set_key(uint32 hash, Val* key) {
    hash_ = hash;
    key_ = key;
  }
  void inc_value(Proc* proc, int64 delta)  {
    if (value_->is_int()) {
      TaggedInts::Inc(proc, &value_, delta);
      return;
    }
    Unimplemented();
 }
  void set_value(Val* value)  { value_ = value; }
  MapCell* next()  { return next_; }
  void set_next(MapCell* cp)  { next_ = cp; }
  uint32 hash() const  { return hash_; }

 private:
  Val* key_;
  Val* value_;
  MapCell *next_;
  uint32 hash_;

  friend void Map::AdjustHeapPtrs();
  friend void Map::CheckHeapPtrs();
};


// Maps have a data structure, Map, stored by all clients.  That's the
// thing that's ref-counted.  Internally, Maps allocate arrays of
// MapCells.  These MapCell arrays are stored on the heap, always
// have refcount 1 (only the Map knows about them), and are
// reallocated when the array needs to grow.
// There are doublings and powers of two in the allocation, but
// the code throughout does not assume the sizes are powers of two.
Map* Map::MakeMapMem(Proc* proc, int space, bool exact) {
  // If space==0, choose an appropriate initial size.
  // Otherwise we're initializing a map and we might as well
  // pre-allocate rather than growing incrementally and waste
  // space.  If the map is small, just round up to power of two.
  // If it's big, provide a little slop.
  const int kSlop = 8192;
  if (space <= kMinNCell) {
      space = kMinNCell;
  } else if (!exact) {
    if (space < kSlop) { // round up if necessary to power of two
      if ((space & (space-1)) != 0) {  // not power of two
        while ((space & (space-1)) != 0)
          space &= (space - 1);  // clear low bit
        space <<= 1;
      }
    } else {  // just provide some spare space
      space = space + kSlop;
    }
  }
  Map* map = new (ALLOC(proc, void, sizeof(Map))) Map;
  memset(map, 0, sizeof(Map));
  map->heads_ = ALLOC(proc, MapCell*, kHeadsPerCell * space * sizeof(MapCell*));
  memset(map->heads_, 0, kHeadsPerCell * space * sizeof(MapCell*));
  map->base_ = ALLOC(proc, MapCell, space * sizeof(MapCell));
  memset(map->base_, 0, space * sizeof(MapCell));
  map->occupancy_ = 0;
  map->space_ = space;
  map->proc_ = proc;
  return map;
}


void Map::Init() {
  // nothing to do
}


void Map::Delete() {
  // Decrement the references for stored keys and values.
  MapCell* endcell = &base_[occupancy_];
  for (MapCell* cellp = base_; cellp < endcell; cellp++) {
    cellp->key()->dec_ref_and_check(proc_);
    cellp->value()->dec_ref_and_check(proc_);
  }
  // Free the memory.
  FREE(proc_, heads_);
  FREE(proc_, base_);
  FREE(proc_, this);
}


void Map::AdjustHeapPtrs() {
  // Note that we adjust pointers directly.  This approach could be used for
  // Clone() as well, rather than using CopyCells to rehash.
  Memory* heap = proc_->heap();
  MapCell* endcell = &base_[occupancy_];
  MapCell* new_base = heap->AdjustPtr(base_);
  ptrdiff_t delta = reinterpret_cast<char*>(new_base) -
                    reinterpret_cast<char*>(base_);
  for (MapCell* cellp = base_; cellp < endcell; cellp++) {
    cellp->key_ = heap->AdjustVal(cellp->key_);
    cellp->value_ = heap->AdjustVal(cellp->value_);
    if (cellp->next_)
      cellp->next_ = reinterpret_cast<MapCell*>(
                       reinterpret_cast<char*>(cellp->next_) + delta);
  }
  MapCell** endhead = heads_ + (kHeadsPerCell * space_);
  for (MapCell** headp = heads_; headp < endhead; headp++) {
    if (*headp)
      *headp = reinterpret_cast<MapCell*>(
                 reinterpret_cast<char*>(*headp) + delta);
  }
  heads_ = heap->AdjustPtr(heads_);
  base_ = new_base;
}


void Map::CheckHeapPtrs() {
  Memory* heap = proc_->heap();
  heap->CheckPtr(base_);
  heap->CheckPtr(heads_);
}


// Calculate fingerprint by iterating along elements.
// We need to guarantee the same fingerprint for the same map contents,
// regardless of allocation order.  If we were to use FingerprintCat to combine
// fingerprints by cell then we would need to scan the cells in a known order.
// So we use simple exclusive OR instead, even though the result may not be
// quite as good.
szl_fingerprint Map::Fingerprint() {
  szl_fingerprint print = kFingerSeed();
  for (int i = 0; i < occupancy_; i++) {
    MapCell* cellp = &base_[i];
    szl_fingerprint cell_fp = FingerprintCat(cellp->key()->Fingerprint(proc_),
                                             cellp->value()->Fingerprint(proc_));
    print ^= cell_fp;
  }
  return print;
}


// Calculate hash by iterating along elements.
// We need to guarantee the same hash for the same map contents,
// regardless of allocation order.  We rely on MapHashCat being associative
// and commutative so that we do not need to scan the cells in a known order.
uint32 Map::MapHash() {
  uint32 hash = kHashSeed32;
  for (int i = 0; i < occupancy_; i++) {
    MapCell* cellp = &base_[i];
    Val* v = cellp->value();
    uint32 cell_hash = MapHashCat(cellp->hash(), v->form()->Hash(v));
    hash = MapHashCat(hash, cell_hash);
  }
  return hash;
}


// Pretty-print Map
int Map::FmtMap(Fmt::State* f) {
  MapCell* endcell = &base_[occupancy_];
  if (occupancy_ == 0)
    return F.fmtprint(f, "{ : }");

  int e = F.fmtprint(f, "{ ");
  for (MapCell* cellp = base_; cellp < endcell; cellp++) {
    if (cellp != base_)
      e += F.fmtprint(f, ", ");
    F.fmtprint(f, "%V: %V", proc_, cellp->key(), proc_, cellp->value());
  }
  e += F.fmtprint(f, " }");
  return e;
}


// Map Equality
bool Map::EqualMap(Map* map) {
  if (occupancy() != map->occupancy())
    return false;
  MapCell* endcell = &base_[occupancy_];
  for (MapCell* cellp = base_; cellp < endcell; cellp++) {
    int32 index = map->Lookup(cellp->key());
    if (index < 0)  // in this map but not in other one
      return false;
    if (!cellp->value()->IsEqual(map->Fetch(index)))
        return false;
  }
  return true;
}


// Fill in array with keys of map
void Map::GetKeys(ArrayVal* array) {
  MapCell* endcell = &base_[occupancy_];
  int elem = 0;
  for (MapCell* cellp = base_; cellp < endcell && elem < array->length(); cellp++) {
    Val* v = cellp->key();
    array->at(elem) = v;
    v->inc_ref();
    elem++;
  }
}


// Get a key by index. Used to iterate over maps in when() statements.
Val* Map::GetKeyByIndex(int index) {
  return base_[index].key();
}


// Get a value by index. Used to iterate over maps in when() statements.
Val* Map::GetValueByIndex(int index) {
  return base_[index].value();
}


// Make a copy of oneself.
// Replace MapCell array with a copy of itself, so modifications
// will not affect other users of this map.  Used when ref > 1;
// provides value semantics for maps.
Map* Map::Clone() {
  Map* map = MakeMapMem(proc_, space_, (space_ < occupancy_));
  CopyCells(map->heads(), map->base(), map->space(),
                            heads(), base(), occupancy(), true);
  map->occupancy_ = occupancy_;
  return map;
}


// Copy cells from one MapCell array to another, rehashing.
// The to_heads and to_base arrays must be empty when called;
// they will be wiped.
void Map::CopyCells(
                    MapCell** to_heads, MapCell* to_base, int to_size,
                    MapCell** from_heads, MapCell* from_base, int from_occupancy, bool inc) {
  memset(to_heads, 0, kHeadsPerCell * to_size * sizeof(MapCell*));
  memset(to_base, 0, to_size * sizeof(MapCell));
  MapCell* endcell = &from_base[from_occupancy];
  MapCell* newcellp = &to_base[0];
  for (MapCell* cellp = from_base; cellp < endcell; cellp++, newcellp++) {
    *newcellp = *cellp;
    // ChainToNewLocation will fix up newcellp->next
    ChainToNewLocation(to_heads, to_size, cellp->hash(), newcellp);
    if (inc) {
      cellp->key()->inc_ref();
      cellp->value()->inc_ref();
    }
  }
}


// Given a cell we will be writing, see if we should grow the MapCell
// array before installing a new entry.  If so, grow it and return the
// new cell location. Grow by doubling in size, to keep reallocation
// cost n log n instead of n^2.
MapCell* Map::GrowIfNeeded(MapCell* cellp, uint32 hash) {
  if (cellp != NULL)  // object already in map
    return cellp;
  // new cell being created. do we need to grow?
  if (occupancy_ == space_) {
    // 100% full -> rehash
    int new_size = 2 * space_;
    MapCell** new_heads = ALLOC(proc_, MapCell*, kHeadsPerCell * new_size * sizeof(MapCell*));
    MapCell* new_base = ALLOC(proc_, MapCell, new_size * sizeof(MapCell));
    CopyCells(new_heads, new_base, new_size, heads(), base(), occupancy_, false);
    FREE(proc_, heads_);
    FREE(proc_, base_);
    heads_ = new_heads;
    base_ = new_base;
    space_ = new_size;
  }
  return ChainToNewLocation(heads_, space_, hash, base_ + occupancy_++);
}


// Set up chain to point to newly created cell, and return pointer to that cell.
MapCell* Map::ChainToNewLocation(MapCell** heads, int space, uint32 hash, MapCell* cellp) {
  MapCell** headp = &heads[hash % (kHeadsPerCell*space)];
  cellp->set_next(*headp);
  *headp = cellp;
  return cellp;
}


// Return index of cell with given key, or NULL if it's not present
MapCell* Map::FindLocation(MapCell** heads, int space, Val* key, uint32 hash) {
  MapCell* cellp = heads[hash % (kHeadsPerCell * space)];
  while(cellp && !cellp->key()->IsEqual(key))
    cellp = cellp->next();
  return cellp;
}


int32 Map::Lookup(Val* key) {
  MapCell* cellp = FindLocation(heads_, space_, key, key->form()->Hash(key));
  if (cellp == NULL)
    return -1;
  return cellp - base();
}


void Map::IncValue(int32 index, int64 delta) {
  assert(index >= 0 && index < occupancy_);
  base()[index].inc_value(proc_, delta);
}


void Map::SetValue(int32 index, Val* value) {
  assert(index >= 0 && index < occupancy_);
  MapCell* cellp = &base()[index];
  // Don't inc_ref the value; we're transferring the reference from the stack
  // to the map cell.  But we must release the old reference.
  // (If the old and new values are the same we still go from two refs to one.)
  cellp->value()->dec_ref();
  cellp->set_value(value);
}


int32 Map::InsertKey(Val* key) {
  uint32 hash = key->form()->Hash(key);
  MapCell* cellp = FindLocation(heads_, space_, key, hash);
  cellp = GrowIfNeeded(cellp, hash);
  // Don't inc_ref the value; we're transferring the reference from the stack
  // to the map cell.  But we must release the old reference.
  // (If the old and new values are the same we still go from two refs to one.)
  cellp->key()->dec_ref();
  cellp->set_key(hash, key);
  return cellp - base();
}


Val* Map::Fetch(int32 index) {
  assert(index >= 0 && index < occupancy_);
  return base()[index].value();
}


}  // namespace sawzall
