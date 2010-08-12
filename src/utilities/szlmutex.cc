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

#include <memory.h>

#include "utilities/szlmutex.h"


// A simple wrapper for Posix thread locking.


SzlMutex::SzlMutex() {
  memset(&lock_, '\0', sizeof(lock_));
}


void SzlMutex::Lock() {
  pthread_mutex_lock(&lock_);
}


void SzlMutex::Unlock() {
  pthread_mutex_unlock(&lock_);
}


void SzlMutex::AssertHeld() const {
  // TODO: do this
}


void SzlMutex::AssertNotHeld() const {
  // TODO: do this
}


void SzlMutex::LockWhen(const Condition &cond) {
  // TODO: do this
}


SzlMutexLock::SzlMutexLock(SzlMutex* mutex) : mutex_(mutex) {
  mutex_->Lock();
}


SzlMutexLock::~SzlMutexLock() {
  mutex_->Unlock();
}
