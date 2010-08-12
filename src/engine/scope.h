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

// A Scope holds a set of uniquely named, or anonymous, objects. Named
// objects can be looked up (using their names). Indexed-based access permits
// simple iteration over all objects in a scope. Scopes are used to maintain
// the set of unique identifiers of a Sawzall scope.

class Scope {
 public:
  // Creation
  static Scope* New(Proc* proc);
  
  // Index-based access. All scope entries can be retrieved with an index i
  // in the range 0 <= i && i < num_entries().
  Object* entry_at(int i) const  { return list_[i]; }
  int num_entries() const  { return list_.length(); }
  bool is_empty() const  { return num_entries() == 0; }
  
  // Inserting a new entry - fails (returns false or dies) if an entry
  // with the same name exists already in the scope. All anonymous
  // objects are considered different from each other.
  // (Complexity O(num_entries()) - but scopes are fairly small.
  // Can be easily changed by using a hash map instead of a list).
  bool Insert(Object* obj);
  void InsertOrDie(Object* obj);

  // Overloads existing intrinsics.  Attempts to insert first,
  // and failing that, overloads the existing intrinsic if possible.
  bool InsertOrOverload(Intrinsic* fun);
  void InsertOrOverloadOrDie(Intrinsic* fun);

  // Simulate multiple inheritance.
  bool Insert(BadExpr* x);
  bool Insert(Field* x);
  bool Insert(Intrinsic* x);
  bool Insert(Literal* x);
  bool Insert(TypeName* x);
  bool Insert(VarDecl* x);
  void InsertOrDie(BadExpr* x);
  void InsertOrDie(Field* x);
  void InsertOrDie(Intrinsic* x);
  void InsertOrDie(Literal* x);
  void InsertOrDie(TypeName* x);
  void InsertOrDie(VarDecl* x);
  
  // Looking up an entry - fails (returns NULL or dies) if no entry
  // with the given name exists in the scope. Anonymous objects
  // cannot be retrieved via lookup.
  // (Complexity O(num_entries()) - but scopes are fairly small.
  // Can be easily changed by using a hash map instead of a list).
  Object* Lookup(szl_string name) const;
  Object* Lookup(szl_string name, int length) const;
  Object* LookupOrDie(szl_string name) const;
  
  // Looking up a (proto tuple) field entry by tag. Returns NULL
  // if no field with the same tag exists, returns the field otherwise.
  // tag must be > 0.
  Field* LookupByTag(int tag) const;

  // Cloning
  static void Clone(CloneMap* cmap, Scope* src, Scope* dst);

  // Print the scope's content.
  void Print() const;

  // For tuple scopes.
  TupleType* tuple() const  { return tuple_; }
  void set_tuple(TupleType* tuple)  { tuple_ = tuple; }

 private:
  List<Object*> list_;
  TupleType* tuple_;

  // Prevent construction from outside the class (must use factory method)
  Scope(Proc* proc)
    : list_(proc), tuple_(NULL) {
  }
};

} // namespace sawzall
