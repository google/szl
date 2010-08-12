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
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <utility>
#include <vector>
#include <string>
#include <algorithm>

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/frame.h"
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/engine.h"
#include "public/emitterinterface.h"
#include "public/sawzall.h"
#include "engine/outputter.h"

namespace {

using sawzall::Val;
using sawzall::TaggedInts;

// Helper function for sorting the keys of a map. This function is called
// by "std::sort()" to compare two
// keys, a and b. The function returns true if a.first < b.first.
bool MapKeySorter(const pair<Val *, int>& a,
                  const pair<Val *, int>& b) {
  if (b.first == NULL) {
    return false;
  } else if (a.first == NULL) {
    return true;
  }
  Val* d = a.first->Cmp(b.first);
  assert(!TaggedInts::is_null(d));
  return (TaggedInts::as_int(d) < 0);
}

}  // namespace


namespace sawzall {

// The object returned by OpenFile; hides details of whether
// file is a plain fd or a pipe or ...
class EmitFile {
 public:
  // constructor
  EmitFile(char* name, int len, int fd, FILE* file, bool is_proc)
    : name_(name),
      len_(len),
      fd_(fd),
      file_(file),
      is_proc_(is_proc) {}
  ~EmitFile()  { if (file_ != NULL) fclose(file_); free(name_); }

  // accessors
  char* name() const  { return name_; }
  int len() const  { return len_; }
  bool is_proc() const  { return is_proc_; }

  // management and I/O
  bool IsEqual(char* name, int len, bool is_proc);
  bool Flush();
  int64 Write(void* buf, unsigned int len);

 private:
  char* name_;
  int len_;
  int fd_;
  FILE* file_;
  bool is_proc_;
};


// Return whether this file name is the same as this EmitFile's
bool EmitFile::IsEqual(char* name, int len, bool is_proc) {
  return is_proc == is_proc_ &&
         len == len_ &&
         strncmp(name, name_, len) == 0;
}


// Write to file associated with this EmitFile
int64 EmitFile::Write(void *buf, unsigned int len) {
  if (file_ != NULL) {
    // buffered file
    return fwrite(buf, 1, len, file_);
  } else {
    // Unix file
    return write(fd_, buf, len);
  }
}


// Flush any pending data.  Only matters if using buffered files.
bool EmitFile::Flush() {
  if (file_ != NULL)
    return (fflush(file_) == 0);
  else
    return true;
}


Outputter::Outputter(Proc* proc, TableInfo* table)
  : proc_(proc),
    table_(table),
    emitter_(NULL),
    emit_count_(0),
    open_files_() {
}


Outputter::~Outputter() {
  for (int i = 0; i < open_files_.size(); i++)
    delete open_files_[i];
}


void Outputter::PutValue(Type* type, Emitter* emitter, Val**& sp,
                         bool on_stack) {
  assert(type != NULL);
  Val* v = Engine::pop(sp);
  // Note: the reference count for "v" is conditionally decremented just before
  // returning; take care not to return from the middle of this function.

  if (type->is_basic()) {
    BasicType* basic_type = type->as_basic();
    switch (basic_type->kind()) {
      case BasicType::BOOL:
        emitter->PutBool(v->as_bool()->val() != 0);
        break;
      case BasicType::BYTES: {
          BytesVal* a = v->as_bytes();
          emitter->PutBytes(a->base(), a->length());
        }
        break;
      case BasicType::INT:
        emitter->PutInt(v->as_int()->val());
        break;
      case BasicType::UINT:
        emitter->PutInt(v->as_uint()->val());
        break;
      case BasicType::FLOAT:
        emitter->PutFloat(v->as_float()->val());
        break;
      case BasicType::FINGERPRINT:
        emitter->PutFingerprint(v->as_fingerprint()->val());
        break;
      case BasicType::STRING: {
          StringVal* a = v->as_string();
          emitter->PutString(a->base(), a->length());
        }
        break;
      case BasicType::TIME:
        emitter->PutTime(v->as_time()->val());
        break;
      default:
        ShouldNotReachHere();
        break;
    }

  } else if (type->is_tuple()) {
    TupleVal* t = v->as_tuple();
    List<Field*>* fields = type->as_tuple()->fields();
    const int n = fields->length();
    emitter->Begin(Emitter::TUPLE, n);
    for (int i = 0; i < n; i++) {
      Field* f = fields->at(i);
      Val** sp = &t->field_at(f);  // PutValue changes sp - need a temporary
      PutValue(f->type(), emitter, sp, false);
    }
    emitter->End(Emitter::TUPLE, n);

  } else if (type->is_array()) {
    ArrayVal* a = v->as_array();
    Type* elem_type = type->as_array()->elem_type();
    const int n = a->length();
    emitter->Begin(Emitter::ARRAY, n);
    for (int i = 0; i < n; i++) {
      Val** sp = &a->at(i);  // PutValue changes sp - need a temporary
      PutValue(elem_type, emitter, sp, false);
    }
    emitter->End(Emitter::ARRAY, n);

  } else if (type->is_map()) {
    MapVal* mv = v->as_map();
    Type* index_type = type->as_map()->index_type();
    Type* elem_type = type->as_map()->elem_type();
    Map* m = mv->map();
    const int n = m->occupancy();

    // First we must sort the elements by keys, since that will make summing
    // of maps much easier.
    vector<pair<Val *, int> > sorted_keys;
    sorted_keys.reserve(n);

    for (int i = 0; i < n; i++) {
      sorted_keys.push_back(make_pair(m->GetKeyByIndex(i), i));
    }
    sort(sorted_keys.begin(), sorted_keys.end(), MapKeySorter);

    // We specify the total number of keys + values.
    const int total_elements = n * 2;
    emitter->Begin(Emitter::MAP, total_elements);

    for (int i = 0; i < n; i++) {
      int map_index = sorted_keys[i].second;
      assert(map_index >= 0 && map_index < n);

      // Output key
      {
        // PutValue changes sp - need a temporary
        Val* sp = m->GetKeyByIndex(map_index);
        Val** ptr_sp = &sp;
        PutValue(index_type, emitter, ptr_sp, false);
      }
      // Output value
      {
        // PutValue changes sp - need a temporary
        Val* sp = m->GetValueByIndex(map_index);
        Val** ptr_sp = &sp;
        PutValue(elem_type, emitter, ptr_sp, false);
      }
    }
    emitter->End(Emitter::MAP, total_elements);

  } else if (type->is_function()) {
    FatalError("emitting of functions is unimplemented");

  } else {
    ShouldNotReachHere();
  }

  if (on_stack)
    v->dec_ref();
}


const char* Outputter::Emit(Val**& sp) {
  // we count all emits
  emit_count_++;

  // start out with a clean slate
  error_msg_ = NULL;

  // begin of emit
  const OutputType* type = this->type();

  // check for null emitters
  if (type->uses_emitter() && emitter_ == NULL) {
    error_msg_ = proc_->PrintError("no emitter installed for table %s; "
                                   "cannot emit", name());
    return error_msg_;
  }

  // special case common emits
  if (type->uses_emitter() && (type->elem_format_args() == NULL)  &&
      (type->elem_type()->is_basic()) && (type->weight() == NULL)) {
    Val *v;
    List<VarDecl*>* index_decls = type->index_decls();
    const int n = index_decls->length();
    if (n == 0) {
      BasicType* basic_type = type->elem_type()->as_basic();
      switch (basic_type->kind()) {
        case BasicType::INT:
          v = Engine::pop(sp);
          emitter_->EmitInt(v->as_int()->val());
          v->dec_ref();
          return NULL;
        case BasicType::FLOAT:
          v = Engine::pop(sp);
          emitter_->EmitFloat(v->as_float()->val());
          v->dec_ref();
          return NULL;
        default:
          break;
      }
    }
  }

  if (type->uses_emitter())
    emitter_->Begin(Emitter::EMIT, 1);

  // handle indices or formatted index string, if any
  EmitFile* file = NULL;
  if (type->uses_emitter()) {
    assert(emitter_ != NULL);
    // regular case: emit to mill
    assert(type->index_format_args() == NULL);
    List<VarDecl*>* index_decls = type->index_decls();
    const int n = index_decls->length();
    if (n > 0) {
      emitter_->Begin(Emitter::INDEX, n);
      for (int i = 0; i < n; i++) {
        PutValue(index_decls->at(i)->type(), emitter_, sp, true);
      }
      emitter_->End(Emitter::INDEX, n);
    }
  } else {
    // emit to file or proc instead of mill
    assert(type->index_format_args() != NULL);
    StringVal* s = *reinterpret_cast<StringVal**&>(sp)++;
    file = OpenFile(s->base(), s->length(), type->is_proc());
    s->dec_ref();
    // OpenFile returns NULL if an error happened
    if (file == NULL) {
      assert(error_msg_ != NULL);  // set by OpenFile
      return error_msg_;
    }
  }


  // handle value or formatted value string
  if (type->elem_format_args() != NULL) {
    // formatted output
    StringVal* s = *reinterpret_cast<StringVal**&>(sp)++;
    if (file != NULL) {
      // emit to file/proc
      if (file->Write(s->base(), s->length()) < 0)
        Error(proc_->PrintError("write error for '%s': %r", name()));
    } else {
      // emit to mill
      emitter_->Begin(Emitter::ELEMENT, 1);
      emitter_->PutString(s->base(), s->length());
      emitter_->End(Emitter::ELEMENT, 1);
    }
    // clean up
    s->dec_ref();

  } else {
    // unformatted output
    if (file != NULL) {
      // emit to file/proc
      // only bytes are possible
      assert(type->elem_type()->is_bytes());
      BytesVal* b = *reinterpret_cast<BytesVal**&>(sp)++;
      if (file->Write(b->base(), b->length()) < 0)
        Error(proc_->PrintError("write error for '%s': %r", name()));
      b->dec_ref();
    } else {
      // emit to mill
      emitter_->Begin(Emitter::ELEMENT, 1);
      PutValue(type->elem_type(), emitter_, sp, true);
      emitter_->End(Emitter::ELEMENT, 1);
    }
  }

  // handle weight, if any
  if (type->weight() != NULL) {
    CHECK(file == NULL) << "cannot handle file/proc and weight attributes combined";
    emitter_->Begin(Emitter::WEIGHT, 1);
    PutValue(type->weight()->type(), emitter_, sp, true);
    emitter_->End(Emitter::WEIGHT, 1);
  }

  // end of emit
  if (type->uses_emitter())
    emitter_->End(Emitter::EMIT, 1);

  // done
  return error_msg_;
}


void Outputter::Error(const char* error_msg) {
  // register only first error
  if (error_msg_ == NULL)
    error_msg_ = error_msg;
}


EmitFile* Outputter::OpenFile(char* str, int len, bool is_proc) {
  EmitFile *ef;
  // TODO: make this lookup more efficient
  for (int i = 0; i < open_files_.size(); i++) {
    ef = open_files_[i];
    if (ef->IsEqual(str, len, is_proc))
      return ef;
  }
  // not yet in cache; set it up.
  char* temp_str = proc_->PrintString("%.*s", len, str);
  string s = temp_str;
  FREE(proc_, temp_str);
  int fd = -1;
  FILE* file = NULL;
  if (is_proc) {
    if (proc_->mode() & Proc::kSecure) {
      Error(proc_->PrintError(
        "access to proc(%s) forbidden in this context", name()));
      return NULL;
    }
    // TODO: wait? probably not - they're not supposed to exit!
    // TODO: SIGPIPE handler.
    int pid;
    int pfd[2];
    if (pipe(pfd) < 0) {
      Error(proc_->PrintError("can't create pipe: %r"));
      return NULL;
    }
    // we write to pfd[1]; child reads from pfd[0]
    fd = pfd[1];
    switch (pid = fork()) {
      case -1:  // error
        Error(proc_->PrintError("can't create child process: %r"));
        return NULL;

      case 0: // child
        close(0);
        dup(pfd[0]);
        // close everything above stderr
        for (int i = 3; i < 100; i++)
          close(i);
        // now exec the process
        execl("/bin/sh", "sh", "-c", s.c_str(), NULL);
        // child process => leave FatalError
        FatalError("can't exec shell: %r");  // exits program

      default:  // parent
        close(pfd[0]);
        break;
    }
  } else if (s.compare(0, 5, "/gfs/", 5) == 0 ||
             s.compare(0, 11, "/namespace/", 11) == 0) {
    // create the file and remember it in the cache.
    if (proc_->mode() & Proc::kSecure) {
      Error(proc_->PrintError(
        "access to file(%s) forbidden in this context", name()));
      return NULL;
    }
    file = fopen(s.c_str(), "w");
    if (file == NULL)
      F.fprint(2, "szl: can't open %s: %r\n", s.c_str());
    // if we can't create it, remember the fact for next time
    // by just continuing with file == NULL
  } else {
    // create the file and remember it in the cache.
    // /dev/stdout should be a dup of fd 1 (if it is open) so that
    // writes to it and to fd 1 use the same file pointer
    // (if it weren't for procs, redefining DefineOutputStringVar to be
    // the int of the file descriptor would be better)
    if (s == "/dev/stdout")
      fd = dup(1);
    else if (s == "/dev/stderr")
      fd = dup(2);
    if (fd < 0) {
      if (proc_->mode() & Proc::kSecure) {
        Error(proc_->PrintError(
          "access to file(%s) forbidden in this context", name()));
        return NULL;
      }
      fd = creat(s.c_str(), 0664);
    }
    if (fd < 0)
      F.fprint(2, "szl: can't open %s: %r\n", s.c_str());
    // if we can't create it, remember the fact for next time
    // by just continuing with fd < 0
  }
  // make a copy of str, since it must survive the execution
  char* copy = (char*)malloc(len);
  memmove(copy, str, len);
  ef = new EmitFile(copy, len, fd, file, is_proc);  // explicitly deallocated by destructor
  open_files_.push_back(ef);
  return ef;
}

}  // namespace sawzall
