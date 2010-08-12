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

// Implementation of the SzlEmitter class.

#include <stdio.h>
#include <float.h>
#include <assert.h>
#include <string>
#include <vector>

#include "public/hash_map.h"

#include "public/porting.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "public/emitterinterface.h"
#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/szlemitter.h"
#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/sawzall.h"
#include "public/szltabentry.h"



SzlEmitter::SzlEmitter(const string& name, const SzlTabWriter* writer,
                       bool display)
    : writer_(writer),
      weight_ops_(writer->weight_ops()),
      key_(new SzlEncoder),
      value_(new SzlEncoder),
      encoder_(NULL),
      table_(new SzlTabEntryMap),
      name_(name),
      memory_estimate_(0),
      display_(display),
      depth_(0),
      weight_(new SzlValue),
      errors_detected_(false) {
}


SzlEmitter::~SzlEmitter() {
  Clear();
  delete table_;
  delete writer_;
  delete key_;
  delete value_;
  delete weight_;
}


void SzlEmitter::Clear() {
  if (display_)
    DisplayResults();

  if (table_ != NULL) {
    table_->clear();
    weight_ops_.Clear(weight_);
  }
  memory_estimate_ = 0;
}


void SzlEmitter::Begin(GroupType type, int len) {
  switch (type) {
    case EMIT:
      assert(encoder_ == NULL && depth_ == 0);
      weight_pos_ = -1;
      in_weight_ = false;
      break;
    case INDEX:
      assert(encoder_ == NULL && depth_ == 0);
      depth_++;
      encoder_ = key_;
      key_->Reset();
      break;
    case ELEMENT:
      assert(encoder_ == NULL && depth_ == 0);
      depth_++;
      encoder_ = value_;
      value_->Reset();
      break;
    case WEIGHT:
      assert(encoder_ == NULL && depth_ == 0);
      assert(writer_->HasWeight());
      in_weight_ = true;
      weight_pos_ = 0;
      depth_++;
      break;
    case TUPLE:
      assert(encoder_ != NULL || in_weight_);
      if (arrays_.size() > 0 && arrays_.back() == depth_ - 1)
        encoder_->Start(SzlType::TUPLE);
      depth_++;
      break;
    case ARRAY:  // fall through
    case MAP:
      if (in_weight_) {
        fputs("arrays or maps inside weights is not currently supported\n",
              stderr);
        errors_detected_ = true;
      }
      assert(encoder_ != NULL);
      if (type == ARRAY) {
        encoder_->Start(SzlType::ARRAY);
      } else {
        encoder_->Start(SzlType::MAP);
        encoder_->PutInt(len);
      }
      arrays_.push_back(depth_);
      depth_++;
      break;
    default:
      LOG(ERROR) << "unknown Sawzall table group type " << type << endl;
      break;
  }
}


void SzlEmitter::End(GroupType type, int len) {
  if (type == EMIT) {
    assert(encoder_ == NULL && depth_ == 0);

    // At this point we have a complete emit.
    // Stash it away in the appropriate aggregation table or add it to the
    // output if we aren't aggregating results during the map phase.
    assert((weight_pos_ > 0) == (writer_->HasWeight()));
    const string& k = key_->data();
    if (writer_->Aggregates()) {
      SzlTabEntry* table_entry = NULL;
      SzlTabEntryMap::iterator it = table_->find(k);
      if (it == table_->end()) {
        table_entry = writer_->CreateEntry(k);
        table_->insert(SzlTabEntryMap::value_type(k, table_entry));
      } else {
        table_entry = it->second;
      }
      const string& v = value_->data();
      if (weight_pos_ > 0) {
        memory_estimate_ += table_entry->AddWeightedElem(v, *weight_);
      } else {
        memory_estimate_ += table_entry->AddElem(v);
      }
    } else {
      assert(!writer_->HasWeight());
      // Optional value filtering.
      string value;
      if (writer_->Filters())
        writer_->FilterValue(value_->data(), &value);
      else
        value_->Swap(&value);
      WriteValue(k, value);
    }
    return;
  }

  assert((encoder_ != NULL || in_weight_) && depth_ > 0);
  --depth_;

  if (type == ARRAY || type == MAP) {
    encoder_->End(type == ARRAY ? SzlType::ARRAY : SzlType::MAP);
    assert(arrays_.back() == depth_);
    arrays_.pop_back();
  } else if (type == TUPLE && arrays_.size() > 0 &&
             arrays_.back() == depth_ - 1) {
    encoder_->End(SzlType::TUPLE);
  }
  if (depth_)
    return;

  assert(arrays_.size() == 0);
  encoder_ = NULL;
  in_weight_ = false;
}


void SzlEmitter::PutBool(bool b) {
  if (in_weight_) {
    assert(encoder_ == NULL);
    weight_ops_.PutBool(b, weight_pos_, weight_);
    weight_pos_++;
  } else {
    assert(encoder_ != NULL);
    encoder_->PutBool(b);
  }
}


void SzlEmitter::PutBytes(const char* p, int len) {
  if (in_weight_) {
    assert(encoder_ == NULL);
    weight_ops_.PutBytes(p, len, weight_pos_, weight_);
    weight_pos_++;
  } else {
    assert(encoder_ != NULL);
    encoder_->PutBytes(p, len);
  }
}


void SzlEmitter::PutInt(int64 i) {
  if (in_weight_) {
    assert(encoder_ == NULL);
    weight_ops_.PutInt(i, weight_pos_, weight_);
    weight_pos_++;
  } else {
    assert(encoder_ != NULL);
    encoder_->PutInt(i);
  }
}


void SzlEmitter::PutFloat(double f) {
  if (in_weight_) {
    assert(encoder_ == NULL);
    weight_ops_.PutFloat(f, weight_pos_, weight_);
    weight_pos_++;
  } else {
    assert(encoder_ != NULL);
    encoder_->PutFloat(f);
  }
}


void SzlEmitter::PutFingerprint(uint64 fp) {
  if (in_weight_) {
    assert(encoder_ == NULL);
    weight_ops_.PutFingerprint(fp, weight_pos_, weight_);
    weight_pos_++;
  } else {
    assert(encoder_ != NULL);
    encoder_->PutFingerprint(fp);
  }
}


void SzlEmitter::PutString(const char* s, int len) {
  if (in_weight_) {
    assert(encoder_ == NULL);
    weight_ops_.PutString(s, len, weight_pos_, weight_);
    weight_pos_++;
  } else {
    assert(encoder_ != NULL);
    encoder_->PutString(s, len);
  }
}


void SzlEmitter::PutTime(uint64 t) {
  if (in_weight_) {
    assert(encoder_ == NULL);
    weight_ops_.PutTime(t, weight_pos_, weight_);
    weight_pos_++;
  } else {
    assert(encoder_ != NULL);
    encoder_->PutTime(t);
  }
}

void SzlEmitter::EmitInt(int64 i) {
  Begin(EMIT, 1);
  Begin(ELEMENT, 1);
  PutInt(i);
  End(ELEMENT, 1);
  End(EMIT, 1);
}

void SzlEmitter::EmitFloat(double f) {
  Begin(EMIT, 1);
  Begin(ELEMENT, 1);
  PutFloat(f);
  End(ELEMENT, 1);
  End(EMIT, 1);
}

bool SzlEmitter::Merge(const string& index, const string& val) {
  SzlTabEntry* table_entry = NULL;
  SzlTabEntryMap::iterator it = table_->find(index);
  if (it == table_->end()) {
    table_entry = writer_->CreateEntry(index);
    table_->insert(SzlTabEntryMap::value_type(index, table_entry));
  } else {
    table_entry = it->second;
  }

  return (table_entry->Merge(val) == SzlTabEntry::MergeOk);
}


// Displays the table contents after all the records have been processed.
// Note that this calls WriteValue, which can be overridden.
void SzlEmitter::DisplayResults() {
  for (SzlTabEntryMap::const_iterator it = table_->begin();
       it != table_->end(); ++it) {
    vector<string> buffer;
    it->second->FlushForDisplay(&buffer);
    for (int i = 0; i < buffer.size(); i++)
      WriteValue(it->first, buffer[i]);
  }
}


// Flush the current table contents.
// This is for use with mapreduce; if used with the default WriteValue,
// all the values associated with a key will be printed together, unlike
// DisplayResults() which prints each value on a separate line,
// with duplication of key values.
void SzlEmitter::Flusher() {
  if (table_ != NULL) {
    string k, v;
    for (SzlTabEntryMap::iterator it = table_->begin();
         it != table_->end(); ++it) {
      v.clear();
      it->second->Flush(&v);
      if (!v.empty())
        WriteValue(it->first, v);
    }
    table_->clear();
  }
  memory_estimate_ = 0;
}


int SzlEmitter::GetTupleCount() const {
  int tuple_count = 0;
  for (SzlTabEntryMap::iterator itTab = table_->begin(); itTab != table_->end();
       itTab++) {
    tuple_count += itTab->second->TupleCount();
  }
  return tuple_count;
}


int SzlEmitter::GetMemoryUsage() const {
  int memory_used = 0;
  for (SzlTabEntryMap::iterator itTab = table_->begin(); itTab != table_->end();
       itTab++) {
    memory_used += itTab->second->Memory();
  }
  return memory_used;
}


void SzlEmitter::WriteValue(const string& key, const string& value) {
  // Mapreduce code should override to generate mapper output.
  // Default version writes to stdout - see PrintEmitter.
  SzlDecoder key_decoder(key.data(), key.length());
  string key_print = key_decoder.PPrint();
  SzlDecoder value_decoder(value.data(), value.length());
  string value_print = value_decoder.PPrint();
  printf("%s[%s] = %s\n",
         name_.c_str(), key_print.c_str(), value_print.c_str());
}
