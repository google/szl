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

class VarDecl;  // for tracking undefined variables

// x86 addressing modes
// the base register number is always the last four bits of the enum value
// the enum order is therefore relevant
enum AddrMod {
  AM_NONE = -1,  // not a valid operand

  // Note that AM_EAX should actually be named AM_rAX (lower case r), since rAX
  // is the AMD notation for referring to multiple register widths, e.g. 16-bit
  // AX, 32-bit EAX, or 64-bit RAX register; however, for historical reasons, it
  // is easier to read EAX, EBP, and ESP, rather than rAX, rBP, rSP
  AM_EAX = 0,  // AL, AX, EAX, RAX
  AM_ECX = 1,  // CL, CX, ECX, RCX
  AM_EDX = 2,  // DL, DX, EDX, RDX
  AM_EBX = 3,  // BL, BX, EBX, RBX
  AM_ESP = 4,  // SPL, SP, ESP, RSP
  AM_EBP = 5,  // BPL, BP, EBP, RBP
  AM_ESI = 6,  // SIL, SI, ESI, RSI
  AM_EDI = 7,  // DIL, DI, EDI, RDI

#if defined(__i386__)
  AM_LAST_REG = AM_EDI,
#endif

  AM_R8  = 8,  // R8L, R8W, R8D, R8
  AM_R9  = 9,
  AM_R10 = 10,
  AM_R11 = 11,
  AM_R12 = 12,
  AM_R13 = 13,
  AM_R14 = 14,
  AM_R15 = 15,

#if defined(__x86_64__)
  AM_LAST_REG = AM_R15,
#endif

  AM_INDIR = 16,              // [reg]
  AM_BASED = AM_INDIR + 16,   // [reg + disp]
  AM_INXD = AM_BASED + 16,    // [reg*2^scale + disp]
  AM_BINXD = AM_INXD + 16,    // [reg1 + reg2*2^scale + disp]
  AM_ABS = AM_BINXD + 16*16,  // [disp]
  AM_IMM,                     // immediate
  AM_FST,                     // on floating point stack
  AM_CC,                      // in condition code

  AM_LAST = AM_CC,
};


static inline bool IsIntReg(AddrMod am) { return am >= AM_EAX && am <= AM_LAST_REG; }


#if defined(__i386__)

static inline bool IsByteReg(AddrMod am) { return am >= AM_EAX && am <= AM_EBX; }
static inline bool IsCallerSaved(AddrMod am) { return am >= AM_EAX && am <= AM_EDX; }

#elif defined(__x86_64__)

static inline bool IsByteReg(AddrMod am) {
  return am >= AM_EAX && am <= AM_R15 && am != AM_EBP && am != AM_ESP;
}
static inline bool IsCallerSaved(AddrMod am) {
  return am >= AM_EAX && am <= AM_R11 && am != AM_EBX && am != AM_EBP && am != AM_ESP;
}

#endif

static inline bool IsIndir(AddrMod am) { return AM_INDIR <= am && am < AM_BASED; }
static inline bool IsBased(AddrMod am) { return AM_BASED <= am && am < AM_INXD; }
static inline bool IsIndexed(AddrMod am) { return AM_INXD <= am && am < AM_BINXD; }
static inline bool IsBasedOrIndexed(AddrMod am) { return AM_BASED <= am && am < AM_BINXD; }
static inline bool IsBasedIndexed(AddrMod am) { return AM_BINXD <= am && am < AM_ABS; }
static inline bool IsRelMem(AddrMod am) { return AM_BASED <= am && am < AM_ABS; }
static inline bool IsMem(AddrMod am) { return AM_INDIR <= am && am < AM_IMM; }
static inline bool HasBase(AddrMod am) { return AM_INDIR <= am && am < AM_ABS && !IsIndexed(am); }
static inline bool HasIndex(AddrMod am) { return AM_INXD <= am && am < AM_ABS; }
static inline AddrMod BaseReg(AddrMod am) { assert(HasBase(am)); return static_cast<AddrMod>(am & 0x0f); }

static inline AddrMod Reg1(AddrMod am) {
  if (AM_EAX <= am && am < AM_ABS)
    return static_cast<AddrMod>(am & 0x0f);
  else
    return AM_NONE;
}

static inline AddrMod Reg2(AddrMod am) {
  if (AM_BINXD <= am && am < AM_ABS)
    return static_cast<AddrMod>(((am - AM_BINXD) >> 4) & 0x0f);
  else
    return AM_NONE;
}

static inline bool IsByteRange(int64 l) { return static_cast<int8>(l) == l; }
static inline bool IsDWordRange(int64 l) { return static_cast<int32>(l) == l; }


enum CondCode {
  CC_NONE = -1,

  CC_O  = 0x00,
  CC_NO = 0x01,
  CC_B  = 0x02,
  CC_AE = 0x03,
  CC_E  = 0x04,
  CC_NE = 0x05,
  CC_BE = 0x06,
  CC_A  = 0x07,
  CC_S  = 0x08,
  CC_NS = 0x09,
  CC_PE = 0x0A,
  CC_PO = 0x0B,
  CC_L  = 0x0C,
  CC_GE = 0x0D,
  CC_LE = 0x0E,
  CC_G  = 0x0F,

  CC_FALSE,
  CC_TRUE
};


// Condition code mapping for comparing swapped operands
CondCode SwapCC(CondCode cc);

// Condition code mapping for negated comparison of operands
CondCode NegateCC(CondCode cc);

// Condition code mapping for comparing unsigned operands
CondCode XsignCC(CondCode cc);

// Condition code mapping for comparing higher part of long operands
CondCode HighCC(CondCode cc);


// Descriptor of the operand of an x86 instruction
struct Operand {
  AddrMod am;         // addressing mode
  int scale;          // 0, 1, 2, 3 (HasIndex(am) == true)
  int size;           // in bytes
  CondCode cc;        // condition code test leading true (am == AM_CC)
  sword_t offset;     // offset in bytes
  sword_t value;      // immediate value (am == AM_IMM)
  int flags;          // user-defined flags, not used by assembler
  VarDecl* var;       // user-defined data, for tracking undefined vars

  // convenience constructors for frequently used addressing modes
  Operand(int addr_mod, int sz, sword_t off) {
    Clear();
    am = static_cast<AddrMod>(addr_mod);
    size = sz;
    offset = off;
    assert(IsIntReg(am) || IsMem(am));
  }

  Operand(int addr_mod_reg1, int reg2, int sz, sword_t off, int sc) {
    Clear();
    am = static_cast<AddrMod>(addr_mod_reg1 + (reg2 << 4));
    size = sz;
    offset = off;
    scale = sc;
    assert(0 <= scale && scale <= 3);
    assert(IsRelMem(am));
  }

  Operand(int addr_mod, int x) {
    Clear();
    am = static_cast<AddrMod>(addr_mod);
    if (am == AM_CC)
      cc = static_cast<CondCode>(x);
    else if (am == AM_IMM)
      value = x;
    else
      assert(false);
  }

  Operand(int addr_mod, const void* ptr) {
    Clear();
    am = static_cast<AddrMod>(addr_mod);
    value = reinterpret_cast<sword_t>(ptr);
    assert(am == AM_IMM);
  }

  Operand(void (*fun)()) {
    Clear();
    am = AM_IMM;
    // new-style casts not allowed between function pointers and objects
    value = (sword_t)fun;
  }

  Operand(int addr_mod) {
    Clear();
    am = static_cast<AddrMod>(addr_mod);
    assert(IsIntReg(am));
  }

  Operand() {
    Clear();
  }

  void Clear() {
    am = AM_NONE;
    scale = 0;
    size = sizeof(intptr_t);
    cc = CC_NONE;
    offset = 0;
    value = 0;
    flags = 0;
    var = NULL;
  }
};


// Set of registers
typedef uint32 RegSet;


// Registers as elements of RegSet
enum {
  RS_EAX = 1 << AM_EAX,
  RS_ECX = 1 << AM_ECX,
  RS_EDX = 1 << AM_EDX,
  RS_EBX = 1 << AM_EBX,

  RS_ESP = 1 << AM_ESP,
  RS_EBP = 1 << AM_EBP,
  RS_ESI = 1 << AM_ESI,
  RS_EDI = 1 << AM_EDI,

  RS_R8  = 1 << AM_R8,
  RS_R9  = 1 << AM_R9,
  RS_R10 = 1 << AM_R10,
  RS_R11 = 1 << AM_R11,
  RS_R12 = 1 << AM_R12,
  RS_R13 = 1 << AM_R13,
  RS_R14 = 1 << AM_R14,
  RS_R15 = 1 << AM_R15,

  // we don't include ebp or esp in these sets, since we never use them
  // as general purpose registers

#if defined(__i386__)

  RS_LAST_REG = RS_EDI,
  RS_BYTE = RS_EAX | RS_ECX | RS_EDX | RS_EBX,
  RS_ANY = RS_BYTE | RS_ESI | RS_EDI,
  RS_CALLEE_SAVED = RS_EBX | RS_ESI | RS_EDI,
  RS_CALLER_SAVED = RS_EAX | RS_ECX | RS_EDX,
  RS_TMP = RS_CALLER_SAVED,
  RS_ALL = RS_ANY,

#elif defined(__x86_64__)

  RS_LAST_REG = RS_R15,
  RS_BYTE = RS_EAX | RS_ECX | RS_EDX | RS_EBX | RS_ESI | RS_EDI |
            RS_R8 | RS_R9 | RS_R10 | RS_R12 | RS_R13 | RS_R14 | RS_R15,
  RS_ANY = RS_BYTE,
  RS_CALLEE_SAVED = RS_EBX | RS_R12 | RS_R13 | RS_R14 | RS_R15,
  RS_CALLER_SAVED = RS_EAX | RS_ECX | RS_EDX | RS_ESI | RS_EDI |
                    RS_R8 | RS_R9 | RS_R10 | RS_R11,
  RS_TMP = RS_R11,  // temp register used by assembler, use with caution
  RS_ALL = RS_ANY | RS_TMP,

#endif

  RS_EMPTY = 0,
};


// Returns the register set used by the given addressing mode
static inline RegSet Regs(AddrMod am) {
  RegSet regs = RS_EMPTY;
  AddrMod reg1 = Reg1(am);
  if (reg1 != AM_NONE) {
    regs = static_cast<RegSet>(1 << reg1);
    AddrMod reg2 = Reg2(am);
    if (reg2 != AM_NONE)
      regs += static_cast<RegSet>(1 << reg2);
  }
  return regs;
}


// Returns the number of registers in the given register set
static inline int NumRegs(RegSet rs) {
  int n = 0;
  while (rs != RS_EMPTY) {
    rs &= rs - 1;
    n++;
  }
  return n;
}


// Returns the first register of the given register set
static inline AddrMod FirstReg(RegSet rs) {
  if (rs == RS_EMPTY)
    return AM_NONE;

  AddrMod reg = AM_EAX;
  RegSet reg_as_set = RS_EAX;
  while ((reg_as_set & rs) == RS_EMPTY) {
    reg = static_cast<AddrMod>(static_cast<int>(reg) + 1);
    reg_as_set <<= 1;
  }
  return reg;
}


// ----------------------------------------------------------------------------
// The x86 instruction assembler
// Generates 64-bit code if __x86_64__ is defined or 32-bit code otherwise

class Assembler {
 public:
  explicit Assembler();
  virtual ~Assembler();

 private:
  // code buffer
  // invariant: code_buffer_ <= emit_pos_ <= code_limit_
  Instr* code_buffer_;  // the code buffer currently used
  Instr* code_limit_;  // the code buffer limit
  Instr* emit_pos_;  // the position for the next emit
  bool dead_code_;  // if set, code emission is disabled
  int esp_offset_;  // esp offset relative to ebp after prologue

 public:
  // Accessors
  Instr* code_buffer() const  { return code_buffer_; }
  unsigned emit_offset() const  { return emit_pos_ - code_buffer_; }
  void align_emit_offset();
  bool emit_ok();
  void set_dead_code(bool dead_code) { dead_code_ = dead_code; }

  // Native stack offset
  void set_esp_offset(int offset) { esp_offset_ = offset; }
  void adjust_esp_offset(int delta) { set_esp_offset(esp_offset_ + delta); }
  int esp_offset() const { return esp_offset_; }

 private:
  // Code emission
  void MakeSpace();
  void emit_int8(int8 x);
  void emit_int16(int16 x);
  void emit_int32(int32 x);

  // x86 low-level code generation

  // Emit portions (bytes) of x86 instructions
  int  EmitPrefixes(AddrMod reg3, AddrMod am, int size);
  void EmitByte(int b);
  void Emit2Bytes(int b1, int b2);
  void Emit3Bytes(int b1, int b2, int b3);
  void EmitWord(int w);
  void EmitDWord(int d);
  void EmitByteDWord(int b1, int d2);
  void Emit2BytesDWord(int b1, int b2, int d3);
  void EmitEA(int reg_op, const Operand* n);
  void EmitIndirEA(int b1, int b2, AddrMod base_reg, int off);
  void EmitImmVal(int32 val, int size);

  // Assemble complete x86 instructions
  void OpSizeEA(int b1, int b2, const Operand* n);
  void OpSizeReg(int b1, int b2, AddrMod reg, int size);
  void OpImm(int b1, int b2, const Operand* n, int32 val);
  void OpImmReg(int b1, int b2, AddrMod reg, int32 val, int size);
  void OpRegReg(int op, AddrMod reg1, AddrMod reg2);
  void OpSizeRegReg(int op, AddrMod reg1, AddrMod reg2, int size);
  void OpSizeRegEA(int op, AddrMod reg, const Operand* n);
  void IncReg(AddrMod reg, int size);
  void DecReg(AddrMod reg, int size);

  // patches byte b at offset in code buffer
  void PatchByte(unsigned offset, int b);
  // patches dword d at offset in code buffer
  void PatchDWord(unsigned offset, int d);

 public:
  // x86 code generation
  // Feel free to add more public functions assembling instructions
  // in a safe manner using private low-level functions
  // Note that all functions taking a register (AddrMod reg) as parameter treat
  // this register as wide as an address (32 or 64 bit), unless the function
  // also takes either a size parameter or an operand parameter.
  void MoveRegReg(AddrMod dst_reg, AddrMod src_reg);
  void AddImmReg(AddrMod dst_reg, int32 val);
  void AddRegEA(AddrMod dst_reg, const Operand* n);
  void SubRegEA(AddrMod dst_reg, const Operand* n);
  void SubImmRegSetCC(AddrMod dst_reg, int32 val, int size);
  void AndRegEA(AddrMod dst_reg, const Operand* n);
  void OrRegEA(AddrMod dst_reg, const Operand* n);
  void Exg(AddrMod am1, AddrMod am2);
  void CmpRegEA(AddrMod reg, const Operand* r);
  void TestReg(const Operand* n, AddrMod reg);
  void TestImm(const Operand* n, int32 val);
  void ShiftRegLeft(AddrMod reg, int power);
  void ShiftRegRight(AddrMod reg, int power, int size, bool signed_flag);
  void Load(AddrMod dst_reg, const Operand* s);
  void LoadEA(AddrMod dst_reg, const Operand* s);
  void Store(const Operand* d, AddrMod src_reg);
  void Push(const Operand* n);
  void PushReg(AddrMod reg);
  void PopReg(AddrMod reg);
  void PushRegs(RegSet regs);
  void PopRegs(RegSet regs);
  void FLoad(const Operand* n);
  void FStore(const Operand* n, int pop_flag);
  void FAdd(const Operand* n);
  void FSub(const Operand* n);
  void FSubR(const Operand* n);
  void FMul(const Operand* n);
  void FDiv(const Operand* n);
  void FDivR(const Operand* n);
  void Inc(const Operand* n);
  void Dec(const Operand* n);
  void Leave();
  void Ret();
  void Int3();

  // code offset value returned when in dead code
  enum { kDeadCodeOffset = -1 };

  // Jumps and calls
  void JmpIndir(const Operand* n);
  void CallIndir(const Operand* n);
  int JmpRel8(int8 rel8);  // returns offset of rel8 in code buffer
  int JmpRel32(int32 rel32);  // returns offset of rel32 in code buffer
  int JccRel8(CondCode cc, int8 rel8);  // returns offset of rel8 in code buffer
  int JccRel32(CondCode cc, int32 rel32);  // returns offset of rel32 in code buffer
  int CallRel32(int32 rel32);  // returns offset of rel32 in code buffer
  void PatchRel8(int offset, int8 rel8);  // patches rel8 at offset in code buffer
  void PatchRel32(int offset, int32 rel32);  // patches rel32 at offset in code buffer

  // Adjust esp with immediate patchable byte value
  int AddImm8Esp(int imm8);  // returns offset of imm8 in code buffer
  void PatchImm8(int offset, int imm8);  // patches imm8 at offset in code buffer

  // Patch the code emitted at offset in the code buffer by EmitPushRegs(pushed)
  // to only push 'subset' regs instead of 'pushed' regs
  void PatchPushRegs(int offset, RegSet pushed, RegSet subset);
};

}  // namespace sawzall
