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

#include <time.h>
#include <stdio.h>
#include <string>
#include <errno.h>
#include <sys/mman.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "utilities/sysutils.h"
#include "utilities/strutils.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/code.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/proc.h"
#include "engine/nativesupport.h"
#include "engine/elfgen.h"


namespace sawzall {


// -----------------------------------------------------------------------------
// Implementation of CodeDesc

CodeDesc* CodeDesc::New(Proc* proc, int index, Function* function, int begin, int end, int line_begin) {
  CodeDesc* c = NEW(proc, CodeDesc);
  c->index_ = index;
  c->function_ = function;
  c->begin_ = begin;
  c->end_ = end;
  c->line_begin_ = line_begin;
  assert(begin <= end);
  assert(begin % kAlignment == 0);
  assert(end % kAlignment == 0);
  assert(line_begin >= 0);
  return c;
}


// -----------------------------------------------------------------------------
// Implementation of TrapDesc

TrapDesc* TrapDesc::New(
  Proc* proc, int begin, int end, int target,
  int stack_height, int native_stack_height,
  VarDecl* var, int var_index, int var_delta,
  bool is_silent, const char* comment,
  TrapDesc* super
) {
  TrapDesc* t = NEW(proc, TrapDesc);
  t->Initialize(proc, begin, end, target, stack_height, native_stack_height,
                var, var_index, var_delta, is_silent, comment, super);
  return t;
}


void TrapDesc::AddTrap(int offset, VarDecl* var) {
  VarTrap trap = { offset, var };
  var_traps_->Append(trap);
}


void TrapDesc::print() const {
  F.print("TrapDesc [%d, %d[ -> %d, stack %d, native stack %d, var %d:%d",
          begin(), end(), target(), stack_height(), native_stack_height(),
          var_delta(), var_index());
  if (is_silent())
    F.print(", silent");
  F.print(" (%s)\n", comment());
}


void TrapDesc::Initialize(
  Proc* proc, int begin, int end, int target,
  int stack_height, int native_stack_height,
  VarDecl* var, int var_index, int var_delta,
  bool is_silent, const char* comment,
  TrapDesc* super
) {
  begin_ = begin;
  end_ = end;
  target_ = target;
  stack_height_ = stack_height;
  native_stack_height_ = native_stack_height;
  var_ = var;
  if (var != NULL)
    var->UsesTrapinfoIndex(proc);  // make sure we have a slot for this variable
  var_index_ = var_index;
  var_delta_ = var_delta;
  is_silent_ = is_silent;
  comment_ = comment;
  var_traps_ = List<VarTrap>::New(proc);
  super_ = super;
  assert(begin <= end);
  assert(!contains(target));  // otherwise danger of endless loops at run-time!
  assert(stack_height >= 0);
  assert(native_stack_height >= 0);
  // assert(var_index_ >= 0);   var_index is negative in native mode
  assert(var_delta_ >= 0);
}


void TrapDesc::InitializeAsSearchKey(int begin) {
  // Used only for calling Compare through List::BinarySearch.
  begin_ = begin;
}


// -----------------------------------------------------------------------------
// Implementation of Code

// For BinarySearch() and Sort() calls below
static int Compare(TrapDesc* const* x, TrapDesc* const* y) {
  return (*x)->begin() - (*y)->begin();
}


Code* Code::New(Proc* proc, Instr* base, List<CodeDesc*>* code_segments,
                List<TrapDesc*>* trap_ranges, List<Node*>* line_num_info) {
  Code* c = NEW(proc, Code);
  c->Initialize(proc, base, code_segments, trap_ranges, line_num_info);
  return c;
}


void Code::Cleanup() {
  // unmap pages containing native code
  if (native_ && code_buffer_ != NULL) {
    MemUnmapCode(code_buffer_, code_buffer_size_);
    code_buffer_ = NULL;
    base_ = NULL;  // to make premature cleanup more noticeable
  }
}


CodeDesc* Code::DescForIndex(int index) {
  return code_segments_->at(index);
}


CodeDesc* Code::DescForInstr(Instr* pc) const {
  if (contains(pc)) {
    int offset = pc - base();
    for (int i = 0; i < code_segments_->length(); i++)
      if (code_segments_->at(i)->contains(offset))
        return code_segments_->at(i);
  }
  return NULL;
}


Function* Code::FunctionForInstr(Instr* pc) const {
  CodeDesc* desc = DescForInstr(pc);
  if (desc != NULL)
    return desc->function();
  return NULL;
}


const TrapDesc* Code::TrapForInstr(Instr* pc) const {
  // find closest begin offset via binary search
  const int offs = pc - base();
  TrapDesc key;
  key.InitializeAsSearchKey(offs);  // set only "begin_" for use in Compare
  const int i = trap_ranges_->BinarySearch(&key, Compare);
  // determine appropriate trap range
  if (trap_ranges_->valid_index(i)) {
    // found an entry
    const TrapDesc* desc = trap_ranges_->at(i);
    // if the entry doesn't contain the offset, it must be
    // contained in one of the enclosing ranges, if any
    // (note: this happens only for nested trap ranges,
    // which occur with def(x) expressions, and thus are
    // relatively infrequent)
    while (desc != NULL && !desc->contains(offs))
      desc = desc->super();
    assert(desc == NULL || desc->contains(offs));
    return desc;
  } else {
    // no entry found
    return NULL;
  }
}


void Code::DisassembleRange(Instr* begin, Instr* end, int line_index) {
#if defined(__i386__)
    const char* cmd = "/usr/bin/objdump -b binary -m i386 -D /tmp/funcode";
    const char* mov_helper_addr = ":\tb8 ";  // mov ..., %eax
#elif defined(__x86_64__)
    const char* cmd = "/usr/bin/objdump -b binary -m i386:x86-64 -D /tmp/funcode";
    const char* mov_helper_addr = ":\t49 c7 c3 ";  // mov ..., %r11
#endif
  const Instr* pc = begin;
  if (native_) {
    FILE* fp;
    fp = fopen("/tmp/funcode", "w");
    if (fp == NULL)
      FatalError("Could not open tmp file");

    size_t written = fwrite(pc, 1, end - begin, fp);
    CHECK_EQ(written, implicit_cast<size_t>(end - begin));
    fclose(fp);
    string disassembly;
    RunCommand(cmd, &disassembly);
    // skip header
    string header = "<.data>:\n";
    string::size_type pos = disassembly.find(header, 0);
    if (pos != string::npos)
      pos += header.length();

    // prefix each disassembled instruction with its absolute address in hex
    // and its relative address (to code base) in decimal
    while (pos != string::npos && pos < disassembly.length()) {
      const string::size_type eol_pos = disassembly.find('\n', pos);
      if (eol_pos != string::npos) {
        string instr = disassembly.substr(pos, eol_pos - pos);
        const uint64 rel_pc = ParseLeadingHex64Value(instr.c_str(), kuint64max);
        if (rel_pc != kuint64max) {
          pc = begin + rel_pc;
          // print line num info if pc >= next known line info entry
          // only print the last line info if several apply to the same pc
          bool print_line_info = false;
          while (line_num_info_ != NULL && line_index < line_num_info_->length() &&
            pc - base() >= line_num_info_->at(line_index)->code_range()->beg) {
            line_index++;
            print_line_info = true;
          }
          if (print_line_info)
            F.print("%L\n", line_num_info_->at(line_index - 1)->file_line());
          if (instr.find(mov_helper_addr, 0) != string::npos) {
            const string::size_type ptr_pos = instr.find("$0x", 0);
            if (ptr_pos != string::npos) {
              // try to identify helper address loaded in eax/r11 in "mov $helper,%eax/%r11"
              const uint64 imm = ParseLeadingHex64Value(instr.c_str() + ptr_pos + 3, kuint64max);
              const char* helper;
              if (imm != kuint64max && (helper = NSupport::HelperName(imm)) != NULL) {
                instr.append("  ; ");
                instr.append(helper);
              }
            }
          }
          F.print("%p (%5d):  %s\n", pc, pc - base(), instr.c_str());
        }
      }
      pos = eol_pos + 1;
    }

  } else {
    while (pc < end)
      // F.print increases the pc by the instruction size
      F.print("%p (%4d):  %I\n", pc, pc - base(), &pc);
  }
}


void Code::DisassembleDesc(CodeDesc* desc) {
  Function* fun = desc->function();
  if (fun != NULL)
    F.print("--- %s: %T\n", fun->name(), fun->type());
  else if (native_ && desc->begin() == 0)
    F.print("--- STUBS\n");
  else
    F.print("--- INIT\n");
  DisassembleRange(base() + desc->begin(), base() + desc->end(), desc->line_begin());
  F.print("\n");
}


void Code::Disassemble() {
  // print code segments
  for (int i = 0; i < code_segments_->length(); i++)
    DisassembleDesc(code_segments_->at(i));

  // print trap ranges
  F.print("--- TRAPS\n");
  for (int i = 0; i < trap_ranges_->length(); i++)
    trap_ranges_->at(i)->print();
  F.print("\n");
}


bool Code::GenerateELF(const char* name,
                       uintptr_t* map_beg, uintptr_t* map_end, int* map_offset) {
  assert(native_);  // should never be called in interpreted mode
  assert(base() != NULL);  // code must be generated first

  ELFGen elf;

  // code
  elf.AddCode(base(), size(), map_beg, map_end, map_offset);

  // symbols
  for (int i = 0; i < code_segments_->length(); i++) {
    CodeDesc* desc = code_segments_->at(i);
    Function* fun = desc->function();
    string fun_name = "sawzall_native::";
    if (fun != NULL) {
      if (fun->name() != NULL)
        fun_name += fun->name();
      else
        fun_name += "$closure";  // fun is anonymously defined and assigned
    } else if (desc->begin() == 0) {
      fun_name += "STUBS";
    } else {
      fun_name += "INIT";
    }
    elf.AddFunction(fun_name, base() + desc->begin(), desc->end() - desc->begin());
  }

  // debug line info
  int prev_beg = 0;
  for (int i = 0; i < line_num_info_->length(); i++) {
    Node* node = line_num_info_->at(i);
    int beg = node->code_range()->beg;
    int end = node->code_range()->end;
    if (FLAGS_v > 1)
      F.print("%s:%d [%d,%d[\n%1N\n", node->file(), node->line(), beg, end, node);
    // skip empty code ranges
    if (end > beg) {
      assert(beg >= prev_beg);
      elf.AddLine(node->file(), node->line(), base() + beg);
      prev_beg = beg;
    }
  }
  elf.EndLineSequence(base() + size());

  return elf.WriteFile(name);
}

#ifndef MAP_ANONYMOUS
#ifdef MAP_ANON
#define MAP_ANONYMOUS MAP_ANON
#else
#error Neither MAP_ANONYMOUS nor MAP_ANON are defines.
#endif
#endif

void Code::MemMapCode(Instr* base, size_t size,
                      Instr** mapped_base, size_t* mapped_size) {
  // memory map the native code to executable pages
  const size_t msize = Align(size, getpagesize());
  Instr* mbase = static_cast<Instr*>(
    mmap(NULL, msize, PROT_READ | PROT_WRITE | PROT_EXEC,
         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

  if (mbase == MAP_FAILED)
    FatalError("Failed to map memory for native code (errno: %d)", errno);

  memcpy(mbase, base, size);
  FlushInstructionCache(mbase, size);

  *mapped_base = mbase;
  *mapped_size = msize;
}


void Code::MemUnmapCode(Instr* mapped_base, size_t mapped_size) {
  munmap(mapped_base, mapped_size);
}


void Code::Initialize(Proc* proc, Instr* base, List<CodeDesc*>* code_segments,
                      List<TrapDesc*>* trap_ranges, List<Node*>* line_num_info) {
  base_ = NULL;
  code_segments_ = code_segments;
  trap_ranges_ = trap_ranges;
  line_num_info_ = line_num_info;
  init_ = -1;
  main_ = -1;
  native_ = (proc->mode() & Proc::kNative) != 0;

  // 1) setup code
  assert(base != NULL);
  assert(code_segments != NULL && code_segments->length() >= 1);
#ifndef NDEBUG
  // assert that code_segments are in consecutive address ranges
  for (int i = 1; i < code_segments->length(); i++)
    assert(code_segments->at(i-1)->end() == code_segments->at(i)->begin());
#endif // NDEBUG
  // determine special entry points
  for (int i = 0; i < code_segments->length(); i++) {
    Function* f = code_segments->at(i)->function();
    if (f == NULL)
      init_ = code_segments->at(i)->begin();
    else if (f->name() != NULL && strcmp(f->name(), "$main") == 0)
      main_ = code_segments->at(i)->begin();
  }
  assert(init_ >= 0);  // must exist
  assert(main_ >= 0);  // must exist
  // copy the code into a contiguous chunk of memory
  size_t size = this->size();  // this requires that code_segment_ is setup!
  if (native_) {
    // allocate the native code buffer in mapped memory so that corresponding
    // pages can be marked as executable
    MemMapCode(base, size, &code_buffer_, &code_buffer_size_);

    // no base_ alignment necessary, since code_buffer_ is aligned to page size,
    // which is larger than CodeDesc::kAlignment
    base_ = code_buffer_;

  } else {
    // allocate the byte code buffer on the heap
    code_buffer_size_ = size + CodeDesc::kAlignment - 1;
    code_buffer_ = NEW_ARRAY(proc, Instr, code_buffer_size_);
    // make sure the code base is aligned to CodeDesc::kAlignment (16) and not
    // just to Memory::kAllocAlignment (8)
    base_ = reinterpret_cast<Instr*>(Align(reinterpret_cast<intptr_t>(code_buffer_),
                                           CodeDesc::kAlignment));
    memcpy(base_, base, size);  // we know that this->size() is in bytes!
  }

  // 2) setup trap ranges
  // sort them according to their begin offset
  // so we can use binary search for lookup
  assert(trap_ranges_ != NULL);
  trap_ranges->Sort(Compare);
}


} // namespace sawzall
