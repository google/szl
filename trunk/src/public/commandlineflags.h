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

#ifndef _COMMANDLINEFLAGS_H_
#define _COMMANDLINEFLAGS_H_

#include <string>
#include <vector>

using namespace std;


void ProcessCommandLineArguments(int& argc, char**& argv);


// Command line flag registration.

namespace sawzall {

void RegisterFlag(bool* flag, const char* name, const char* desc);
void RegisterFlag(int32* flag, const char* name, const char* desc);
void RegisterFlag(int64* flag, const char* name, const char* desc);
void RegisterFlag(string* flag, const char* name, const char* desc);

void HandleCommandLineHelpFlags(const char* argv0);

}  // namespace sawzall



#define DEFINE_bool(name, deflt, desc)  namespace fLB { \
  bool FLAGS_##name = (sawzall::RegisterFlag(&FLAGS_##name,#name,desc), (deflt)); \
  } using fLB::FLAGS_##name

#define DEFINE_int32(name, deflt, desc)  namespace fLI { \
  int32 FLAGS_##name = (sawzall::RegisterFlag(&FLAGS_##name,#name,desc), (deflt)); \
  } using fLI::FLAGS_##name

#define DEFINE_int64(name, deflt, desc)  namespace fLI { \
  int64 FLAGS_##name = (sawzall::RegisterFlag(&FLAGS_##name,#name,desc), (deflt)); \
  } using fLI::FLAGS_##name

#define DEFINE_string(name, deflt, desc)  namespace fLS { \
  string FLAGS_##name = (sawzall::RegisterFlag(&FLAGS_##name,#name,desc), (deflt)); \
  } using fLS::FLAGS_##name

#define DECLARE_bool(name)  namespace fLB { \
    extern bool FLAGS_##name; \
  } using fLB::FLAGS_##name

#define DECLARE_int32(name)  namespace fLI { \
    extern int32 FLAGS_##name; \
  } using fLI::FLAGS_##name

#define DECLARE_int64(name)  namespace fLI { \
    extern int64 FLAGS_##name; \
  } using fLI::FLAGS_##name

#define DECLARE_string(name)  namespace fLS { \
    extern string FLAGS_##name; \
  } using fLS::FLAGS_##name


namespace sawzall {

struct CmdLineFlag {
  enum Type { BOOL, INT32, INT64, STRING };
  CmdLineFlag(void* ptr, Type type, const char* type_string, const char* name,
              const char* description)
    : ptr(ptr), type(type), type_string(type_string), name(name),
      description(description)  { }
  static void AddFlag(void* ptr, Type type, const char* type_string,
                      const char* name, const char* description) {
    if (flags == NULL)
      flags = new vector<CmdLineFlag>;
    flags->push_back(CmdLineFlag(ptr, type, type_string, name, description));
  }
  union {
    void* ptr;
    bool* ptr_bool;
    int32* ptr_int32;
    int64* ptr_int64;
    string* ptr_string;
  };
  Type type;
  const char* type_string;
  const char* name;
  const char* description;
  static vector<CmdLineFlag>* flags;
};

}  // namespace sawzall

// ----------------------------------------------------------------------------
// Handling of post-static-initialization startup code.


// Registration of initialization code to be performed after main() is entered.
// If the initialization code included "REQUIREMODULE_INITIALIZED(name)",
// that one must be done before this one.  The dependencies must form a DAG;
// cycles will cause initialization to hang.

#define REGISTER_MODULE_INITIALIZER(name, code)              \
  namespace {                                                \
    void ModuleInitializer() { code; }                       \
    int module_init_dummy =                                  \
      RegisterModuleInitializer(#name, &ModuleInitializer);  \
  }

#define REQUIRE_MODULE_INITIALIZED(name)                     \
  InitializeOneModule(#name)

int RegisterModuleInitializer(const char* name, void (*init)());

void InitializeOneModule(const char* name);
void InitializeAllModules();


#endif  // _COMMANDLINEFLAGS_H_
