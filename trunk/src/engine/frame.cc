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

#include <assert.h>

#include "engine/globals.h"
#include "public/commandlineflags.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/code.h"
#include "engine/frame.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/tracer.h"
#include "engine/codegen.h"
#include "engine/compiler.h"
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"


namespace sawzall {

FrameIterator::FrameIterator(Proc* proc, Frame* fp, NFrame* nfp, Val** sp, Instr* pc)
  : proc_(proc),
    fp_(fp),
    nfp_(nfp),
    sp_(sp),
    pc_(pc),
    native_(nfp != NULL) {
  assert(fp == NULL || nfp == NULL);
}


bool FrameIterator::is_valid() const {
  if (native_)
    // we always start unwinding from a valid native Sawzall frame
    // native frames are contiguous and never mixed with non-Sawzall frames
    // the first invalid (non-Sawzall) frame is the frame that called init or main
    return sp() <= proc()->native_bottom_sp();
  else
    return pc() != NULL;
}


void FrameIterator::Unwind() {
  assert(is_valid());
  if (native_) {
    pc_ = nfp()->return_pc();
    nfp_ = nfp()->dynamic_link();
    Function* f = function();
    // this test can go away once all code is in functions:
    size_t frame_size = f == NULL ? NFrame::kInitFrameSize : f->frame_size();
    sp_ = &nfp()->at(-frame_size / sizeof(Val*));

  } else {
    pc_ = fp()->return_pc();
    if (is_valid()) {
      Function* f = function();
      if (f != NULL) {  // this test can go away once all code is in functions
        sp_ = &(fp()->at(0)) + (f->frame_size() / sizeof(Val*));
      }
      fp_ = fp()->dynamic_link();
    }
  }
}


Function* FrameIterator::function() const {
  assert(is_valid());
  return proc()->code()->FunctionForInstr(pc());
}


void FrameIterator::PrintFrame(Fmt::State* f, int frame_id) const {
  const char* indent = "     ";
  F.fmtprint(f, "%3d. ", frame_id);  // consumes as much space as indent string
  Function* fun = function();
  if (fun != NULL) {
    // function name, if any
    if (fun->name() != NULL)
      F.fmtprint(f, "%s: ", fun->name());
    // function signature
    F.fmtprint(f, "%T", fun->ftype());

    if (FLAGS_v > 0) {
      F.fmtprint(f, " (level = %d)\n%s", fun->level(), indent);
      PrintLinkage(f);
    }
    F.fmtprint(f, "\n");

    List<VarDecl*>* locals = fun->locals();
    for (int i = 0; i < locals->length(); i++) {
      if (locals->at(i)->name() != NULL) {
        F.fmtprint(f, indent);
        PrintVar(f, locals->at(i));
        F.fmtprint(f, "\n");
      }
    }
  } else {
    F.fmtprint(f, "no symbolic frame information (initialization frame?)\n%s", indent);
    if (FLAGS_v > 0) {
      PrintLinkage(f);
    } else {
      F.fmtprint(f, "(use --v=1 for more details)");
    }
    F.fmtprint(f, "\n");
  }
  F.fmtprint(f, "\n");
}


void FrameIterator::PrintStack(Fmt::State* f, int nframes, Proc* proc, Frame* fp, NFrame* nfp, Val** sp, Instr* pc) {
  // make sure nframes is >= 0
  if (nframes < 0)
    nframes = 0;
  // determine stack length
  int length = 0;
  for (FrameIterator fit(proc, fp, nfp, sp, pc); fit.is_valid(); fit.Unwind())
    length++;
  // determine number of top and bottom frames
  // to print and number of middle frames to skip
  int ntop = length;
  int nmid = 0;
  int nbot = 0;
  if (length > nframes) {
    ntop = (nframes + 1) / 2;  // round up for ntop
    nmid = length - nframes;
    nbot = nframes - ntop;
  }
  // print frames
  F.fmtprint(f, "Stack trace:\n");
  int n = 0;
  FrameIterator fit(proc, fp, nfp, sp, pc);
  // print top frames
  for (int i = 0; i < ntop; i++) {
    fit.PrintFrame(f, n);
    fit.Unwind();
    n++;
  }
  // skip middle frames, if any
  if (nmid > 0) {
    for (int i = 0; i < nmid; i++)
      fit.Unwind();
    F.fmtprint(f, "...  (skipping frames %d to %d)\n\n", n, n + nmid - 1);
    n += nmid;
  }
  // print bottom frames
  for (int i = 0; i < nbot; i++) {
    fit.PrintFrame(f, n);
    fit.Unwind();
    n++;
  }
  // done
  assert(n == length && !fit.is_valid());
}


void FrameIterator::PrintStack(int fd, int nframes, Proc* proc, Frame* fp, Val** sp, Instr* pc) {
  Fmt::State f;
  char buf[1024];
  F.fmtfdinit(&f, fd, buf, sizeof buf);
  PrintStack(&f, nframes, proc, fp, NULL, sp, pc);
  F.fmtfdflush(&f);
}


void FrameIterator::PrintStack(int fd, int nframes, Proc* proc, NFrame* nfp, Val** sp, Instr* pc) {
  Fmt::State f;
  char buf[1024];
  F.fmtfdinit(&f, fd, buf, sizeof buf);
  PrintStack(&f, nframes, proc, NULL, nfp, sp, pc);
  F.fmtfdflush(&f);
}


void FrameIterator::PrintLinkage(Fmt::State* f) const {
  if (native_) {
    F.fmtprint(f, "fp = %p, sp = %p, pc = %p, dl = %p, sl = %p, ra = %p",
                  nfp(), sp(), pc(),
                  nfp()->dynamic_link(), nfp()->static_link(), nfp()->return_pc());
  } else {
    F.fmtprint(f, "fp = %p, sp = %p, pc = %p, dl = %p, sl = %p, ra = %p",
                  fp(), sp(), pc(),
                  fp()->dynamic_link(), fp()->static_link(), fp()->return_pc());
  }
}


void FrameIterator::PrintVar(Fmt::State* f, VarDecl* var) const {
  Type* type = var->type();
  const int index = var->offset() / sizeof(Val*);
  F.fmtprint(f, "%s", var->name());
  Val*& val = native_ ? nfp()->at(index) : fp()->at(index);
  if (FLAGS_v > 0)
    F.fmtprint(f, " @ %p (fp + %d)", val, var->offset());
  F.fmtprint(f, ": %T = %V", type, proc(), val);
}

}  // namespace sawzall
