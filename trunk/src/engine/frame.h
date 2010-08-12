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

// A Frame describes a raw Sawzall activation frame at run-time
// on the interpreter stack. It holds frame-specific information
// about the caller, and provides accessors to the frames's local variables.
//
// A frame pointer "fp" (Frame* or Frame's 'this' pointer) always points
// immediately *after* the actual stack location of the frame data (stored
// under fp[-1] or this[-1]), so locals (including parameters) can be accessed
// quickly.
//
// "fp" is the base for locals indexing. All variables in the generated
// code are accessed relative to "bp", so "bp" is set to "fp" of the function
// whose variables we are interested in. This is either done automatically
// when a frame is entered or explicitly using set_bp instruction when
// accessing variables in the outer scopes.
//
// Variables on the stack are 1-slot pointers to heap objects (unless they
// are small integers, smi, that are stored directly on the stack). Output
// variables are always static and allocated 2 stack slots within the init
// frame - an output temp used for format/proc/file and an output index
// providing a connection to the external emitter, but are passed around
// using their frame index to support the run-time 1-slot variable requirement.
//
// =============================================================================
// Non-native interpreter stack layout (grows from high to low addresses)
// =============================================================================
//
// The Sawzall stack is allocated as a continuous block of memory on the heap
// (see Proc::Proc in proc.cc).
//
// "yellow zone"    : minimum amount of space to be available at the top of the
//                    stack when entering a function, otherwise we assume
//                    stack overflow happened.
// expression stack : space for temp values used during expression evaluation
// dynamic link     : pointer to the base of the frame (fp) of calling function
// static link      : pointer to the base of the frame (fp) of enclosing scope,
//                    i.e. the function that textually encloses this one
//                    (the calling or the calling of the calling etc function)
// return address   : address of the instruction in the calling function where
//                    to continue execution after this function returns
//
//                                                                         A
//                     top                                                A|A
//    +++++++++++++++++++++++++++++++                                      |
//      LOW  :                      :   <-- proc->stack_ (end of stack)    |
//           :    "yellow zone"     :
//           :                      :
//           +----------------------+
//           :                      :   <-- proc->limit_sp() (limit pointer)
//           :                      :
//           :                      :
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//           :                      :   <-- sp of callee
//           :   expression stack   :
//  C        :                      :
//           +----------------------+
//  A     +--|     dynamic link     |   <-- fp->stack()
//        |  |     static link      |       (expr stack base of callee)
//  L     |  |    return address    |
//        |  +----------------------+
//  L     |  |       NO_INDEX       | 0 <-- fp of callee
//        |  |        local 1       | 1
//  E     |  :         ...          :
//        |  |        local N       |
//  E     |  +----------------------+
//        |  |        param 1       |
//        |  :          ...         :
//        |  |        param M       |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//        |  :                      :   <-- sp of caller
//        |  :   expression stack   :
//  C     |  :                      :
//        |  +----------------------+                                  --+--
//  A   +-|--|     dynamic link     |   <-- fp->stack()                  |
//      | |  |      static link     |       (exp stack base of caller) Frame
//  L   | |  |    return address    |                                    |
//      | |  +----------------------+                          --+--   --+--
//  L   | +->|       NO_INDEX       | 0 <-- fp of caller         |       |
//      |    |        local 1       | 1                       fun->      |
//  E   |    :         ...          :                      locals_size_  |
//      |    |        local N       |                            |       |
//  R   |    +----------------------+                          --+--  fun->
//      |    |        param 1       |                               frame_size_
//      |    :         ...          :                                    |
//      |    |        param M       |                                    |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                                  --+--
//      |    |                      |   <-- sp of $main
//      |    :                      :
//      |    :   expression stack   :
//      |    :                      :
//  $   |    |                      |
//      |    +----------------------+
//  M   | +--|     dynamic link     |   <-- fp->stack()
//      | +--|      static link     |       (expr stack base of $main)
//  A   | |  |  return addr == 0x0  |
//      | |  +----------------------+
//  I   +--->|       NO_INDEX       | 0 <-- fp of $main
//        |  |        local 1       | 1
//  N     |  :         ...          :
//        |  |        local N       |
//        |  +----------------------+
//        |  |        input         |
//        |  |       input_key      |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//        |  | dynamic link == 0x0  |
//        |  |  static link == 0x0  |
//        |  |  return addr == 0x0  |
//        |  +----------------------+
//        +->|       NO_INDEX       | 0 <-- fp of init
//           | output        temp   | 1
//           |               smi(0) | 2
//           | stdout        temp   | 3
//   I       |               smi(1) | 4
//           | stderr        temp   | 5
//   N       |               smi(2) | 6
//           |_undef_cnt     temp   | 7
//   I       |               smi(3) | 8
//           |_undef_details temp   | 9
//   T       |               smi(4) | 10
//           :                      :
//           :     user-defined     :
//           :   static variables   :
//           :  (including tables)  :
//           :                      :
//           +----------------------+
//     HIGH  |  not part of stack   |   <- proc->initial_sp() (initial pointer)
//    +++++++++++++++++++++++++++++++
//                  bottom
//
// Frames for calls to static functions issued as part of static initialization
// are not shown as they are pushed and popped before the call to $main.
//

class Frame {
 public:
  // Creation
  Frame* Initialize(Frame* dynamic_link, Frame* static_link, Instr* return_pc) {
    // 'this' points immediately after the frame data on the stack => this[-1]
    this[-1].dynamic_link_ = dynamic_link;
    this[-1].static_link_ = static_link;
    this[-1].return_pc_ = return_pc;
    return this;
  }

  // Caller info - see header comment for details
  Frame* dynamic_link() const  { return this[-1].dynamic_link_; }
  Frame* static_link() const  { return this[-1].static_link_; }
  Instr* return_pc() const  { return this[-1].return_pc_; }

  // Variable access
  Val*& at(int i) { assert(i >= 0); return reinterpret_cast<Val**>(this)[i]; }
  IntVal*& int_at(int i)  { return reinterpret_cast<IntVal*&>(at(i)); }
  FloatVal*& float_at(int i)  { return reinterpret_cast<FloatVal*&>(at(i)); }
  ArrayVal*& array_at(int i)  { return reinterpret_cast<ArrayVal*&>(at(i)); }
  MapVal*& map_at(int i)  { return reinterpret_cast<MapVal*&>(at(i)); }
  TupleVal*& tuple_at(int i)  { return reinterpret_cast<TupleVal*&>(at(i)); }

  // Expression stack
  Val** stack()  { return reinterpret_cast<Val**>(this - 1); }

  // The offset of the first static and local variable relative to bp
  // offset 0 reserved for NO_INDEX (see opcode.h) => start at sizeof(void*)
  // Subsequent variables are allocated at higher addresses.
  enum {
    kStaticStartOffset = sizeof(void*),
    kLocalStartOffset = sizeof(void*),
  };

 private:
  // caller info - see header comment for details
  Frame* dynamic_link_;
  Frame* static_link_;
  Instr* return_pc_;
};


// An NFrame describes a native Sawzall activation frame at run-time
// on the native stack. It provides accessors to the frame-specific information
// about the caller, and to its local variables.
//
// The local variables of the init frame (aka static variables) are not
// allocated on the native stack, but on the interpreter stack instead, so that
// intrinsics can access statics identically both in interpreted and native
// mode. The init frame is popped before the call to $main (see Proc::Execute()
// in proc.cc), but the static variables are still accessed via $main's static
// link pointing to init on the interpreter stack.
//
// =============================================================================
// Native stack layout in 32-bit mode (grows from high to low addresses)
// =============================================================================
//
// caller saved regs: registers used for expression evaluation; saved by the
//                    calling function before calls to other functions that
//                    might overwrite their contents
// callee saved regs: registers used to allocate local variables; saved by the
//                    called function before using them and restored before
//                    returning
// padding          : added as necessary to always keep the stack aligned
// header size      : combined size of the caller-saved registers with
//                    non-Sawzall values and padding in the call area - used by
//                    the trap handler to only decrement the ref counts of valid
//                    Sawzall values while skipping the non-Sawzall ones;
//                    when caller-saved registers are pushed, Sawzall values
//                    are processed first, so that non-Sawzall values and
//                    padding create a continuous block described by header size
// dynamic link     : pointer to the base of the frame of calling function
// static link      : pointer to the base of the frame of enclosing scope,
//                    i.e. the function that textually encloses this one
//                    (the calling or the calling of the calling etc function)
// return address   : address of the instruction in the calling function where
//                    to continue execution after this function returns (pushed
//                    by the call instruction)
//
//
//                    top
//    +++++++++++++++++++++++++++++++                         A
//      LOW  :                      :                        A|A
//           :                      :                         |
//           :                      :                         |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//           :                      :
//   C       :                      :
//           :                      :
//   A       :                      :
//           +----------------------+                       --+--
//   L       |    callee saved regs |   <-- esp of callee     |
//           |    ...padding...     |                         |
//   L       |       local N        |                         |
//           :        ...           :      frame size after prologue % 16 == 0
//   E       |       local 1        |                         |
//           +----------------------+                         |
//   E  +----|     dynamic link     | 0 <-- ebp of callee     |
//      |    |    return address    | 1                       |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                       --+--
//      |    |      static link     | 2                       |
//      |    |       proc ptr       | 3                       |
//   C  |    |       param 1        | 4                       |
//      |    :         ...          :                         |
//   A  |    |       param M        |              call area size % 16 == 0
//      |    |      header size     |                         |
//   L  |    :     ...padding...    :                         |
//      |    |   caller saved regs  |                         |
//   L  |    +----------------------+                       --+--
//      |    |   callee saved regs  |   <-- esp of caller
//   E  |    :    ...padding...     :
//      |    |       local N        |
//   R  |    :         ...          :
//      |    |       local 1        |
//      +--->+----------------------+
//      +----|     dynamic link     | 0 <-- ebp of caller
//      |    |    return address    | 1
//    +-|-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      |    |      static link     | 2
//      |    |       proc ptr       | 3
//      |    |       param 1        | 4
//   $  |    :         ...          :
//      |    |       param M        |
//   M  |    |      header size     |
//      |    |     ...padding...    |
//   A  |    |    caller saved regs |
//      |    +----------------------+
//   I  |    |    callee saved regs |   <-- esp of $main
//      |    :    ...padding...     :       (proc->native_.bottom_sp_)
//   N  |    |       local N        |
//      |    :         ...          :
//      |    |       local 1        |                   handled by nativecodegen
//      +--->+----------------------+                            A
//      +----|     dynamic link     | 0 <-- ebp of $main         |
// - - -|- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//      |    |    return address    | 1                          |
//    +-|-+-+-+-+-+-+-+-+-+-+-+-+-+-+                            V
//  +--------|     static link      | 2                 handled by C++ compiler
//  |   |    |       proc ptr       | 3
//  |   |    |        input         | 4
//  |   |    |      input_key       | 5
//  |   |    :    ...padding...     :
//  |   |    :                      :
//  |   +--->:    Proc::Execute()   :
//  |        :        frame         :
//  |        :                      :
//  | +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |        :                      :
//  |        :      C++ frames      :
//  |   HIGH :                      :
//  | +++++++++++++++++++++++++++++++
//  |                 bottom
//  |
//  |
//  +---> to INIT frame on the interpreter stack above
//
// Init frame and frames for calls to static functions issued as part of static
// initialization are not shown as they are pushed and popped before the call
// to $main. The layout of the init frame is similar to that of $main, but
// without input and input_key parameters and with locals stored on the
// interpreter stack.
//
// A new call area is reserved on the stack by the caller for each call.
// Call areas can overlap in case of nested function calls. For example, when
// calling f(g(x, y), z), the following sequence occurs:
//  - reserve call area for calling f
//  - push z
//  - reserve call area for calling g
//  - push y
//  - push x
//  - call g
//  - release call area for calling g
//  - push g's result
//  - call f
//  - release call area for calling f
//
// Here is the state of the stack before and after calling g().
//
//
//                     BEFORE call to g()     AFTER call to g()
//           --+--   +-------------------+    BEFORE call to f()         --+--
//             |     |    static link    |                                 |
//             |     |     proc ptr      |                                 |
//         call area |         x         |                                 |
//   --+--  for g()  |         Y         |  +-------------------+  --+--   |
//     |       |     |    header size    |  |     static link   |    |     |
//     |       |     |   ...padding...   |  |      proc ptr     |    |     |
// call area   |     | caller saved regs |  |       g(x,y)      | size % 16 == 0
//  for f()  --+--   |         z         |  |         z         |    |     |
//     |             |    header size    |  |    header size    |    |     |
//     |             |   ...padding...   |  |   ...padding...   |    |     |
//     |             | caller saved regs |  | caller saved regs |    |     |
//   --+--           +-------------------+  +-------------------+  --+-- --+--
//
//
// In this example if x were undefined, the trap handler would start unwinding
// the stack before g's non-Sawzall arguments (static link and proc ptr) were
// pushed. It would decrement the ref count of y, then read the header size to
// know how much space is occupied by padding and other non-Sawzall values that
// should be skipped. If any of the Sawzall values were pushed as part of the
// caller saved registers, their ref counts would get decremented as well by the
// trap handler. Then, the ref count of z would get decremented, and so on.
// See Proc::HandleTrap in proc.cc.
//
// The non-Sawzall arguments of support functions must therefore precede Sawzall
// arguments in the parameter list for this scheme to work correctly.
// See header comment in nativesupport.h for more details.
//
// =============================================================================
// Native stack layout in 64-bit mode (grows from high to low addresses)
// =============================================================================
//
// There are two difference comparing to the 32-bit layout
// - register names start with r instead of e (alghough in the code we use e)
// - the first 4 arguments are passed in registers, then spilled onto the
//   stack right away (see NCodeGen::Prologue() in nativecodegen.cc)
//
// See 32-bit section above for vocabulary and additional details.
//
//                    top
//    +++++++++++++++++++++++++++++++                         A
//      LOW  :                      :                        A|A
//           :                      :                         |
//           :                      :                         |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//           :                      :
//           :                      :
//           :                      :
//           :                      :
//   C       +----------------------+                       --+--
//           |    callee saved regs |   <-- rsp of callee     |
//   A       :    ...padding...     :                         |
//           |       local N        |                         |
//   L       :        ...           :      frame size after prologue % 16 == 0
//           |       local 1        |                         |
//   L       |       param 4 (r9)   |                         |
//           |       param 3 (r8)   |                         |
//   E       |       param 2 (rcx)  |                         |
//           |       param 1 (rdx)  |                         |
//   E       |      proc ptr (rsi)  |                         |
//           |    static link (rdi) |                         |
//           +----------------------+                         |
//      +----|     dynamic link     | 0 <-- rbp of callee     |
//      |    |    return address    | 1                       |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                       --+--
//      |    |       param 5        | 2                       |
//      |    :         ...          :                         |
//      |    |       param M        |              call area size % 16 == 0
//   C  |    |      header size     |                         |
//      |    :     ...padding...    :                         |
//   A  |    |   caller saved regs  |                         |
//      |    +----------------------+                       --+--
//   L  |    |   callee saved regs  |   <-- rsp of caller
//      |    :    ...padding...     :
//   L  |    |       local N        |
//      |    :         ...          :
//   E  |    |       local 1        |
//      |    |       param 4 (r9)   |
//   R  |    |       param 3 (r8)   |
//      |    |       param 2 (rcx)  |
//      |    |       param 1 (rdx)  |
//      |    |      proc ptr (rsi)  |
//      |    |    static link (rdi) |
//      +--->+----------------------+
//      +----|     dynamic link     | 0 <-- rbp of caller
//      |    |    return address    | 1
//    +-|-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      |    |       param 5        | 2
//      |    :         ...          :
//      |    |       param M        |
//   $  |    |      header size     |
//      |    :    ...padding...     :
//   M  |    |    caller saved regs |
//      |    +----------------------+
//   A  |    |    callee saved regs |   <-- rsp of $main
//      |    :    ...padding...     :   (proc->native_.bottom_sp_)
//   I  |    |       local N        |
//      |    :         ...          :
//   N  |    |       local 1        |
//      |    |     input_key (rcx)  |
//      |    |      input (rdx)     |
//      |    |      proc ptr (rsi)  |
//  +---|----|    static link (rdi) |                   handled by nativecodegen
//  |   +--->+----------------------+                            A
//  |   +----|     dynamic link     | 0 <-- rbp of $main         |
// -|- -|- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//  |   |    |    return address    | 1                          |
//  | +-|-+-+-+-+-+-+-+-+-+-+-+-+-+-+                            V
//  |   |    :    ...padding...     :                   handled by C++ compiler
//  |   |    :                      :
//  |   +--->:    Proc::Execute()   :
//  |        :        frame         :
//  |        :                      :
//  | +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |        :                      :
//  |        :      C++ frames      :
//  |   HIGH :                      :
//  | +++++++++++++++++++++++++++++++
//  |                 bottom
//  |
//  |
//  +---> to INIT frame on the interpreter stack above
//
// Init frame and frames for calls to static functions issued as part of static
// initialization are not shown as they are pushed and popped before the call
// to $main. The layout of the init frame is similar to that of $main, but
// without input and input_key parameters and with locals stored on the
// interpreter stack.
//


class NFrame {
 public:
  // The size of the activation frame of init cannot be obtained from
  // f->frame_size() as for regular functions, because init does not have a
  // representative function. However, it is constant and easy to calculate,
  // because the init frame does not contain any local variables; the statics
  // are allocated in the interpreter stack instead;
  // the frame contains all callee-saved registers (incl. rbp) and some padding
  // keeping the stack aligned (see NCodeGen::Prologue);
  // The frame size is defined as the distance between fp and sp at the time the
  // prologue of the function has been executed. The frame size modulo
  // kStackAlignment is always 2*kStackWidth, because the return address and
  // the dynamic link are not included in this size. The distance between the sp
  // of the caller (when the return address and dynamic link are not pushed yet)
  // and the sp of the callee must be aligned to kStackAlignment.
  // We should use a function f to represent init and f->frame_size() will
  // provide the correct value.

  enum {
    // Native stack frame size alignment in bytes
    kStackAlignment = 16,

#if defined(__i386__)
    // The size of Val pointers, registers, return addresses
    kStackWidth = 4,
    kStackWidthLog2 = 2,

    // Number of caller-saved registers
    kNumCallerSaved = 3,

    // Number of callee-saved registers
    kNumCalleeSaved = 3,

    // Maximum number of integer parameters passed in registers
    kMaxNumRegParams = 0,

    // Maximum number of Sawzall user parameters passed in registers
    kMaxNumRegSzlParams = 0,

    // Number of links in frame, i.e. return address, dynamic link
    kNumFrameLinks = 2,

    // Distance between esp and ebp in  frame of init
    kInitFrameSize = (kNumCalleeSaved + 1/*ebp*/ + 2/*padding*/)*kStackWidth,

    // Variable indices in frame, relative to ebp
    kDynamicLinkIdx = 0,
    kReturnAddrIdx = 1,
    kStaticLinkIdx = 2,
    kProcPtrIdx = 3,
    kParamStartIdx = 4,
    kLocalEndIdx = 0,

#elif defined(__x86_64__)
    // The size of Val pointers, registers, return addresses
    kStackWidth = 8,
    kStackWidthLog2 = 3,

    // Number of caller-saved registers
    kNumCallerSaved = 9,

    // Number of callee-saved registers
    kNumCalleeSaved = 5,

    // Maximum number of integer parameters passed in registers
    kMaxNumRegParams = 6,

    // Maximum number of Sawzall user parameters passed in registers (excl. sl and proc)
    kMaxNumRegSzlParams = 4,

    // Number of links in frame, i.e. return address, dl, sl, proc ptr
    kNumFrameLinks = 4,

    // Distance between rsp and rbp in  frame of init
    kInitFrameSize = (1/*proc*/ + 1/*sl*/ + kNumCalleeSaved + 1/*rbp*/ + 0/*padding*/)*kStackWidth,

    // Variable indices in frame, relative to rbp
    kDynamicLinkIdx = 0,
    kReturnAddrIdx = 1,
    kStaticLinkIdx = -1,
    kProcPtrIdx = -2,
    kParamStartIdx = 2,
    kLocalEndIdx = kProcPtrIdx,

#else
  #error "Unrecognized target machine"
#endif

    // Maximum size of call area header (upper bound heuristic) consists of all
    // caller-saved registers and maximum padding size
    kMaxCallAreaHeaderSize = kNumCallerSaved*kStackWidth + kStackAlignment - kStackWidth,
  };

  // Caller info
  NFrame* dynamic_link() { return reinterpret_cast<NFrame*>(at(kDynamicLinkIdx)); }
  Instr* return_pc() { return reinterpret_cast<Instr*>(at(kReturnAddrIdx)); }
  NFrame* static_link() { return reinterpret_cast<NFrame*>(at(kStaticLinkIdx)); }
  Proc* proc_ptr() { return reinterpret_cast<Proc*>(at(kProcPtrIdx)); }

  // Variable access, index i can be positive (parameters) or negative (locals)
  Val*& at(int i) { return reinterpret_cast<Val**>(this)[i]; }
  IntVal*& int_at(int i)  { return reinterpret_cast<IntVal*&>(at(i)); }
  FloatVal*& float_at(int i)  { return reinterpret_cast<FloatVal*&>(at(i)); }
  ArrayVal*& array_at(int i)  { return reinterpret_cast<ArrayVal*&>(at(i)); }
  MapVal*& map_at(int i)  { return reinterpret_cast<MapVal*&>(at(i)); }
  TupleVal*& tuple_at(int i)  { return reinterpret_cast<TupleVal*&>(at(i)); }
  Outputter*& output_at(int i)  { return reinterpret_cast<Outputter*&>(at(i)); }

  static inline NFrame* base(NFrame* fp, int delta) {
    assert(delta >= 0);
    while (delta-- > 0) fp = fp->static_link();
    return fp;
  }
};


// A FrameIterator is used to iterate over Sawzall activation
// frames. It is created with parameters describing the start
// frame (usually the top-most frame). Using Unwind() one
// can iterate over all frames from top to bottom.
// A FrameIterator handles either interpreted frames or native frames.

class FrameIterator {
 public:
  // Creation
  FrameIterator(Proc* proc, Frame* fp, NFrame* nfp, Val** sp, Instr* pc);

  Proc* proc() const  { return proc_; }
  Frame* fp() const  { return fp_; }
  NFrame* nfp() const  { return nfp_; }
  Val** sp() const  { return sp_; }
  Instr* pc() const  { return pc_; }
  bool is_valid() const;

  // Unwind one stack frame: advances to the caller frame
  void Unwind();

  // The function corresponding to this frame
  // (this requires a search => cache the result if needed repeatedly)
  Function* function() const;

  // Print var value for the current frame via f.
  void PrintVar(Fmt::State* f, VarDecl* var) const;

  // Print the current frame via f.
  // frame_id is printed with the frame output.
  void PrintFrame(Fmt::State* f, int frame_id) const;

  // Print a stack trace via f. nframes determines how
  // many frames are printed. If there are more then nframes, the
  // frames in the middle are skipped (and replaced by '...').
  static void PrintStack(Fmt::State* f, int nframes, Proc* proc, Frame* fp, NFrame* nfp, Val** sp, Instr* pc);

  // Convenience wrappers. Same as PrintStack above,
  // but printing via file descriptor fd.
  static void PrintStack(int fd, int nframes, Proc* proc, Frame* fp, Val** sp, Instr* pc);
  static void PrintStack(int fd, int nframes, Proc* proc, NFrame* fp, Val** sp, Instr* pc);

 private:
  Proc* proc_;
  Frame* fp_;  // frame in interpreter's stack
  NFrame* nfp_;  // frame in native stack
  Val** sp_;
  Instr* pc_;
  bool native_;

  // Print linkage info for the current frame via f (debugging).
  void PrintLinkage(Fmt::State* f ) const;
};


}  // namespace sawzall
