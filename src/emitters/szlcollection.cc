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

// Implementation of SzlTabWriter and SzlTabEntry for collections in Sawzall.

#include <string>
#include <vector>

#include "public/porting.h"
#include "public/logging.h"

#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szltabentry.h"


// Implementation of collection table objects.
class SzlCollection : public SzlTabWriter {
 private:
  explicit SzlCollection(const SzlType& type)
    : SzlTabWriter(type, false, false)  { }

 public:
  static SzlTabWriter* Create(const SzlType& type, string* error) {
    return new SzlCollection(type);
  }

  virtual SzlTabEntry* CreateEntry(const string& index) const {
    return new SzlCollectionEntry();
  }

 private:
  class SzlCollectionEntry : public SzlTabEntry {
   public:
    SzlCollectionEntry()  { }
    virtual ~SzlCollectionEntry()  { }

    virtual int Memory() { return sizeof(SzlCollectionEntry); }
  };
};


REGISTER_SZL_TAB_WRITER(collection, SzlCollection);
