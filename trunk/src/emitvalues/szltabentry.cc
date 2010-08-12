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
#include <string>

#include "public/porting.h"
#include "public/hash_map.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "public/szltype.h"
#include "public/szltabentry.h"


typedef hash_map<string, SzlTabWriterCreator> SzlTabWriterCreators;
static SzlTabWriterCreators* creators;

SzlTabWriterRegisterer::SzlTabWriterRegisterer(const char* kind,
                                               SzlTabWriterCreator creator) {
  if (creator == NULL) {
    fprintf(stderr, "failed to register %s: creator is NULL\n", kind);
  }
  // Note: might need a lock in a threaded environment.
  if (creators == NULL)
    creators = new SzlTabWriterCreators;

  if (creators->find(kind) != creators->end()) {
    fprintf(stderr, "multiple registrations of the same szl table kind %s\n",
            kind);
  }

  (*creators)[kind] = creator;
}


SzlTabWriter* SzlTabWriter::CreateSzlTabWriter(const SzlType& t,
                                               string* error) {
  if (creators == NULL) {
    *error = "no SzlTabWriters are registered";
    return NULL;
  }
  if (t.kind() != SzlType::TABLE) {
    *error = "the SzlType is not of type table";
    return NULL;
  }
  SzlTabWriterCreators::iterator creator = creators->find(t.table());
  if (creator == creators->end()) {
    *error = "unknown szl table type";
    return NULL;
  }
  return (creator->second)(t, error);
}
