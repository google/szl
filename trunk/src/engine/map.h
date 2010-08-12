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

namespace sawzall {

class MapCell;

class Map {
 public:
  // No constructor, only an initter - we always place one in memory
  // allocated from the heap.  If space is zero, choose a good initial size.
  // The exact flag says to use the provided value, exactly; don't round up.
  static Map* MakeMapMem(Proc* proc, int space, bool exact);
  void Init();
  void Delete();
  // Adjust the contained heap pointers due to memory block movement at GC.
  void AdjustHeapPtrs();
  // Check the validity of the contained heap pointers.
  void CheckHeapPtrs();
  // Make a copy of the map; used by Uniq()
  Map* Clone();
  // Fill array with the keys in a map
  void GetKeys(ArrayVal* key_array);
  // Get a key by index; used internally by when() statements
  Val* GetKeyByIndex(int index);
  // Get a value by index; used internally by when() statements
  Val* GetValueByIndex(int index);

  // Access to elements
  // Reference counts are bumped as appropriate by these routines
  // when adding keys or elements.  Storing into a key or value does
  // not inc_ref; the reference is transferred. Values returned by Fetch*
  // are not inc_ref'ed.
  int32 Lookup(Val* key);
  void IncValue(int32 index, int64 delta);
  void SetValue(int32 index, Val* value);
  int32 InsertKey(Val* key);
  Val* Fetch(int32 index);
  szl_fingerprint Fingerprint();
  uint32 MapHash();
  int FmtMap(Fmt::State* f);
  bool EqualMap(Map* map);
  int occupancy()  { return occupancy_; }

 private:
  static MapCell* FindLocation(MapCell** heads, int space_, Val* key, uint32 hash);
  static MapCell* ChainToNewLocation(MapCell** heads, int space, uint32 hash, MapCell* empty);
  static void CopyCells(MapCell** to_heads, MapCell* to_base, int to_size,
                        MapCell** from_heads, MapCell* from_base, int from_occupancy, bool inc);
  // Once we know the location of a key, grow the Map if necessary
  // to make space for that key if it's a new entry in the Map.
  MapCell* GrowIfNeeded(MapCell* cellp, uint32 hash);

  // Access to array of MapCells
  MapCell** heads()  { return heads_; }
  MapCell* base()  { return base_; }
  int space()  { return space_; }

  int occupancy_;  // the number of cells occupied
  int space_;  // the allocated number of cells
  MapCell** heads_;  // heads of the hash chains, and ...
  MapCell* base_;  // ... the cells; both can be reallocated for growth
  Proc* proc_;  // TODO
};


}  // namespace sawzall
