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
#include <math.h>
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
#include "engine/symboltable.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/help.h"


namespace sawzall {


void Help::PrintUniverse() {
  assert(SymbolTable::is_initialized());
  Scope* universe = SymbolTable::universe();
  for (int i = 0; i < universe->num_entries(); i++) {
    Object* obj = universe->entry_at(i);
    if (!obj->is_internal())  // don't print internal identifiers
      F.print("%s ", obj->name());
  }
  F.print("\n");
}


void Help::PrintDefinition(Object* obj) {
  assert(obj != NULL);

  if (obj->AsLiteral() != NULL) {
    F.print("static %s: %T = %N;  # literal; ", obj->name(), obj->type(), obj->node());
    Val* val = obj->AsLiteral()->val();
    // we need a Proc to print a value, but NULL will do in this context.
    // use alternate (#) format for time values.
    if (obj->type()->IsEqual(SymbolTable::time_type(), false))
      F.print("%#V\n", NULL, val);
    else
      F.print("%V\n", NULL, val);
    return;
  }

  if (obj->AsVarDecl() != NULL) {
    F.print("%s: %T;\n", obj->name(), obj->type());
    return;
  }

  if (obj->AsTypeName() != NULL) {
    F.print("type %s = %#T;", obj->name(), obj->type());
    if (obj->type()->is_basic())
      F.print("  # basic type");
    F.print("\n");
    return;
  }

  if (obj->AsIntrinsic() != NULL) {
    Intrinsic* fun = obj->AsIntrinsic();
    switch (fun->kind()) {
      case Intrinsic::INTRINSIC:
      case Intrinsic::MATCH:
      case Intrinsic::MATCHPOSNS:
      case Intrinsic::MATCHSTRS:
        F.print("%s: %T;\n", obj->name(), fun->type());
        return;
      default:
        F.print("%s: %T; # incomplete definition - please see the documentation\n", obj->name(), obj->type());
        return;
    }
  }

  // catch-all
  F.print("cannot explain '%s' yet: functionality not yet implemented\n", obj->name());
}


// Tag is a simple helper class to get matching
// opening and closing tag brackets.
class Tag {
 public:
  Tag(const char* name, const char* attrs = NULL) : name_(name) {
    F.print("<%s", name);
    if (attrs != NULL)
      F.print(" %s", attrs);
    F.print(">");
  }
  
  ~Tag() {
    F.print("</%s>", name_);
  }
  
 private:
  const char* name_;
};


void Help::PrintHtmlDocumentation(const char* title) {
  assert(SymbolTable::is_initialized());
  Tag html("html");

  { Tag head("head");
    { Tag titletag("title");
       F.print("%s", title);
    }
    F.print("<link rel=\"stylesheet\" href=\"szlhelpstyle.css\">\n");
  }
  F.print("\n");

  { Tag body("body");
    Scope* universe = SymbolTable::universe();
    for (int i = 0; i < universe->num_entries(); i++) {
      Object* obj = universe->entry_at(i);
      if (!obj->is_internal()) { // don't print internal identifiers
        { Tag tt("pre");
          PrintDefinition(obj);
        }
        F.print("%s\n", obj->doc());
        F.print("<hr>\n");
      }
    }
  }
  F.print("\n");
}


bool Help::Explain(const char* name) {
  Object* obj = SymbolTable::universe()->Lookup(name);
  if (obj != NULL) {
    PrintDefinition(obj);
    // print documentation
    F.print("\n%s\n", obj->doc());
    if (obj->AsIntrinsic() != NULL) {
      if (obj->AsIntrinsic()->can_fail())
        F.print("Returns an undefined value when an error occurs.\n");
      else
        F.print("Never returns an undefined value.\n");
    }
    return true;
  }
  return false;
}


}  // namespace sawzall

