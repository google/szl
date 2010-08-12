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

// For debugging
// #define SZL_IMMEDIATE_DELETE  // immediate delete when ref count is zero

#include <string>

namespace sawzall {


// ----------------------------------------------------------------------------
// Each Sawzall value is represented by a tagged Val*: If the lowest-order
// bit (bit 0) is set, the Val* doesn't point to a Val object in memory but
// instead represents a 31bit small integer (smi). If the lowest-order bit
// is not set, the Val* points to a corresponding Val object in memory
// that holds the value's data.
//
// Val objects don't have virtual functions which saves the space for the
// vtable* per Val object. Instead, each Val object contains a Form*. The
// Form object contain virtual functions and implement the Val interface.
// Since there is (roughly) only one Form object per Val object type, the
// extra space overhead for the vtable* is negligible.

class Val {
 public:
  // accessors
  Form* form() const {
    if (is_smi()) {
      return SymbolTable::int_type()->int_form();
    } else {
      assert(ref_ >= 0);
      return form_;
    }
  }
  Type* type() const  { return form()->type(); }

  // testers
  bool is_null() const  { return TaggedInts::is_null(this); }
  bool is_ptr() const  { return TaggedInts::is_ptr(this); }
  bool is_smi() const  { return TaggedInts::is_smi(this); }

  bool is_basic() const  { return type()->is_basic(); }
  bool is_scalar() const  { return type()->is_basic64(); }
  bool is_bool() const  { return type()->is_bool(); }
  bool is_bytes() const  { return type()->is_bytes(); }
  bool is_fingerprint() const  { return type()->is_fingerprint(); }
  bool is_float() const  { return type()->is_float(); }
  bool is_int() const  { return type()->is_int(); }
  bool is_uint() const  { return type()->is_uint(); }
  bool is_string() const  { return type()->is_string(); }
  bool is_time() const  { return type()->is_time(); }
  bool is_array() const  { return type()->is_array(); }
  bool is_map() const  { return type()->is_map(); }
  bool is_tuple() const  { return type()->is_tuple(); }
  bool is_closure() const  { return type()->is_function(); }

  bool is_indexable() const  { return type()->is_indexable(); }
  bool is_unique() const  { return form()->IsUnique(this); }

  // Reference count of a value; returns 1 for smi and null.
  int32 ref() const {
    if (is_ptr() && !is_null()) {
      assert(ref_ >= 0);
      return ref_;
    } else {
      return 1;
    }
  }

  // Call inc_ref() whenever a persistent copy is made of a Val pointer.
  void inc_ref() {
    if (is_ptr() && !is_null()) {
      assert(ref_ >= 0);
      ref_++;
    }
  }

  // Call dec_ref() whenever a Val pointer is discarded, except (see below)
  // within Form::Delete() methods.  Note that dec_ref() does not call
  // Form::Delete(); unreferenced objects are discovered and deleted later
  // in the memory manager, if and when we run low on memory.  Calling Delete
  // immediately would require dealing with contained object pointers and
  // would slow down execution.
  void dec_ref()  {
    if (is_ptr() && !is_null()) {
      ref_--;
      assert(ref_ >= 0);
#ifdef SZL_IMMEDIATE_DELETE
      // TODO: for debugging, must somehow find the Proc object
      if (ref_ == 0)
        form_->Delete(NULL, this);
#endif
    }
  }

  // Use dec_ref_and_check() within Form::Delete() methods (which are only
  // used during GC) to discard references contained in Val objects.
  void dec_ref_and_check(Proc* proc)  {
    if (is_ptr() && !is_null()) {
      ref_--;
      assert(ref_ >= 0);
      if (ref_ == 0) {
        form_->Delete(proc, this);
      }
    }
  }

  // A "read only" ref count is one so high that any write to this value should
  // trigger a copy.  We want to make it large enough so that no normal object
  // should ever have a ref count anywhere near this high, and small enough so
  // that it is highly unlikely to overflow.
  static const int32 kInitialReadOnlyRefCount = kuint32max >> 2;

  // We want to test whether an object is "read only" using its ref count.
  // Although "read only" objects should never have their ref counts decremented
  // below the initial value, we set the threshold a bit low just in case.
  static const int32 kMinimumReadOnlyRefCount = kuint32max >> 3;

  // Set a high reference count so this value will never be considered "unique"
  // and so will never be modified.  Also used to indicate objects that will
  // not be moved (or must not, e.g. non-heap objects) during memory compaction.
  void set_readonly()  {
    if (is_ptr() && !is_null()) {
      assert(ref_ >= 0);
      ref_ = kInitialReadOnlyRefCount;
    }
  }
  // Garbage collection must be able to distinguish pre-allocated values from
  // chunk-allocated values; the former do not have block headers.
  bool is_readonly() const {
    if (is_ptr() && !is_null()) {
      assert(ref_ >= 0);
      return ref_ > kMinimumReadOnlyRefCount;
    } else {
      return true;
    }
  }

  // equality
  bool IsEqual(Val* val);
  Val* Cmp(Val* val);  // return <0, =0, >0, or NULL if not comparable.

  // narrowings
  BytesVal* as_bytes() {
    assert(is_bytes());
    assert(ref_ >= 0);
    return reinterpret_cast<BytesVal*>(this);
  }

  const BytesVal* as_bytes() const {
    assert(is_bytes());
    assert(ref_ >= 0);
    return reinterpret_cast<const BytesVal*>(this);
  }

  BoolVal* as_bool() {
    assert(is_bool());
    assert(ref_ >= 0);
    return reinterpret_cast<BoolVal*>(this);
  }

  FingerprintVal* as_fingerprint() {
    assert(is_fingerprint());
    assert(ref_ >= 0);
    return reinterpret_cast<FingerprintVal*>(this);
  }

  FloatVal* as_float() {
    assert(is_float());
    assert(ref_ >= 0);
    return reinterpret_cast<FloatVal*>(this);
  }

  IntVal* as_int() {
    assert(is_int());
    assert(is_smi() || ref_ >= 0);
    return reinterpret_cast<IntVal*>(this);
  }

  UIntVal* as_uint() {
    assert(is_uint());
    assert(ref_ >= 0);
    return reinterpret_cast<UIntVal*>(this);
  }

  StringVal* as_string() {
    assert(is_string());
    assert(ref_ >= 0);
    return reinterpret_cast<StringVal*>(this);
  }

  const StringVal* as_string() const {
    assert(is_string());
    assert(ref_ >= 0);
    return reinterpret_cast<const StringVal*>(this);
  }

  TimeVal* as_time() {
    assert(is_time());
    assert(ref_ >= 0);
    return reinterpret_cast<TimeVal*>(this);
  }

  ArrayVal* as_array() {
    assert(is_array());
    assert(ref_ >= 0);
    return reinterpret_cast<ArrayVal*>(this);
  }

  const ArrayVal* as_array() const {
    assert(is_array());
    assert(ref_ >= 0);
    return reinterpret_cast<const ArrayVal*>(this);
  }

  MapVal* as_map() {
    assert(is_map());
    assert(ref_ >= 0);
    return reinterpret_cast<MapVal*>(this);
  }

  TupleVal* as_tuple() {
    assert(is_tuple());
    assert(ref_ >= 0);
    return reinterpret_cast<TupleVal*>(this);
  }

  ClosureVal* as_closure() {
    assert(is_closure());
    assert(ref_ >= 0);
    return reinterpret_cast<ClosureVal*>(this);
  }

  IndexableVal* as_indexable() {
    assert(is_indexable());
    assert(ref_ >= 0);
    return reinterpret_cast<IndexableVal*>(this);
  }

  // formatting
  int Format(Proc* proc, Fmt::State* f);
  static int ValFmt(Fmt::State* f);  // implements %V

  // making unique (ref == 1)
  Val* Uniq(Proc* proc);

  // get the 64-bit value of a basic64 Val*
  uint64 basic64();

  // return the fingerprint of the Val*
  szl_fingerprint Fingerprint(Proc* proc);

  // direct access to ref_ from native code
  static size_t ref_offset() { return OFFSETOF_MEMBER(Val, ref_); }
  static size_t ref_size() { return sizeof(int32); }

 protected:
  // a Val header consists of the pointer to its form and a reference count
  // (there is a 1-to-1 correspondence between Forms and Types)
  Form* form_;
  // we don't use uint32 for the ref count to be able to detect
  // underflow to values < 0 (which indicate an implementation error)
  int32 ref_;
  // all remaining Val fields follow here via its subclasses
};


// ----------------------------------------------------------------------------
// Scalar values

class BoolVal: public Val {
 public:
  bool val() const  { assert(ref_ >= 0); return val_; }

  // direct access to val_ from native code
  static size_t val_offset() { return OFFSETOF_MEMBER(BoolVal, val_); }
  static size_t val_size() { return sizeof(bool); }

 private:
  bool val_;
  friend class BoolForm;
};


class FingerprintVal: public Val {
 public:
  szl_fingerprint val() const  { assert(ref_ >= 0); return val_; }

 private:
  szl_fingerprint val_;
  friend class FingerprintForm;
};


class FloatVal: public Val {
 public:
  szl_float val() const  { assert(ref_ >= 0); return val_; }

 private:
  szl_float val_;
  friend class FloatForm;
};


class IntVal: public Val {
 public:
  szl_int val() const {
    if (is_smi()) {
      // TODO: The (Val*) cast below is only to get gcc to shut up...
      return TaggedInts::as_smi((Val*)this);
    } else {
      assert(ref_ >= 0);
      return val_;
    }
  }

 private:
  szl_int val_;
  friend class IntForm;
};


class UIntVal: public Val {
 public:
  szl_uint val() const  { assert(ref_ >= 0); return val_; }

 private:
  szl_uint val_;
  friend class UIntForm;
};


class TimeVal: public Val {
 public:
  szl_time val() const  { assert(ref_ >= 0); return val_; }

 private:
  szl_time val_;
  friend class TimeForm;
};


// ----------------------------------------------------------------------------
// Indexables
//
// Indexables are values that can be indexed and that support slicing.
// The IndexableVal class factors out the length.

class IndexableVal: public Val {
 public:
  // accessors
  int length() const  { return length_; }

  bool legal_index(szl_int i) const  { return 0 <= i && i < length_; }

  void intersect_slice(szl_int* beg, szl_int* end, szl_int length);

  // direct access to length_ from native code
  static size_t length_offset() { return OFFSETOF_MEMBER(IndexableVal, length_); }
  static size_t length_size() { return sizeof(int); }

 protected:
  int length_;
};


// Slices are constructed using IndexableValWithOrigin. To save
// space, StringVal replicates some of this code instead of
// inheriting from IndexableValWithOrigin (unsliced StringVals
// do not contain an origin_ field).
class IndexableValWithOrigin: public IndexableVal {
 public:
  // accessors
  int origin() const  { return origin_; }

  void SetRange(int origin, int length) {
    assert(origin >= 0);
    assert(length >= 0);
    origin_ = origin;
    length_ = length;
  }

  void SetSubrange(int origin, int length) {
    assert(origin + length <= length_);
    SetRange(origin_ + origin, length);
  }

 private:
  int origin_;
};


// ----------------------------------------------------------------------------
// Arrays

class ArrayVal: public IndexableValWithOrigin {
 public:
  // accessors
  Val** base() {
    return (reinterpret_cast<Val**>(array_ + 1)) + origin();
  }

  Val*& at(int i)  { assert(legal_index(i)); return base()[i]; }
  Val** end() { return base() + length(); }

  bool is_unique() const  { return ref() == 1 && array_->ref() == 1; }

  // Assign to a slice.
  void PutSlice(Proc* proc, int beg, int end, ArrayVal* x);

  // Length as seen by program.
  int semantic_length() const  { return length_; }

 private:
  ArrayVal* array_;
  friend class ArrayForm;
};


// ----------------------------------------------------------------------------
// Bytes

// Uusally we want explicitly unsigned values, but sometimes
// we need char* pointers, so we have methods for both.
class BytesVal: public IndexableValWithOrigin {
 public:
  unsigned char* u_base() {
    return (reinterpret_cast<unsigned char*>(array_ + 1)) + origin();
  }

  char* base()  { return (reinterpret_cast<char*>(u_base())); }

  // Note that the elements are always unsigned
  unsigned char& at(int i)  { assert(legal_index(i)); return u_base()[i]; }

  bool is_unique() const  { return ref() == 1 && array_->ref() == 1; }

  // Assign to a slice.
  void PutSlice(Proc* proc, int beg, int end, BytesVal* x);

  // Length as seen by program.
  int semantic_length() const  { return length_; }

 private:
  BytesVal* array_;
  friend class BytesForm;
};


// ----------------------------------------------------------------------------
// Strings
//
// There are two kinds of StringVals: Those whose data is appended to this
// StringVal; and those whose data is appended to a different StringVal.
// From the point of view of the implementation, only the latter kind is
// called a slice. For slice StringVals, the array and origin fields are
// stored in a SliceInfo structure within the StringVal. For non-slice
// StringVals, the array and origin fields are missing, and the string
// data begins at the first byte of the info_ field. To distinguish a
// slice from a non-slice, we look at the size_ field. If it is negative,
// the StringVal is a slice; otherwise it is the total available allocated
// memory, including the space of the SliceInfo field. Because a string
// could be converted to a slice in place, we must always allocate space
// sufficient to store SliceInfo, even if the string is very short.


struct SliceInfo {
  StringVal* array;
  int origin;
};


// An OffsetMap holds information associated with a StringVal,
// most importantly the information required to make indexing
// reasonably fast.
class OffsetMap {
 public:
  static OffsetMap* New(Proc* proc);
  szl_int byte_offset(StringVal* val, szl_int rune_index);
  void Reset()  { index_ = 0; offset_ = 0; }

 private:
  // Cache of Rune index to byte offset mapping
  int index_;
  int offset_;

  // Prevent construction from outside the class (must use factory method)
  OffsetMap() { }
  friend class StringVal;  // exception: StringVal::ASCIIMap
};


class StringVal: public IndexableVal {
 public:
  bool is_unique() const  { return ref() == 1 && array()->ref() == 1; }

  // Sizing/slicing support
  void SetRange(Proc* proc, int origin, int length, int num_runes);
  void SetSubrange(Proc* proc, int origin, int length, int num_runes);

  // Various accessors
  unsigned char* u_base() {
    if (is_slice())
      return reinterpret_cast<unsigned char*>(&slice_.array->slice_) + slice_.origin;
    return reinterpret_cast<unsigned char*>(&slice_);
  }

  char* base()  { return (reinterpret_cast<char*>(u_base())); }
  bool is_ascii() const { return map_ == &ASCIIMap; }
  int num_runes() const { return num_runes_; }

  // Convert a rune index into the corresponding byte offset.
  szl_int byte_offset(Proc* proc, szl_int rune_index);

  // Accessing runes at a given byte offset. Note that putting a rune r1
  // at a byte offset b1 *may* change the offset of a given rune r2 at a
  // byte offset b2 > b1.
  Rune at(int byte_offset);
  void put(Proc* proc, int byte_offset, Rune r);

  // Assign to a slice.
  void PutSlice(Proc* proc, int beg, int end, StringVal* x);

  // Copy into a UTF-8 C++ string.
  // (Returns a C++ string instead of a C string so that the memory for the
  // character buffer is automatically managed.)
  string cpp_str(Proc* proc);

  // Make a copy of the string and NUL-terminate it
  char* c_str(char* buf, int nbuf);

  // (Pre-)Allocate an offset map for the string. Use this only for compile-time
  // and static strings with the proper proc. This will make sure that the maps
  // for those strings won't be collected after a Sawzall run (because they are
  // allocated with the memory associated with a different proc).
  void AllocateOffsetMap(Proc* proc);

  // direct access to num_runes_ from native code
  static size_t num_runes_offset() { return OFFSETOF_MEMBER(StringVal, num_runes_); }
  static size_t num_runes_size() { return sizeof(int); }

  // Length as seen by program.
  int semantic_length() const  { return num_runes_; }

 private:
  int size_;  // actual number of bytes allocated after this header, or < 0 if slice
  int num_runes_;
  OffsetMap* map_;   // is NULL, &ASCIIMap, or from OffsetMap::New [uses ALLOC]
  SliceInfo slice_;  // must be last field!

  bool is_slice() const  { return size_ < 0; }

  int size() const {
    if (is_slice())
      return 0;
    return size_;
  }

  StringVal* array() {
    if (is_slice())
      return slice_.array;
    return this;
  }

  const StringVal* array() const {
    if (is_slice())
      return slice_.array;
    return this;
  }

  int origin() const {
    if (is_slice())
      return slice_.origin;
    return 0;
  }

  // implementation of PutSlice, also used by put()
  void PutSliceImpl(Proc* proc, int dst_offset, int dst_size, int dst_runes,
                    char* src, int src_size, int src_runes);

  static OffsetMap ASCIIMap;
  friend class StringForm;
};


// ----------------------------------------------------------------------------
// Maps

class MapVal: public Val {
 public:
  Map* map() const { return map_; }  // BUG: this shouldn't be here; only to make printing easy for now
  Val* Fetch(Val* key);
  void Insert(Proc* proc, Val* key, Val* value);
  void InitMap(Proc* proc, int occupancy, bool exact);
  int64 occupancy() { return map_->occupancy(); }
  void set_map(Map* map)  { map_ = map; }
  bool is_unique() const  { return ref() == 1; }

 private:
  Map* map_;
  friend class MapForm;
};


// ----------------------------------------------------------------------------
// Tuples

class TupleVal: public Val {
 public:
  // accessors
  Val** base()  { return reinterpret_cast<Val**>(this + 1); }
  // Field access.  We may not allocate space (slots) for fields that are never
  // used.  Use field_at() to access a field by Field node; use slot_at() only
  // if the field's slot index is known (e.g., in the engine).
  Val*& field_at(Field* f) { return slot_at(f->slot_index()); }
  // In the engine and a few other places  we access the slots directly.
  Val*& slot_at(int i) { assert(legal_index(i)); return base()[i]; }
  static size_t slot_offset(int i) { return sizeof(TupleVal) + i*sizeof(Val*); }
  bool is_unique() const  { return ref() == 1; }

  // support for proto tuple inproto bits; same index issue applies
  void clear_field_bit_at(TupleType* t, Field* f) {
    ClearBit(base(), t->inproto_index(f));
  }
  void set_field_bit_at(TupleType* t, Field* f) {
    SetBit(base(), t->inproto_index(f));
  }
  bool field_bit_at(TupleType* t, Field* f) {
    return TestBit(base(), t->inproto_index(f));
  }
  void clear_slot_bit_at(int i)  { ClearBit(base(), i); }
  void set_slot_bit_at(int i)  { SetBit(base(), i); }
  bool slot_bit_at(int i)  { return TestBit(base(), i); }

 private:
  bool legal_index(szl_int i) const  { return 0 <= i && i < type()->as_tuple()->nslots(); }
  friend class TupleForm;
};


// ----------------------------------------------------------------------------
// Closures

class ClosureVal: public Val {
 public:
  // accessors
  Instr* entry() const  { return entry_; }
  Frame* context() const  { return context_; }

  // compute the dynamic level (used for fingerprint; could use for equality)
  int dynamic_level(Proc* proc) const;

  // direct access to entry_ and context_ from native code
  static size_t entry_offset() { return OFFSETOF_MEMBER(ClosureVal, entry_); }
  static size_t entry_size() { return sizeof(Instr*); }
  static size_t context_offset() { return OFFSETOF_MEMBER(ClosureVal, context_); }
  static size_t context_size() { return sizeof(Frame*); }

 private:
  Instr* entry_;
  Frame* context_;
  friend class ClosureForm;
};


}  // namespace sawzall
