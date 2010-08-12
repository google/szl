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


// to avoid including utf/utf.h
typedef signed int Rune;
const int Runeself = 0x80;

class Proc;


// ----------------------------------------------------------------------------
// A couple of useful bit vector operations.
// set is a bit vector, i is the bit index, starting with 0.

static inline void ClearBit(void* set, unsigned int i) {
  uint32* a = reinterpret_cast<uint32*>(set);
  const unsigned int s = 5;  // 1 << s == 32, number of bits in int32
  const unsigned int m = (1 << s) - 1;  // bit mask for 0..31
  a[i >> s] &= ~(1 << (i & m));
}

static inline void SetBit(void* set, unsigned int i) {
  uint32* a = reinterpret_cast<uint32*>(set);
  const unsigned int s = 5;  // 1 << s == 32, number of bits in int32
  const unsigned int m = (1 << s) - 1;  // bit mask for 0..31
  a[i >> s] |= 1 << (i & m);
}


static inline bool TestBit(void* set, unsigned int i) {
  uint32* a = reinterpret_cast<uint32*>(set);
  const unsigned int s = 5;  // 1 << s == 32, number of bits in int32
  const unsigned int m = (1 << s) - 1;  // bit mask for 0..31
  return (a[i >> s] & (1 << (i & m))) != 0;
}


// ----------------------------------------------------------------------------
// I/O helpers

// Return the (lexically determined) directory of a file.
char* FileDir(Proc* proc, const char* file);


// ----------------------------------------------------------------------------
// General helper functions & macros
// (define these as macros if the compiler doesn't produce good code)

template <class T>
static inline bool IsPowerOf2(T x) {
  return (x & (x-1)) == 0;
}


template <class T>
static inline T Align(T x, size_t size) {
  assert(IsPowerOf2(size));
  return (x + (size-1)) & ~(size-1);
}


// ----------------------------------------------------------------------------
// List is a template for very light-weight lists. We are not using
// the STL because we want full control over space and speed and
// understand the code.

template <class T>
class List {
 public:
  // Creation
  // Note: We allow constructor based creation because of
  // stack-allocated lists.
  static List<T>* New(Proc* proc) {
    // allocate the List object via proc heap!
    // initialization happens via constructor
    return new (Allocate(proc, sizeof(List))) List<T>(proc);
  }
  
  List(Proc* proc) {
    proc_ = proc;
    data_ = NULL;
    length_ = 0;
  }
  
  ~List() {
    if (data_ != NULL)
      Deallocate(proc_, data_);
  }
  
  bool valid_index(int i) const {
    return 0 <= i && i < length_;
  }
  
  void Append(T x) {
    if (IsPowerOf2(length_)) {
      // reached end of allocated space => double data_ length
      T* old = data_;
      int new_length = (length_ == 0 ? 1 : 2 * length_);
      // allocate the List data array via proc_ heap (explicitly freed)
      data_ = new (Allocate(proc_, new_length * sizeof(T))) T[new_length];
      // copy list
      memmove(data_, old, length_ * sizeof(T));
      if (old != NULL)
        Deallocate(proc_, old);
    }
    data_[length_++] = x;
  }

  T& operator [] (int i) const {
    assert(valid_index(i));
    return data_[i];
  }

  T& at(int i) const {
    assert(valid_index(i));
    return data_[i];
  }

  T& last()  { return (*this)[length_ - 1]; }

  T* data()  { return data_; }

  int length() const  { return length_; }
  
  bool is_empty() const  { return length_ == 0; }
  
  bool is_sorted(int (*cmp)(const T* x, const T* y)) const  {
    int i = 1;
    while (i < length() && cmp(&at(i-1), &at(i)) <= 0)
      i++;
    return i >= length();
  }
  
  // Sort all list entries (using QuickSort)
  void Sort(int (*cmp)(const T* x, const T* y)) {
    qsort(data(), length(), sizeof(T),
          reinterpret_cast<int(*)(const void*, const void*)>(cmp));
    assert(is_sorted(cmp));
  }
  
  // Binary search of the list for x. The result is an index i.
  // If i is valid (i.e. 0 <= i < length()), then the following
  // holds: at(i) <= x < at(i+1).
  // Assumes that the list is sorted in increasing order.
  // From E.W. Dijkstra, "Methodik des Programmierens".
  int BinarySearch(T x, int (*cmp)(const T* x, const T* y)) const {
    assert(is_sorted(cmp));
    if (length() > 0) {
      int low = 0;
      int high = length();
      while (high != low + 1) {
        int mid = (high + low) / 2;  // low < mid < high
        if (cmp(&at(mid), &x) > 0)  // x < at(mid)
          high = mid;
        else  // at(mid) <= x
          low = mid;
      }
      // high == low + 1
      return low;
    }
    // not found
    return -1;
  }

  // Linear search of the list for x. The result is an index i.
  int IndexOf(T x) const {
    int i = length();
    while (--i >= 0 && at(i) != x)
      ;
    return i;  // will be -1 if not found
  }

  void Clear() {
    if (data_ != NULL)
      Deallocate(proc_, data_);
    data_ = NULL;
    length_ = 0;
  }

  void Truncate(int length) {
    // For debugging and test purposes only - causes excessive reallocation.
    // If we ever need to use this in production we should add a field
    // to save the allocated size.
    assert(length >= 0 && length <= length_);
    length_ = length;
  }

  List<T>* Copy(Proc* proc) {
    List<T>* nlist = List<T>::New(proc);
    for (int i = 0; i < this->length(); i++)
      nlist->Append(this->at(i));
    return nlist;
  }

 private:
  Proc* proc_;
  T* data_;
  int length_;
};

// Returns null terminated char* corresponding to src. It is the callers
// responsibility to delete the memory when it is done with it.
char* CharList2CStr(List<char>* src);

// ----------------------------------------------------------------------------
// Stack is a template for very light-weight stacks. We are not using
// the STL because we want full control over space and speed and
// understand the code.

template <class T>
class Stack {
 public:
  // Creation
  // Note: We allow constructor based creation because of
  // stack-allocated lists.
  static Stack<T>* New(Proc* proc) {
    // initialization happens via constructor
    // allocate via proc heap!
    return new (Allocate(proc, sizeof(Stack))) Stack<T>;
  }
  
  Stack(Proc* proc)
    : list_(proc),
      sp_(0) {
  }

  ~Stack() {
    Clear();
  }
  
  const T& top() const {
    return nth_top(0);
  }

  T& mutable_top() {
    assert(sp_ > 0);
    return list_[sp_ - 1];
  }

  const T& nth_top(int n) const {
    assert(0 <= n && n < sp_);
    return list_[sp_ - n - 1];
  }

  T pop() {
    assert(sp_ > 0);
    return list_[--sp_];
  }

  void push(T x) {
    if (sp_ == list_.length())
      list_.Append(x);
    else
      list_[sp_] = x;
    sp_++;
  }

  bool is_empty() const {
    return sp_ == 0;
  }

  bool is_present(T x) {
    for (int i = 0; i < sp_; i++)
      if (x == list_[i])
        return true;
    return false;
  }

  int length() const {
    return sp_;
  }
  
  void Clear() {
    sp_ = 0;
  }

 private:
  List<T> list_;
  int sp_;
};


// ----------------------------------------------------------------------------
// CloneMap is a helper class for cloning Node and Type object graphs.
// These graphs are almost trees; they have a few nodes referenced from more
// than one place, and CloneMap manages those references.

// TODO: move this so utils.h uses no compiler classes?
// Or add the SymbolTable and Function stuff in a subclass?

class SymbolTable;
class FileLine;

class CloneMap {
 public:
  CloneMap(Proc* proc, SymbolTable* table, Function* context, FileLine* fl) :
      proc_(proc), table_(table), context_(context),
      fileline_(fl), map_(NULL)  { }
  ~CloneMap();
  Proc* proc() const  { return proc_; }
  SymbolTable* table() const  { return table_; }
  Function* context() const  { return context_; }
  FileLine* file_line() const  { return fileline_; }

  template<class T> void CloneList(List<T*>* src, List<T*>* dst);
  template<class T> List<T*>* CloneList(List<T*>* src);
  template<class T> void AlwaysCloneStmtList(List<T*>* src, List<T*>* dst);
  template<class T> List<T*>* AlwaysCloneStmtList(List<T*>* src);
  template<class T> T* Find(T* original);
  template<class T> void Insert(T* original, T* clone);
  template<class T> T* CloneOrNull(T* original);
  template<class T> T* CloneStmtOrNull(T* original);

  // Like CloneList, but the list elements must already be cloned.
  template<class T> void CloneListOfAlreadyCloned(List<T*>* src, List<T*>* dst);

 private:
  void* FindAny(void* key);
  void InsertAny(void* key, void* value);

  class CMap;   // do not introduce dependencies on the underlying map
  Proc* proc_;
  SymbolTable* table_;   // for adding cloned functions
  Function* context_;
  FileLine* fileline_;   // substitute location in cloned nodes
  CMap* map_;
};


template<class T>
void CloneMap::CloneList(List<T*>* src, List<T*>* dst) {
  for (int i = 0; i < src->length(); i++)
    dst->Append(src->at(i)->Clone(this));
}


template<class T>
List<T*>* CloneMap::CloneList(List<T*>* src) {
  if (src == NULL)
    return NULL;
  List<T*>* dst = List<T*>::New(proc_);
  CloneList(src, dst);
  return dst;
}


template<class T>
void CloneMap::AlwaysCloneStmtList(List<T*>* src, List<T*>* dst) {
  for (int i = 0; i < src->length(); i++)
    dst->Append(src->at(i)->CloneStmt(this));
}


template<class T>
List<T*>* CloneMap::AlwaysCloneStmtList(List<T*>* src) {
  if (src == NULL)
    return NULL;
  List<T*>* dst = List<T*>::New(proc_);
  AlwaysCloneStmtList(src, dst);
  return dst;
}


template<class T>
void CloneMap::CloneListOfAlreadyCloned(List<T*>* src, List<T*>* dst) {
  for (int i = 0; i < src->length(); i++) {
    T* original = src->at(i);
    T* clone = Find(original);
    if (clone == NULL) {
      // Not in map; must be a node that does not get cloned.
      clone = original->Clone(this);
      assert(clone == original);
    }
    dst->Append(clone);
  }
}


template<class T>
T* CloneMap::CloneOrNull(T* original) {
  if (original == NULL)
    return NULL;
  else
    return original->Clone(this);
}


template<class T>
T* CloneMap::CloneStmtOrNull(T* original) {
  if (original == NULL)
    return NULL;
  else
    return original->CloneStmt(this);
}


template<class T>
T* CloneMap::Find(T* original) {
  if (original == NULL)
    return NULL;
  void* p = FindAny(original);
  if (p == NULL)
    return NULL;
  // Assumes no multiple inheritance.
  // We know the type is right because the original and clone have the same
  // type.
  T* clone = static_cast<T*>(p);
  assert(clone != NULL);
  return clone;
}


template<class T>
void CloneMap::Insert(T* original, T* clone) {
  InsertAny(original, clone);
}


}  // namespace sawzall
