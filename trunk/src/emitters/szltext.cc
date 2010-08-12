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
#include <sys/stat.h>

#include "public/porting.h"
#include "public/logging.h"

#include "public/szldecoder.h"
#include "public/szlresults.h"
#include "public/szltabentry.h"


namespace {

// Structure for storing data directly to a text file.
class SzlText: public SzlTabWriter {
 private:
  explicit SzlText(const SzlType& type)
    : SzlTabWriter(type, false, true),
      value_type_(type.element()->type()), file_(NULL)  { }
  virtual ~SzlText()  { }
  class SzlTextEntry;

 public:
  static SzlTabWriter* Create(const SzlType& type, string* error) {
    if (!Validate(type, error))
      return NULL;
    return new SzlText(type);
  }

  virtual SzlTabEntry* CreateEntry(const string& index) const {
    return new SzlTextEntry(file());
  }

  virtual bool WritesToMill() const { return false; }

  virtual void FilterKey(const string& key,
                         string* fkey, uint64* shardfp) const {
    fkey->clear();
  }

  virtual void CreateOutput(const string& tempname);

  static bool Validate(const SzlType& type, string* error);

  static void Props(const char* kind, SzlType::TableProperties* props) {
    props->name = "text";
    props->has_param = false;
    props->has_weight = false;
  }

  FILE* file() const { return file_; }

 private:
  // Type of the table and its value
  const SzlType value_type_;

 private:
  // Builder for file.
  FILE* file_;

  class SzlTextEntry: public SzlTabEntry {
   public:
    explicit SzlTextEntry(FILE* file) : file_(file)  { }
    virtual ~SzlTextEntry()  { }

    virtual void Write(const string& val) {
      DCHECK(file() != NULL);
      size_t count = fwrite(val.data(), 1, val.size(), file_);
      CHECK_EQ(count, val.size());
    }

    virtual int Memory()  { return sizeof(SzlTextEntry); }
  
    FILE* file() const { return file_; }

   private:
    // Builder for file.
    FILE* file_;
  };
};


REGISTER_SZL_TAB_WRITER(text, SzlText);

// Since we have no mill results, and therefore no SzlResults,
// we register our type checking functions here.
REGISTER_SZL_NON_MILL_RESULTS(text, &SzlText::Validate, &SzlText::Props);


bool SzlText::Validate(const SzlType& type, string* error) {
  if(type.indices_size() != 0) {
    *error = "text tables cannot be indexed";
    return false;
  }

  const SzlType& value_type = type.element()->type();
  if (value_type.kind() != SzlType::STRING
  && value_type.kind() != SzlType::BYTES) {
    *error = "text table elements must be of type string or bytes";
    return false;
  }

  return true;
}

void SzlText::CreateOutput(const string& tempname) {
  file_ = fopen(tempname.c_str(), "w");
  if (file_ == NULL) {
    LOG(ERROR) << "Can't open output for text table, file " << tempname;
  }
}

};
