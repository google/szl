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

// We need PRId64, which is only defined if we explicitly ask for it.
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <stdio.h>
#include <assert.h>

#include "engine/globals.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "utilities/strutils.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/code.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "public/sawzall.h"
#include "public/emitterinterface.h"
#include "engine/outputter.h"
#include "engine/linecount.h"
#include "engine/proc.h"

DECLARE_bool(szl_bb_count);

namespace sawzall {

uint64 LineCount::last_src_fingerprint_;
class CntPair;


LineCount::LineCount(Proc* proc) {
  proc_ = proc;
  emitter_ = NULL;
  counters_size_ = 0;
  counters_ = NULL;
}


LineCount::~LineCount() {
  delete [] counters_;
}


void LineCount::AllocCounters(int n) {
  assert(n >= 0);
  counters_size_ = n;
  counters_ = new int64[counters_size_];
  memset(counters_, '\0', counters_size_ * sizeof(int64));
}

void LineCount::ResetCounters() {
  for (int i = 0; i < counters_size_; i++) {
    counters_[i] = 0;
  }
}

// line-number, count pairs
// must be bitwise copyable: sorted using qsort()
struct CntPair {
  CntPair(Node* a, int64 b) : node(a), count(b)  {}
  CntPair() : node(NULL), count(0)  {}
  Node* node;
  int64 count;
  FileLine* file_line() const { return node->file_line(); }
};


static int CmpPair(const CntPair* x, const CntPair* y) {
  const FileLine* xfl = x->node->file_line();
  const FileLine* yfl = y->node->file_line();
  if (xfl->offset() != yfl->offset())
    return xfl->offset() - yfl->offset();
  return y->count - x->count;  // larger first
}


static void EmitStringInt(Emitter* emit, const char* key, int64 val) {
  emit->Begin(Emitter::EMIT, 1);
  emit->Begin(Emitter::INDEX, 1);
  emit->PutString(key, strlen(key));
  emit->End(Emitter::INDEX, 1);
  emit->Begin(Emitter::ELEMENT, 1);
  emit->PutInt(val);
  emit->End(Emitter::ELEMENT, 1);
  emit->End(Emitter::EMIT, 1);
}


// emit all the counts.  called at the end of each shard
void LineCount::Emit(const char* source) {
  Emitter* emit = emitter_;
  if (emit == NULL || FLAGS_szl_bb_count == false)
    return;

  CntPair* cnt_pairs = new CntPair[size()];
  for (int i = 0; i < size(); i++)
    cnt_pairs[i] = CntPair(proc_->LineNumInfo()->at(i), counter_at(i));
  qsort(cnt_pairs, size(), sizeof(CntPair),
        reinterpret_cast<int(*)(const void*, const void*)>(&CmpPair));

  for (int i = 0; i < size(); i++) {
    if (false) {  // make true, so the next maintainer can see what is going on
      printf("%d,%d\t%d\t%"PRId64"\n", i, cnt_pairs[i].node->line_counter(),
             cnt_pairs[i].file_line()->offset(),
             cnt_pairs[i].count);
    }
    Node* n = cnt_pairs[i].node;
    // Nodes other than line counting nodes can be in the list.  Futher
    // it is possible for different Nodes to get associated with one
    // position in the source, and in that case, we want the largest count.
    if (n->line_counter()
        && (i == 0 || cnt_pairs[i-1].file_line()->offset()
            != cnt_pairs[i].file_line()->offset())) {
      string v = StringPrintf("%08d", n->file_line()->offset());
      EmitStringInt(emit, v.c_str(), cnt_pairs[i].count);
    }
  }

  delete [] cnt_pairs;

  // and the raw source, done only once per process under control of the static
  // last_src_fingerprint_.
  // source might be NULL if this routine is called from mapreduce code, where
  // NULL should be passed by all but one of the mappers.
  if (source != NULL) {
    uint64 fp = FingerprintString(source, strlen(source));
    if (fp != last_src_fingerprint_) {
      EmitStringInt(emit, source, 1);
      last_src_fingerprint_ = fp;
    }
  }
}

}  // namespace sawzall
