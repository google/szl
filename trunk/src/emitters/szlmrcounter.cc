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

// provides the mapreduce counts for x: table mrcounter of int;
// This is severely truncated, and used mostly for parsing and type checking

#include "public/porting.h"
#include "public/logging.h"

#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/szldecoder.h"
#include "public/szlresults.h"
#include "public/szltabentry.h"


namespace {

class SzlMrCounter: public SzlTabWriter {
 private:
  explicit SzlMrCounter(const SzlType& type)
    : SzlTabWriter(type, false, false)  { }
  ~SzlMrCounter()  { }
  class SzlMrCounterEntry;

 public:
  static SzlTabWriter* Create(const SzlType& type, string* error) {
    return new SzlMrCounter(type);
  }

  virtual SzlTabEntry* CreateEntry(const string& index) const {
    LOG(FATAL) << "SzlMrCounter::CreateEntry called";
    return NULL;
  }

  // Is the type acceptable?
  static bool Validate(const SzlType& type, string* error);

  virtual bool IsMrCounter() const  { return true; }

  // Our properties (whatever they are)
  static void Props(const char * kind, SzlType::TableProperties* props) {
    props->name = "mrcounter";
    props->has_param = false;
    props->has_weight = false;
  }
};


REGISTER_SZL_TAB_WRITER(mrcounter, SzlMrCounter);

// we don't write to the mill, so no SzlResults, so register checking code
REGISTER_SZL_NON_MILL_RESULTS(mrcounter,
                              &SzlMrCounter::Validate,
                              &SzlMrCounter::Props);


// no indices, and value must be an int
bool SzlMrCounter::Validate(const SzlType& type, string* error) {
  if (type.indices_size() != 0) {
    *error = "mrcounter cannot be indexed";
    return false;
  }
  const SzlType& val_type = type.element()->type();
  if (!val_type.Equal(SzlType::kInt)) {
    *error = "mrcounter only accepts ints";
    return false;
  }
  return true;
}


} // namespace
