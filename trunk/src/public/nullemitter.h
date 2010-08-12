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

// No-op implementation of the emitter interface that ignores emitted data.
// Can be used by tests and applications that want to run arbitrary Sawzall
// code, but do not care about emitted data.

#ifndef _PUBLIC_NULLEMITTER_H_
#define _PUBLIC_NULLEMITTER_H_

#include "public/emitterinterface.h"

class NullEmitter : public sawzall::Emitter {
 public:
  NullEmitter() {}
  virtual ~NullEmitter() {}

  virtual void Begin(GroupType type, int len) {}
  virtual void End(GroupType type, int len) {}
  virtual void PutBool(bool b) {}
  virtual void PutBytes(const char* p, int len) {}
  virtual void PutInt(int64 i) {}
  virtual void PutFloat(double f) {}
  virtual void PutFingerprint(uint64 fp) {}
  virtual void PutString(const char* s, int len) {}
  virtual void PutTime(uint64 t) {}

  virtual void EmitInt(int64 i) {}
  virtual void EmitFloat(double f) {}
};


class NullEmitterFactory : public sawzall::EmitterFactory {
 public:
  NullEmitterFactory() {}
  ~NullEmitterFactory() {}

  sawzall::Emitter* NewEmitter(sawzall::TableInfo* table_info, string* error) {
    return &emitter_;
  }
 private:
  // Since emits are ignored we can use the same emitter for all tables
  NullEmitter emitter_;
};

#endif // _PUBLIC_NULLEMITTER_H_
