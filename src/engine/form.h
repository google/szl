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

// Some primitives for hashing, used by Maps, needed by Forms.

// Note that we rely on MapHashCat() being associative and commutative so that
// we can combine hash values for elements of a container in any order.
const inline uint32 MapHashCat(uint32 h1, uint32 h2)  { return h1 ^ h2; }
const szl_fingerprint kFingerSeed();


// ----------------------------------------------------------------------------
// Forms implement the interface of Val objects. Generally, there is
// a 1-to-1 correspondence between Types and Forms. However,
// an ArrayVal of a particular ArrayType may have different forms
// depending on whether the array is sliced or not.
// TODO: Implement this feature.

class Form {
 public:
  // Form interface
  // initialization
  void Initialize(Type* type)  { type_ = type; }

  // accessors
  Type* type() const  { return type_; }

  // Val interface

  // memory management
  virtual void Delete(Proc* proc, Val* v);
  virtual void AdjustHeapPtrs(Proc* proc, Val* v) {}
  virtual void CheckHeapPtrs(Proc* proc, Val* v) {}

  // 64-bit value of a basic64 Val
  virtual uint64 basic64(Val* v) const { ShouldNotReachHere(); return 0; }

  // create scalar value of given type
  virtual Val* NewValBasic64(Proc* proc, Type* type, uint64 bits) { ShouldNotReachHere(); return NULL; }

  // equality
  virtual bool IsEqual(Val* v1, Val* v2) const = 0;
  virtual Val* Cmp(Val* v1, Val* v2) const = 0;

  // printing
  virtual int Format(Proc* proc, Fmt::State* f, Val* v) const = 0;

  // uniquing
  virtual bool IsUnique(const Val* v) const;
  virtual Val* Uniq(Proc* proc, Val* v) const = 0;

  // hashing
  virtual uint32 Hash(Val* v) const = 0;

  // fingerprinting
  virtual szl_fingerprint Fingerprint(Proc* proc, Val* v) const = 0;

  virtual ~Form() {}

 private:
  Type* type_;
};


// ----------------------------------------------------------------------------
// Basic forms

class BoolForm: public Form {
 public:
  // allocation
  BoolVal* NewVal(Proc* proc, bool val);
  virtual Val* NewValBasic64(Proc* proc, Type* type, uint64 bits);

  // Val interface
  virtual bool IsEqual(Val* v1, Val* v2) const;
  virtual Val* Cmp(Val* v1, Val* v2) const;
  virtual int Format(Proc* proc, Fmt::State* f, Val* v) const;
  virtual Val* Uniq(Proc* proc, Val* v) const;
  virtual uint32 Hash(Val* v) const;
  virtual szl_fingerprint Fingerprint(Proc* proc, Val* v) const;
  virtual uint64 basic64(Val* v) const;
};


class IntForm: public Form {
 public:
  // allocation
  IntVal* NewVal(Proc* proc, szl_int x) {
    if (TaggedInts::fits_smi(x))
      return reinterpret_cast<IntVal*>(TaggedInts::MakeVal(x));
    else
      return NewValInternal(proc, x);
  }

  virtual Val* NewValBasic64(Proc* proc, Type* type, uint64 bits);

  // Val interface
  virtual bool IsEqual(Val* v1, Val* v2) const;
  virtual Val* Cmp(Val* v1, Val* v2) const;
  virtual int Format(Proc* proc, Fmt::State* f, Val* v) const;
  virtual Val* Uniq(Proc* proc, Val* v) const;
  virtual uint32 Hash(Val* v) const;
  virtual szl_fingerprint Fingerprint(Proc* proc, Val* v) const;
  virtual uint64 basic64(Val* v) const;

 private:
  IntVal* NewValInternal(Proc* proc, szl_int x);
  friend class TaggedInts;
};


class UIntForm: public Form {
 public:
  // allocation
  UIntVal* NewVal(Proc* proc, szl_uint x);
  virtual Val* NewValBasic64(Proc* proc, Type* type, uint64 bits);

  // Val interface
  virtual bool IsEqual(Val* v1, Val* v2) const;
  virtual Val* Cmp(Val* v1, Val* v2) const;
  virtual int Format(Proc* proc, Fmt::State* f, Val* v) const;
  virtual Val* Uniq(Proc* proc, Val* v) const;
  virtual uint32 Hash(Val* v) const;
  virtual szl_fingerprint Fingerprint(Proc* proc, Val* v) const;
  virtual uint64 basic64(Val* v) const;
};


class FingerprintForm: public Form {
 public:
  // allocation
  FingerprintVal* NewVal(Proc* proc, szl_fingerprint x);
  virtual Val* NewValBasic64(Proc* proc, Type* type, uint64 bits);

  // Val interface
  virtual bool IsEqual(Val* v1, Val* v2) const;
  virtual Val* Cmp(Val* v1, Val* v2) const;
  virtual int Format(Proc* proc, Fmt::State* f, Val* v) const;
  virtual Val* Uniq(Proc* proc, Val* v) const;
  virtual uint32 Hash(Val* v) const;
  virtual szl_fingerprint Fingerprint(Proc* proc, Val* v) const;
  virtual uint64 basic64(Val* v) const;
};


class FloatForm: public Form {
 public:
  // allocation
  FloatVal* NewVal(Proc* proc, szl_float x);
  virtual Val* NewValBasic64(Proc* proc, Type* type, uint64 bits);

  // Val interface
  virtual bool IsEqual(Val* v1, Val* v2) const;
  virtual Val* Cmp(Val* v1, Val* v2) const;
  virtual int Format(Proc* proc, Fmt::State* f, Val* v) const;
  virtual Val* Uniq(Proc* proc, Val* v) const;
  virtual uint32 Hash(Val* v) const;
  virtual szl_fingerprint Fingerprint(Proc* proc, Val* v) const;
  virtual uint64 basic64(Val* v) const;
};


class TimeForm: public Form {
 public:
  // allocation
  TimeVal* NewVal(Proc* proc, szl_time x);
  virtual Val* NewValBasic64(Proc* proc, Type* type, uint64 bits);

  // Val interface
  virtual bool IsEqual(Val* v1, Val* v2) const;
  virtual Val* Cmp(Val* v1, Val* v2) const;
  virtual int Format(Proc* proc, Fmt::State* f, Val* v) const;
  virtual Val* Uniq(Proc* proc, Val* v) const;
  virtual uint32 Hash(Val* v) const;
  virtual szl_fingerprint Fingerprint(Proc* proc, Val* v) const;
  virtual uint64 basic64(Val* v) const;
};


// ----------------------------------------------------------------------------
// BytesForm

class BytesForm: public Form {
 public:
  // allocation
  BytesVal* NewVal(Proc* proc, int length);
  BytesVal* NewValInit(Proc* proc, int length, const char* x);
  // See ref count issues discussed below for StringForm::NewSlice().
  BytesVal* NewSlice(Proc* proc, BytesVal* v, int origin, int length);
  virtual void Delete(Proc* proc, Val* v);
  virtual void AdjustHeapPtrs(Proc* proc, Val* v);
  virtual void CheckHeapPtrs(Proc* proc, Val* v);

  // Val interface
  virtual bool IsEqual(Val* v1, Val* v2) const;
  virtual Val* Cmp(Val* v1, Val* v2) const;
  virtual int Format(Proc* proc, Fmt::State* f, Val* v) const;
  virtual bool IsUnique(const Val* v) const;
  virtual Val* Uniq(Proc* proc, Val* v) const;
  virtual uint32 Hash(Val* v) const;
  virtual szl_fingerprint Fingerprint(Proc* proc, Val* v) const;
};


// ----------------------------------------------------------------------------
// StringForm

class StringForm: public Form {
 public:
  // allocation
  StringVal* NewVal(Proc* proc, int length, int num_runes);
  StringVal* NewValInitCStr(Proc* proc, szl_string str);
  StringVal* NewValInit(Proc* proc, int length, const char* str);
  // If the ref count of "v" is one then the value is reused and the return
  // value is "v".  Otherwise the ref count of "v" is decremented and a new
  // value is created and returned.  If the caller is not abandoning its
  // original ref to "v" it should increment v's ref count before the call.
  StringVal* NewSlice(Proc* proc, StringVal* v, int origin, int length, int num_runes);
  virtual void Delete(Proc* proc, Val* v);
  virtual void AdjustHeapPtrs(Proc* proc, Val* v);
  virtual void CheckHeapPtrs(Proc* proc, Val* v);

  // Val interface
  virtual bool IsEqual(Val* v1, Val* v2) const;
  virtual Val* Cmp(Val* v1, Val* v2) const;
  virtual int Format(Proc* proc, Fmt::State* f, Val* v) const;
  virtual bool IsUnique(const Val* v) const;
  virtual Val* Uniq(Proc* proc, Val* v) const;
  virtual uint32 Hash(Val* v) const;
  virtual szl_fingerprint Fingerprint(Proc* proc, Val* v) const;
};


// ----------------------------------------------------------------------------
// ArrayForm

class ArrayForm: public Form {
 public:
  // allocation
  ArrayVal* NewVal(Proc* proc, int length);
  ArrayVal* NewValInit(Proc* proc, int length, Val* init_val);
  // See ref count issues discussed above for StringForm::NewSlice().
  ArrayVal* NewSlice(Proc* proc, ArrayVal* v, int origin, int length);
  virtual void Delete(Proc* proc, Val* v);
  virtual void AdjustHeapPtrs(Proc* proc, Val* v);
  virtual void CheckHeapPtrs(Proc* proc, Val* v);

  // Val interface
  virtual bool IsEqual(Val* v1, Val* v2) const;
  virtual Val* Cmp(Val* v1, Val* v2) const;
  virtual int Format(Proc* proc, Fmt::State* f, Val* v) const;
  virtual bool IsUnique(const Val* v) const;
  virtual Val* Uniq(Proc* proc, Val* v) const;
  virtual uint32 Hash(Val* v) const;
  virtual szl_fingerprint Fingerprint(Proc* proc, Val* v) const;
};


// ----------------------------------------------------------------------------
// MapForm

class MapForm: public Form {
 public:
  // allocation
  MapVal* NewVal(Proc* proct);
  MapVal* NewValInit(Proc* proc, int occupancy, bool exact);
  virtual void Delete(Proc* proc, Val* v);
  virtual void AdjustHeapPtrs(Proc* proc, Val* v);
  virtual void CheckHeapPtrs(Proc* proc, Val* v);

  // Val interface
  virtual bool IsEqual(Val* v1, Val* v2) const;
  virtual Val* Cmp(Val* v1, Val* v2) const;
  virtual int Format(Proc* proc, Fmt::State* f, Val* v) const;
  virtual Val* Uniq(Proc* proc, Val* v) const;
  virtual uint32 Hash(Val* v) const;
  virtual szl_fingerprint Fingerprint(Proc* proc, Val* v) const;
};


// ----------------------------------------------------------------------------
// TupleForm

class TupleForm: public Form {
 public:
  enum InitMode { ignore_inproto, clear_inproto, set_inproto};
  TupleVal* NewVal(Proc* proc, InitMode mode);
  virtual void Delete(Proc* proc, Val* v);
  virtual void AdjustHeapPtrs(Proc* proc, Val* v);
  virtual void CheckHeapPtrs(Proc* proc, Val* v);

  // Val interface
  virtual bool IsEqual(Val* v1, Val* v2) const;
  virtual Val* Cmp(Val* v1, Val* v2) const;
  virtual int Format(Proc* proc, Fmt::State* f, Val* v) const;
  virtual Val* Uniq(Proc* proc, Val* v) const;
  virtual uint32 Hash(Val* v) const;
  virtual szl_fingerprint Fingerprint(Proc* proc, Val* v) const;
};


// ----------------------------------------------------------------------------
// ClosureForm

class ClosureForm: public Form {
 public:
  ClosureVal* NewVal(Proc* proc, Instr* entry, Frame* context);

  // Val interface
  virtual bool IsEqual(Val* v1, Val* v2) const;
  virtual Val* Cmp(Val* v1, Val* v2) const;
  virtual int Format(Proc* proc, Fmt::State* f, Val* v) const;
  virtual Val* Uniq(Proc* proc, Val* v) const;
  virtual uint32 Hash(Val* v) const;
  virtual szl_fingerprint Fingerprint(Proc* proc, Val* v) const;
};


}  // namespace sawzall
