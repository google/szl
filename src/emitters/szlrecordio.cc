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
#include "public/logging.h"
#include "public/recordio.h"

#include "public/szldecoder.h"
#include "public/szlresults.h"
#include "public/szltabentry.h"

#include "emitvalues/szlxlate.h"


namespace {


// Structure for storing data directly to a recordio file.
class SzlRecordio: public SzlTabWriter {
 private:
  explicit SzlRecordio(const SzlType& type)
    : SzlTabWriter(type, false, true),
      value_type_(type.element()->type()),
      writer_(NULL)  { }
  virtual ~SzlRecordio()  { delete writer_; }
  class SzlRecordioEntry;

 public:
  static SzlTabWriter* Create(const SzlType& type, string* error) {
    if (!Validate(type, error))
      return NULL;
    return new SzlRecordio(type);
  }

  virtual SzlTabEntry* CreateEntry(const string& index) const {
    return new SzlRecordioEntry(writer_);
  }

  virtual bool WritesToMill() const  { return false; }

  virtual void FilterKey(const string& key,
                         string* fkey, uint64* shardfp) const {
    fkey->clear();
  }

  virtual void FilterValue(const string& value, string* fvalue) const {
    SzlDecoder dec(value.data(), value.size());
    uint64 junk;
    SzlXlate::TranslateValue(value_type_, &dec, fvalue, &junk);
  }

  // Create the output file for the table.
  virtual void CreateOutput(const string& tempname);

  // Is the type acceptable?  It will be a recordio table type with
  // properties already checked.
  static bool Validate(const SzlType& type, string* error);

  // Retrieve the TableProperties for this kind of table.
  static void Props(const char* kind, SzlType::TableProperties* props) {
    props->name = "recordio";
    props->has_param = true;
    props->has_weight = false;
  }
  
  sawzall::RecordWriter* writer() const { return writer_; }

 private:
  // Element type for filter.
  const SzlType value_type_;

  // Recordio writer.
  sawzall::RecordWriter* writer_;

  class SzlRecordioEntry: public SzlTabEntry {
   public:
    explicit SzlRecordioEntry(sawzall::RecordWriter* const& writer)
      : writer_(writer)  { }
    virtual ~SzlRecordioEntry()  { }

    // Write the output value.
    virtual void Write(const string& val) {
      CHECK(writer() != NULL);
      CHECK(writer()->Write(val.data(), val.size()));
    }

    virtual int Memory()  { return sizeof(SzlRecordioEntry); }

    sawzall::RecordWriter* writer() const { return writer_; }

   private:
    sawzall::RecordWriter* const& writer_;
  };
};


REGISTER_SZL_TAB_WRITER(recordio, SzlRecordio);

// Since we have no mill results, and therefore no SzlResults,
// we register our type checking functions here.
REGISTER_SZL_NON_MILL_RESULTS(recordio,
                              &SzlRecordio::Validate, &SzlRecordio::Props);


bool SzlRecordio::Validate(const SzlType& type, string* error) {
  if(type.indices_size() != 0) {
    *error = "recordio tables cannot be indexed";
    return false;
  }

  const SzlType& value_type = type.element()->type();
  if (!SzlXlate::IsTranslatableType(value_type)) {
    *error = "can't translate recordio value type";
    return false;
  }

  return true;
}


void SzlRecordio::CreateOutput(const string& filename) {
  writer_ = sawzall::RecordWriter::Open(filename.c_str());
  if (writer_ == NULL)
    LOG(ERROR) << "Can't open output for recordio table, file " << filename;
  // Param gives the compression block size - not currently used.
}


}  // unnamed namespace
