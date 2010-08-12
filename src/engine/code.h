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

namespace sawzall {


// A CodeDesc provides the connection between
// a code segment and a Sawzall function. Segments
// are described by position-independent offsets from
// the start of the code.

class CodeDesc {
 public:
  // Alignment for individual CodeDescs. Both the begin
  // and end (offsets) must be aligned to kAlignment.
  enum { kAlignment = 16 };

  // Creation
  static CodeDesc* New(Proc* proc, int index, Function* function,
                       int begin, int end, int line_begin);

  int index() const  { return index_; }
  Function* function() const  { return function_; }
  int begin() const  { return begin_; }
  int end() const  { return end_; }
  int line_begin() const  { return line_begin_; }
  size_t size() const  { return end_ - begin_; }
  bool contains(int pos) const  { return begin_ <= pos && pos < end_; }

 private:
  int index_;  // index of this code segment in list owned by Code object
  Function* function_;  // function compiled into this code segment
  int begin_;  // code begin offset in code block owned by Code object
  int end_;  // code end offset in code block owned by Code object
  int line_begin_;  // index of first line info entry for this code segment in
                    // global list owned by Code object

  // Prevent construction from outside the class (must use factory method)
  CodeDesc() { /* nothing to do */ }
};


// A TrapDesc provides the connection between a range of code and
// a trap handler. Ranges are described by position-independent offsets
// from the start of the code. A range is described by an interval
// [begin, end[. Ranges may nest, in which case they have an enclosing
// (or super) trap range - however, they never partially overlap.

class TrapDesc {
 public:
  // Creation
  static TrapDesc* New(
    Proc* proc, int begin, int end, int target,
    int stack_height, int native_stack_height,
    VarDecl* var, int var_index, int var_delta,
    bool is_silent, const char* comment,
    TrapDesc* super
  );

  // Trap to variable mapping.
  struct VarTrap {
    int code_offset;
    VarDecl* var;
  };

  // Accessors
  int begin() const  { return begin_; }
  int end() const  { return end_; }
  int target() const  { return target_; }
  int stack_height() const  { return stack_height_; }
  int native_stack_height() const  { return native_stack_height_; }
  bool contains(int pos) const  { return begin_ <= pos && pos < end_; }
  VarDecl* var() const  { return var_; }
  int var_index() const  { return var_index_; }
  int var_delta() const  { return var_delta_; }
  bool is_silent() const  { return is_silent_; }
  const char* comment() const  { return comment_; }
  const List<VarTrap>* var_traps() const  { return var_traps_; }
  TrapDesc* super() const  { return super_; }

  // Modifiers
  void AddTrap(int offset, VarDecl* var);

  // Debugging
  void print() const;

 private:
  int begin_;  // the begin of the trap range [begin_, end_[
  int end_;  // the end of the trap range [begin_, end_[
  int target_;  // the target offset after processing the trap
  int stack_height_;  // the stack height relative to the fp at the target
  int native_stack_height_;  // the native stack height at the target
  VarDecl* var_;   // decl of variable, if any, to be undefined
  int var_index_;  // index of variable, if any, to be undefined
  int var_delta_;  // (lexical) context difference for the variable
  bool is_silent_;  // silent traps never cause program termination
  const char* comment_;  // describes the corresponding code
  List<VarTrap>* var_traps_;  // individual traps where vars tested
  TrapDesc* super_;  // the enclosing trap range

  friend class TrapHandler;  // in codegen.cc
  friend class NTrapHandler;  // in nativecodegen.cc

  void Initialize(
    Proc* proc, int begin, int end, int target,
    int stack_height, int native_stack_height,
    VarDecl* var, int var_index, int var_delta,
    bool is_silent, const char* comment,
    TrapDesc* super
  );
  void InitializeAsSearchKey(int begin);

  // Prevent construction from outside the class (must use factory method)
  TrapDesc() { /* nothing to do */ }

  // Code::TrapForInstr needs to create a TrapDesc on
  // the stack and thus requires access to Initialize and
  // the TrapDesc constructor.
  friend class Code;
};


// A Code object holds the compiled code for a
// Sawzall executable. It provides access to entry
// points for various code segments, has support
// for disassembling the code and implements
// general static code accessors.

class Code {
 public:
  // Construction/destruction
  // The descs argument provides the connection between
  // individual functions and the generated code. The
  // constructor copies the code into a contiguous
  // heap-allocated block of memory and adjusts its
  // entry points accordingly.
  static Code* New(Proc* proc, Instr* base, List<CodeDesc*>* code_segments,
                   List<TrapDesc*>* trap_ranges, List<Node*>* line_num_info);
  void Cleanup();

  // All code
  Instr* base() const  { return base_; }
  size_t size() const  { return code_segments_->last()->end(); }
  bool contains(Instr* pc) const  { return base() <= pc && pc < base() + size(); }
  int number_of_segments() const  { return code_segments_->length(); }

  // Special entry points
  Instr* init() const  { return base() + init_; }
  Instr* main() const  { return base() + main_; }

  // CodeDesc for a given segment index
  CodeDesc* DescForIndex(int index);

  // CodeDesc for a given pc (or NULL)
  CodeDesc* DescForInstr(Instr* pc) const;

  // Function for a given pc (or NULL)
  Function* FunctionForInstr(Instr* pc) const;

  // TrapDesc for a given pc (or NULL)
  const TrapDesc* TrapForInstr(Instr* pc) const;

  // Printing
  void DisassembleRange(Instr* begin, Instr* end, int line_index);
  void DisassembleDesc(CodeDesc* desc);
  void Disassemble();

  // Profiling of native code
  // Generate an ELF file containing the native code, its symbols and line info;
  // map_beg, map_end, and map_offset (if non-NULL) are set to describe where
  // the text section of the generated ELF file would be mapped in memory;
  // these values are normally found in /proc/self/map for loaded libraries.
  // Returns true on success
  bool GenerateELF(const char* name,
                   uintptr_t* map_beg, uintptr_t* map_end, int* map_offset);

  // the relationship between Nodes and source
  List<Node*>* LineNumInfo() { return line_num_info_; }

  // --------------------------------------------------------------------------
  // Static inlined accessors - used by the interpreter:
  // return a pointer to a field at the current pc, and
  // increment the pc by the size of the field
  //
  // CAUTION: The performance of these functions is crucial! We rely
  // on the fact that they get inlined away - do not make any changes
  // w/o fully understanding the performance implications.
  //
  // In particular, if the Instr** parameter type is used instead of Instr*&,
  // at the call site we need to take the address of the pc: &pc. Even
  // though the functions are inlined, the compiler is forced to allocate
  // pc in memory instead of a register).
  //
  // Note: This code relies on the fact that sizeof(Instr) == 1.
  // This is asserted in the constructor for Code().

#define ACCESS(type, pc) \
  { type* tmp = reinterpret_cast<type*>(pc); \
    (pc) += sizeof(type); \
    return *tmp; \
  }

  typedef int32 pcoff;

  static unsigned char& uint8_at(Instr*& pc)  ACCESS(unsigned char, pc)  // unsigned
  static int8& int8_at(Instr*& pc)  ACCESS(int8, pc)
  static int16& int16_at(Instr*& pc)  ACCESS(int16, pc)
  static int32& int32_at(Instr*& pc)  ACCESS(int32, pc)
  static pcoff& pcoff_at(Instr*& pc)  ACCESS(pcoff, pc)
  static void*& ptr_at(Instr*& pc)  ACCESS(void*, pc)
  static Val*& val_at(Instr*& pc)  ACCESS(Val*, pc)
#undef ACCESS

  // Static methods mapping native code in executable pages; also used by unittest
  static void MemMapCode(Instr* base, size_t size,
                         Instr** mapped_base, size_t* mapped_size);
  static void MemUnmapCode(Instr* mapped_base, size_t mapped_size);

  // Flush instruction cache before executing generated code, no-op for x86
  static void FlushInstructionCache(Instr* base, size_t size) {
    // no-op for x86 code
  }

 private:
  Instr* code_buffer_;  // malloc'ed or mapped (for native code) chunk of memory
  size_t code_buffer_size_;
  Instr* base_;  // aligned code base in code_buffer_
  List<CodeDesc*>* code_segments_;
  List<TrapDesc*>* trap_ranges_;
  List<Node*>* line_num_info_;
  int init_;
  int main_;
  bool native_;

  void Initialize(Proc* proc, Instr* base, List<CodeDesc*>* code_segments,
                  List<TrapDesc*>* trap_ranges, List<Node*>* line_num_info);

  // Prevent construction from outside the class (must use factory method)
  Code() { /* nothing to do */ }
};

} // namespace sawzall
