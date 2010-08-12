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
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <map>

#include "engine/globals.h"

#include "utilities/strutils.h"
#include "utilities/szlmutex.h"

#include "fmt/runes.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/proc.h"


namespace sawzall {

const int kMicrosecPerSec = 1000000;
const int kMicrosecPerMillisec = 1000;
const int kMillisecPerSec = 1000;



class CloneMap::CMap : public map<void*, void*> { };


CloneMap::~CloneMap() {
  delete map_;
}


void* CloneMap::FindAny(void* key) {
  if (map_ == NULL)
    return NULL;
  CMap::iterator it = map_->find(key);
  if (it == map_->end())
    return NULL;
  else
    return it->second;
}


void CloneMap::InsertAny(void* key, void* value) {
  if (map_ == NULL)
    map_ = new CMap;
  (*map_)[key] = value;
}


// Return the (lexically determined) directory of a file.
char* FileDir(Proc* proc, const char* file) {
  char* dir = proc->CopyString(file);
  char* p = strrchr(dir, '/');
  if (p == NULL) {
    return proc->CopyString(".");
  }
  *p = '\0';
  return dir;
}


// case-insensitve string comparison, from Plan 9.
int cistrcmp(const char *s1, const char *s2) {
  int c1, c2;

  while (*s1) {
    c1 = *(unsigned char*)s1++;
    c2 = *(unsigned char*)s2++;

    if (c1 == c2)
      continue;

    if (c1 >= 'A' && c1 <= 'Z')
      c1 -= 'A' - 'a';

    if (c2 >= 'A' && c2 <= 'Z')
      c2 -= 'A' - 'a';

    if (c1 != c2)
      return c1 - c2;
  }
  return -*s2;
}


// convert List<String> into char* (null terminated). It is the callers
// responsibility to delete the memory when it is done with it.
char* CharList2CStr(List<char>* src) {
  const int n = src->length();
  char* dst = new char[n + 1];  // +1 for terminating 0 char
  memcpy(dst, src->data(), n);
  dst[n] = '\0';  // add terminating 0 char
  return dst;
}

}  // namespace sawzall
