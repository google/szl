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

// Database support in sawzall runtime.

#include <assert.h>

#include "public/hash_map.h"

#include "engine/globals.h"
#include "public/logging.h"

#include "utilities/dbutils.h"
#include "utilities/szlmutex.h"

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

// The db's that are currently opened
static hash_map<int, SzlDB*> DbMap;
// The key to the db in the hash_map (keeps increasing)
static int CurrentDbId = 0;

// Mutex for accessing db's and dbid.
static SzlMutex DbLock;;

static ArrayType* array_of_array_of_string_type = NULL;

static ArrayVal* NewArrayStringArray(Proc* proc, int length) {
  return array_of_array_of_string_type->form()->NewVal(proc, length);
}


// dbconnect(dbspec: string, defaultspec: string): db
static const char* dbconnect_doc =
  "Connects to a database with the dbspecs and returns a db object.  "
  "It is recommended to declare the db object as static so only one "
  "connection is made per worker.";

static const char* dbconnect(Proc* proc, Val**& sp) {
  // Get arguments
  string dbspec = Engine::pop_cpp_string(proc, sp);
  string defaultspec = Engine::pop_cpp_string(proc, sp);

  // Connect to the database with the specs
  SzlDB* db = SzlDB::Connect(dbspec.c_str(), defaultspec.c_str());
  if (db != NULL) {
    SzlMutexLock m(&DbLock);  // Acquire lock
    DbMap[CurrentDbId] = db;

    // Return the key to the database object
    Engine::push_szl_int(sp, proc, CurrentDbId);

    // Safely incerment dbid.
    CurrentDbId++;
    return NULL;
  }
  return proc->PrintError("Error connecting to database.");
}


// dbquery(db: db, query: string): array of array of string
static const char* dbquery_doc =
  "Executes a sql query on the given database object.  "
  "Returns an array of array of string, each array of string "
  "representing one row of results.  For most queries such as "
  "SELECT statements, the results can be declared as static to "
  "avoid excessive queries on the database.";

static const char* dbquery(Proc* proc, Val**& sp) {
  // Get arguments
  int dbid = static_cast<int>(Engine::pop_szl_int(sp));
  string query = Engine::pop_cpp_string(proc, sp);

  if (DbMap.count(dbid) > 0) {  // If the db connection is already made
    SzlMutexLock m(&DbLock);  // Acquire lock
    SzlDB* db = DbMap[dbid];
    if (db != NULL && db->SafeExecuteQuery(query.c_str())) {
      int rowcount = db->GetRowCount();
      ArrayVal* result_rows = NewArrayStringArray(proc, rowcount);

      for (int r = 0; db->Fetch(); r++) {
        int colcount = db->GetColCount();
        ArrayVal* szl_cols = Factory::NewStringArray(proc, colcount);

        for (int c = 0; c < colcount; c++) {
          const char* column_str = db->GetString(c);
          if (column_str == NULL) {
            column_str = "NULL";  // real undef value?
          }
          szl_cols->at(c) = Factory::NewStringC(proc, column_str);
        }
        result_rows->at(r) = szl_cols;
      }
      Engine::push(sp, result_rows);
      return NULL;
    }
    // Return an undefined value if the query failed.
    return proc->PrintError("Query on database failed.");
  }
  return proc->PrintError("Invalid database object.");
}


static void Initialize() {
  // make sure the SymbolTable is initialized
  assert(SymbolTable::is_initialized());
  Proc* proc = Proc::initial_proc();
  FileLine* file_line = SymbolTable::init_file_line();

  // shortcuts for predefined types
  Type* string_type = SymbolTable::string_type();
  Type* int_type = SymbolTable::int_type();
  Type* array_of_string_type = SymbolTable::array_of_string_type();

  Field* array_of_string_field =
    Field::New(proc, file_line, NULL, array_of_string_type);

  array_of_array_of_string_type =
    ArrayType::New(proc, array_of_string_field);

  Type* db_type = int_type;

  SymbolTable::RegisterType("SQL_DB", db_type);

#define DEF(name, type, attribute) \
  SymbolTable::RegisterIntrinsic(#name, type, name, name##_doc, attribute)

  DEF(dbconnect, FunctionType::New(proc)->
          par("dbspec", string_type)->
          par("defaultspec", string_type)->
          res(db_type),
      Intrinsic::kNormal);

  DEF(dbquery, FunctionType::New(proc)->
          par("db", db_type)->
          par("query", string_type)->
          res(array_of_array_of_string_type),
      Intrinsic::kNormal);

#undef DEF
}

}  // namespace sawzall


// Note: This must happen outside the namespace
// to avoid linker/name mangling problems.
REGISTER_MODULE_INITIALIZER(
  DbIntrinsic,
  { REQUIRE_MODULE_INITIALIZED(Sawzall);
    sawzall::Initialize();
  }
);
