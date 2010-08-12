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

#include <time.h>
#include <assert.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/proc.h"


namespace sawzall {


Scope* Scope::New(Proc* proc) {
  Scope* s = NEWP(proc, Scope);
  return s;
}


bool Scope::Insert(Object* obj) {
  assert(obj != NULL);
  if (obj->is_anonymous() || Lookup(obj->name()) == NULL) {
    // object doesn't exist yet in this scope => insert it
    list_.Append(obj);
    obj->set_scope(this);
    return true;
  } else {
    // object exists already
    return false;
  }
}


void Scope::InsertOrDie(Object* obj) {
  if (!Insert(obj))
    FatalError("identifier '%s' already declared in this scope", obj->name());
}


bool Scope::InsertOrOverload(Intrinsic* fun) {
  assert(fun != NULL);
  if (Insert(fun))
    return true;

  Object* obj = Lookup(fun->name());
  Intrinsic* existing = obj->AsIntrinsic();
  if (existing != NULL && existing->add_overload(fun)) {
    fun->object()->set_scope(this);
    return true;
  }

  return false;
}


void Scope::InsertOrOverloadOrDie(Intrinsic* fun) {
  if (!InsertOrOverload(fun))
    FatalError("identifier '%s' already declared in this scope", fun->name());
}


Object* Scope::Lookup(szl_string name) const {
  return Lookup(name, strlen(name));
}


static bool SamePossiblyDottedName(szl_string dotted_name, szl_string name,
                                      int length) {
  const char* p;
  const char* q;
  for (p = dotted_name, q = name; *p != '\0' && q < name + length;
       p++, q++) {
    if (*p != *q) {
      // Possible mismatch, check for the exception case.
      if (*p == '.' && *q == '_')
        continue;      // Was '.' vs '_', treat it as a match
      else
        return false;  // Not '.' vs '_', really was a mismatch
    }
  }
  return (*p == '\0' && q == name + length);
}


Object* Scope::Lookup(szl_string name, int length) const {
  assert(name != NULL);
  for (int i = 0; i < list_.length(); i++) {
    Object* obj = list_[i];
    if (!obj->is_anonymous()) {
      if (memcmp(obj->name(), name, length) == 0 && obj->name()[length] == '\0')
        return obj;
      // Temporarily find dotted names (package-qualified names using dot as
      // the separator) when given a name that matches except for using
      // underscores where the first name uses dots.
      if (obj->AsTypeName() != NULL && obj->type()->is_tuple() &&
          obj->type()->as_tuple()->is_message() &&
          SamePossiblyDottedName(obj->name(), name, length)) {
        return obj;
      }
    }
  }
  return NULL;
}


Object* Scope::LookupOrDie(szl_string name) const {
  Object* obj = Lookup(name);
  if (obj == NULL)
    FatalError("identifier '%s' not found in this scope", name);
  return obj;
}


Field* Scope::LookupByTag(int tag) const {
  assert(tag > 0);  // tags must be > 0, 0 indicates no tag
  for (int i = 0; i < list_.length(); i++) {
    Field* field = list_[i]->AsField();
    if (field != NULL && field->tag() == tag)
      return field;
  }
  return NULL;
}


void Scope::Clone(CloneMap* cmap, Scope* src, Scope* dst) {
  // Scope entries are just for lookup, so we never clone them; instead
  // we rely on their having already been cloned where originally written.
  for (int i = 0; i < src->num_entries(); i++) {
    // Block scope entries can be VarDecl, TypeName, QuantVarDecl
    Object* obj = src->entry_at(i);
    if (obj->AsVarDecl() != NULL) {
      VarDecl* vardecl = cmap->Find(obj->AsVarDecl());
      assert(vardecl != NULL);
      dst->InsertOrDie(vardecl);
    } else if (obj->AsTypeName() != NULL) {
      TypeName* tname = cmap->Find(obj->AsTypeName());
      assert(tname != NULL);
      dst->InsertOrDie(tname);
    } else {
      ShouldNotReachHere();
    }
  }
}


void Scope::Print() const {
  if (is_empty()) {
    F.print("{}\n");
  } else {
    F.print("{\n");
    for (int i = 0; i < num_entries(); i++) {
      Object* obj = entry_at(i);
      F.print("  %s: %T;", obj->display_name(), obj->type());
      // print more detail, if possible
      VarDecl* var = obj->AsVarDecl();
      if (var != NULL) {
        const char* kind = "";
        if (var->is_local())
          kind = "local";
        else if (var->is_param())
          kind = "parameter";
        else if (var->is_static())
          kind = "static";
        else
          ShouldNotReachHere();
        F.print("  # %s, offset = %d", kind, var->offset());
      }
      F.print("\n");
    }
    F.print("}\n");
  }
}


// Simulate multiple inheritance.
// These should be in the header but that introduces too many dependencies.
bool Scope::Insert(BadExpr* x)  { return Insert(x->object()); }
bool Scope::Insert(Field* x)  { return Insert(x->object()); }
bool Scope::Insert(Intrinsic* x)  { return Insert(x->object()); }
bool Scope::Insert(Literal* x)  { return Insert(x->object()); }
bool Scope::Insert(TypeName* x)  { return Insert(x->object()); }
bool Scope::Insert(VarDecl* x)  { return Insert(x->object()); }
void Scope::InsertOrDie(BadExpr* x)  { InsertOrDie(x->object()); }
void Scope::InsertOrDie(Field* x)  { InsertOrDie(x->object()); }
void Scope::InsertOrDie(Intrinsic* x)  { InsertOrDie(x->object()); }
void Scope::InsertOrDie(Literal* x)  { InsertOrDie(x->object()); }
void Scope::InsertOrDie(TypeName* x)  { InsertOrDie(x->object()); }
void Scope::InsertOrDie(VarDecl* x)  { InsertOrDie(x->object()); }


} // namespace sawzall
