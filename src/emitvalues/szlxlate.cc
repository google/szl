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

#include <string>

#include "public/porting.h"
#include "public/logging.h"
#include "public/commandlineflags.h"
#include "public/hashutils.h"

#include "public/szldecoder.h"

#include "emitvalues/szlxlate.h"


DEFINE_bool(saw_use_key_from_double, true,
            "Use KeyFromDouble to encode floats; otherwise use EncodeDouble");


bool SzlXlate::IsTranslatableKeyType(const SzlType& type) {
  return type.kind() == SzlType::BOOL
         || type.kind() == SzlType::BYTES
         || type.kind() == SzlType::STRING
         || type.kind() == SzlType::FINGERPRINT
         || type.kind() == SzlType::INT
         || type.kind() == SzlType::TIME;
}

bool SzlXlate::IsTranslatableType(const SzlType& type) {
  return IsTranslatableKeyType(type) || type.kind() == SzlType::FLOAT;
}

void SzlXlate::TranslateValue(const SzlType& type,
                              SzlDecoder* dec,
                              string *out,
                              uint64* shardfp) {
  SzlType::Kind kind = type.kind();

  if (kind == SzlType::STRING) {
    CHECK(dec->GetString(out));
  } else if (kind == SzlType::BYTES) {
    CHECK(dec->GetBytes(out));
  } else if (kind == SzlType::FLOAT) {
    double f;
    CHECK(dec->GetFloat(&f));
    if (FLAGS_saw_use_key_from_double) {
      KeyFromDouble(f, out);
    } else {
      out->assign(EncodeDouble(f));
    }
  } else {
    uint64 v;
    if (kind == SzlType::FINGERPRINT) {
      CHECK(dec->GetFingerprint(&v));
    } else if (kind == SzlType::INT) {
      int64 iv;
      CHECK(dec->GetInt(&iv));
      v = iv;
    } else if (kind == SzlType::TIME) {
      CHECK(dec->GetTime(&v));
    } else if (kind == SzlType::BOOL) {
      bool b;
      CHECK(dec->GetBool(&b));
      v = b;
    } else {
      LOG(ERROR) << "can't translate szl type kind " << kind;
    }

    // Integer keyed tables get sharded by key.
    *shardfp = v;
    KeyFromUint64(v, out);
    return;
  }

  // All other tables get sharded by fingerprint of key.
  *shardfp = FingerprintString(out->data(), out->size());
}
