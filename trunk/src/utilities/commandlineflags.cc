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

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <string>
#include <vector>
#include <utility>

#include "public/hash_map.h"

#include "public/porting.h"
#include "public/commandlineflags.h"


// Also does post-static-initialization global initialization.

using sawzall::RegisterFlag;
DEFINE_bool(silent_init, true, "No log message on initialization");

DECLARE_bool(help);
DECLARE_bool(helpxml);
DECLARE_bool(version);


namespace sawzall {


// Do not rely on static initialization for setting up init_list,
// since elements are added to it during static initialization.
vector<CmdLineFlag>* CmdLineFlag::flags = NULL;

void RegisterFlag(bool* flag, const char* name, const char* description) {
  CmdLineFlag::AddFlag(flag, CmdLineFlag::BOOL, "bool", name, description);
}

void RegisterFlag(int32* flag, const char* name, const char* description) {
  CmdLineFlag::AddFlag(flag, CmdLineFlag::INT32, "int32", name, description);
}

void RegisterFlag(int64* flag, const char* name, const char* description) {
  CmdLineFlag::AddFlag(flag, CmdLineFlag::INT64, "int64", name, description);
}

void RegisterFlag(string* flag, const char* name, const char* description) {
  CmdLineFlag::AddFlag(flag, CmdLineFlag::STRING, "string", name, description);
}


}  // namespace sawzall


using sawzall::CmdLineFlag;

void ProcessCommandLineArguments(int& argc, char**& argv) {
  bool help_flags = false;
  bool non_help_flags = false;
  for (int i = 1; i < argc; ) {
    const char* p = argv[i];
    if (*p++ == '-') {
      if (*p == '-' && *(p+1) == '\0') {
        // "--" argument terminates flags;  delete this one and quit.
        memcpy(&argv[i], &argv[i+1], sizeof(argv[i]) * (argc - i - 1));
        --argc;
        break;
      }
      // Skip optional second "-".
      if (*p == '-')
        p++;
      // Search for command line argument name, possibly followed by "=".
      vector<CmdLineFlag>* flags = CmdLineFlag::flags;
      int skip = 0;
      for (int j = 0; j < flags->size(); j++) {
        CmdLineFlag& flag = (*flags)[j];
        const char* name = flag.name;
        int len = strlen(name);
        bool no = p[0] == 'n' && p[1] == 'o';
        if ((memcmp(p,name,len) == 0 && (p[len]=='\0' || p[len]=='=')) || (no &&
             memcmp(p+2,name,len) == 0 && (p[len+2]=='\0' || p[len+2]=='=')
             && flag.type == CmdLineFlag::BOOL)) {
          if (flag.ptr_bool == &FLAGS_help || flag.ptr_bool == &FLAGS_helpxml ||
              flag.ptr_bool == &FLAGS_version)
            help_flags = true;
          else
            non_help_flags = true;
          if (help_flags && non_help_flags) {
            fputs("Help and version flags must not be mixed with other flags\n",
                  stderr);
            exit(1);
          }
          if (flag.type == CmdLineFlag::BOOL && p[len] != '=') {
            // For boolean can omit value; do not use following arg as value
            *flag.ptr_bool = !no;
            skip = 1;
            break;
          } 
          const char* q;
          if (p[len] == '=') {
            q = p + len + 1;
            skip = 1;
          } else {
            if (i+1 == argc) {
              fprintf(stderr, "The '%s' flag is missing its value.\n",
                      flag.name);
              abort();
            }
            q = argv[i+1];
            skip = 2;
          }
          if (flag.type == CmdLineFlag::BOOL) {
            if (strcmp(q, "true") == 0) {
              *flag.ptr_bool = !no;
            } else if (strcmp(q, "false") == 0) {
              *flag.ptr_bool = no;
            } else {
              fprintf(stderr,
                      "Invalid value '%s' specified for bool flag  '%s'\n",
                      q, flag.name);
              abort();
            }
          } else if (flag.type == CmdLineFlag::INT32) {
            errno = 0;
            char* end;
            *flag.ptr_int32 = strtol(q, &end, 0);
            if (errno != 0 || *end != '\0') {
              fprintf(stderr,
                      "Invalid value '%s' specified for int32 flag  '%s'\n",
                      q, flag.name);
              abort();
            }
          } else if (flag.type == CmdLineFlag::INT64) {
            errno = 0;
            char* end;
            *flag.ptr_int64 = strtoll(q, &end, 0);
            if (errno != 0 || *end != '\0') {
              fprintf(stderr,
                      "Invalid value '%s' specified for int64 flag  '%s'\n",
                      q, flag.name);
              abort();
            }
          } else if (flag.type == CmdLineFlag::STRING) {
            *flag.ptr_string = q;
          } else {
            fputs("Internal error, unrecognized flag type.\n", stderr);
            abort();
          }
          break;
        }
      }
      if (skip == 0) {
        fprintf(stderr, "Unknown command line flag '%s'\n", p);
        abort();
      }
      // Delete the flag, and the value if present.
      memcpy(&argv[i], &argv[i+skip], sizeof(argv[i]) * (argc - i - skip));
      argc -= skip;
    } else {
      // Not a command line flag, skip this argument.
      i++;
    }
  }

  if (help_flags)
    sawzall::HandleCommandLineHelpFlags(argv[0]);
}


// ----------------------------------------------------------------------------
// Handling of post-static-initialization startup code.


// Do not rely on static initialization for setting up init_list,
// since elements are added to it during static initialization.
static vector<pair<const char*,void(*)()> >* init_list = NULL;


int RegisterModuleInitializer(const char* name, void (*init)()) {
  if (init_list == NULL)
    init_list = new vector<pair<const char*,void(*)()> >;
  init_list->push_back(pair<const char*,void(*)()>(name, init));
  return 0;
}


void InitializeOneModule(const char* name) {
  if (init_list != NULL) {
    for (int i = 0; i < init_list->size(); i++) {
      if (strcmp((*init_list)[i].first, name) == 0 &&
          (*init_list)[i].second != NULL) {
        void (*f)() = (*init_list)[i].second;
        (*init_list)[i].second = NULL;
        (*f)();
      }
    }
  }
}


void InitializeAllModules() {
  if (init_list != NULL) {
    for (int i = 0; i < init_list->size(); i++) {
      if ((*init_list)[i].second != NULL) {
        (*(*init_list)[i].second)();
        (*init_list)[i].second = NULL;
      }
    }
  }
}
