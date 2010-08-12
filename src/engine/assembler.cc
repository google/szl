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

#include "engine/globals.h"

#include "public/commandlineflags.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/code.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/assembler.h"


// from codegen.cc:
DECLARE_bool(eliminate_dead_code);


namespace sawzall {

// ----------------------------------------------------------------------------
// Implementation of Assembler


// Emit 32-bit or 64-bit code
#if defined(__i386__)

  const bool kEmit64 = false;

  static inline int high32(sword_t word) {
    ShouldNotReachHere();
    return 0;
  }

#elif defined(__x86_64__)

  const bool kEmit64 = true;

  static inline int high32(sword_t word) {
    return word >> 32;
  }

#else
  #error "Unrecognized target machine"
#endif


// Maps register addressing mode to register encoding
static const int8 reg_encoding[AM_R15 + 1] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
};


// Maps register addressing mode to register encoding shifted left three bits
static const int8 reg3_encoding[AM_R15 + 1] = {
  0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38,
  0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38,
};


// Addressing mode encoding table containing part of the ModR/M byte
// bits: mod:2 reg:3 r/m:3, reg is not encoded by this table
// 0xFF means illegal addressing mode
// see Intel Vol. 2A, 2.2.1.2, p 2-13
// see AMD Vol. 3, 1.2.7, p 19
static const int8 mod_rm[AM_LAST + 1] = {

// EAX   ECX   EDX   EBX   ESP   EBP   ESI   EDI
//  R8    R9   R10   R11   R12   R13   R14   R15

  0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,  // EAX..EDI
  0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,  // R8..R15

  0x00, 0x01, 0x02, 0x03, 0x04, 0xFF, 0x06, 0x07,  // INDIR: [EAX..EDI]
  0x00, 0x01, 0x02, 0x03, 0x04, 0xFF, 0x06, 0x07,  // INDIR: [R8..R15]

  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,  // BASED: [EAX..EDI + disp]
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,  // BASED: [R8..R15 + disp]

  0x04, 0x04, 0x04, 0x04, 0xFF, 0x04, 0x04, 0x04,  // INXD: [EAX..EDI*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // INXD: [R8..R15*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + EAX*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + EAX*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + ECX*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + ECX*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + EDX*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + EDX*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + EBX*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + EBX*2^scale + disp]

  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // BINXD: [EAX..EDI + ESP*2^scale + disp]
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // BINXD: [R8..R15 + ESP*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + EBP*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + EBP*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + ESI*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + ESI*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + EDI*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + EDI*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + R8*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + R8*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + R9*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + R9*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + R10*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + R10*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + R11*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + R11*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + R12*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + R12*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + R13*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + R13*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + R14*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + R14*2^scale + disp]

  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [EAX..EDI + R15*2^scale + disp]
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,  // BINXD: [R8..R15 + R15*2^scale + disp]

  0xFF, 0xFF, 0xFF, 0xFF                           // ABS, IMM, FST, CC
};


// Addressing mode encoding table containing the low 6 bits of the SIB byte
// bits: (scale:2) index:3 base:3
// 0x80 means no SIB byte
// 0xFF means illegal addressing mode
static const int8 sib[AM_LAST + 1] = {

// EAX   ECX   EDX   EBX   ESP   EBP   ESI   EDI
//  R8    R9   R10   R11   R12   R13   R14   R15

  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,  // EAX..EDI
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,  // R8..R15

  0x80, 0x80, 0x80, 0x80, 0x24, 0xFF, 0x80, 0x80,  // INDIR: [EAX..EDI]
  0x80, 0x80, 0x80, 0x80, 0x24, 0xFF, 0x80, 0x80,  // INDIR: [R8..R15]

  0x80, 0x80, 0x80, 0x80, 0x24, 0x80, 0x80, 0x80,  // BASED: [EAX..EDI + disp]
  0x80, 0x80, 0x80, 0x80, 0x24, 0x80, 0x80, 0x80,  // BASED: [R8..R15 + disp]

  0x05, 0x0D, 0x15, 0x1D, 0xFF, 0x2D, 0x35, 0x3D,  // INXD: [EAX..EDI*2^scale + disp]
  0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D,  // INXD: [R8..R15*2^scale + disp]

  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,  // BINXD: [EAX..EDI + EAX*2^scale + disp]
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,  // BINXD: [R8..R15 + EAX*2^scale + disp]

  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,  // BINXD: [EAX..EDI + ECX*2^scale + disp]
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,  // BINXD: [R8..R15 + ECX*2^scale + disp]

  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,  // BINXD: [EAX..EDI + EDX*2^scale + disp]
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,  // BINXD: [R8..R15 + EDX*2^scale + disp]

  0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,  // BINXD: [EAX..EDI + EBX*2^scale + disp]
  0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,  // BINXD: [R8..R15 + EBX*2^scale + disp]

  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // BINXD: [EAX..EDI + ESP*2^scale + disp]
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // BINXD: [R8..R15 + ESP*2^scale + disp]

  0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,  // BINXD: [EAX..EDI + EBP*2^scale + disp]
  0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,  // BINXD: [R8..R15 + EBP*2^scale + disp]

  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,  // BINXD: [EAX..EDI + ESI*2^scale + disp]
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,  // BINXD: [R8..R15 + ESI*2^scale + disp]

  0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,  // BINXD: [EAX..EDI + EDI*2^scale + disp]
  0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,  // BINXD: [R8..R15 + EDI*2^scale + disp]

  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,  // BINXD: [EAX..EDI + R8*2^scale + disp]
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,  // BINXD: [R8..R15 + R8*2^scale + disp]

  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,  // BINXD: [EAX..EDI + R9*2^scale + disp]
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,  // BINXD: [R8..R15 + R9*2^scale + disp]

  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,  // BINXD: [EAX..EDI + R10*2^scale + disp]
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,  // BINXD: [R8..R15 + R10*2^scale + disp]

  0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,  // BINXD: [EAX..EDI + R11*2^scale + disp]
  0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,  // BINXD: [R8..R15 + R11*2^scale + disp]

  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,  // BINXD: [EAX..EDI + R12*2^scale + disp]
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,  // BINXD: [R8..R15 + R12*2^scale + disp]

  0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,  // BINXD: [EAX..EDI + R13*2^scale + disp]
  0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,  // BINXD: [R8..R15 + R13*2^scale + disp]

  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,  // BINXD: [EAX..EDI + R14*2^scale + disp]
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,  // BINXD: [R8..R15 + R14*2^scale + disp]

  0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,  // BINXD: [EAX..EDI + R15*2^scale + disp]
  0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,  // BINXD: [R8..R15 + R15*2^scale + disp]

  0xFF, 0xFF, 0xFF, 0xFF                           // ABS, IMM, FST, CC
};


// Addressing mode encoding table containing the low 2 bits of the REX prefix
// 0xFF means illegal addressing mode
static const int8 rex_xb[AM_LAST + 1] = {

// EAX   ECX   EDX   EBX   ESP   EBP   ESI   EDI
//  R8    R9   R10   R11   R12   R13   R14   R15

  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // EAX..EDI
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // R8..R15

  0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00,  // INDIR: [EAX..EDI]
  0x01, 0x01, 0x01, 0x01, 0x01, 0xFF, 0x01, 0x01,  // INDIR: [R8..R15]

  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BASED: [EAX..EDI + disp]
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // BASED: [R8..R15 + disp]

  0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,  // INXD: [EAX..EDI*2^scale + disp]
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // INXD: [R8..R15*2^scale + disp]

  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BINXD: [EAX..EDI + EAX*2^scale + disp]
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // BINXD: [R8..R15 + EAX*2^scale + disp]

  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BINXD: [EAX..EDI + ECX*2^scale + disp]
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // BINXD: [R8..R15 + ECX*2^scale + disp]

  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BINXD: [EAX..EDI + EDX*2^scale + disp]
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // BINXD: [R8..R15 + EDX*2^scale + disp]

  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BINXD: [EAX..EDI + EBX*2^scale + disp]
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // BINXD: [R8..R15 + EBX*2^scale + disp]

  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // BINXD: [EAX..EDI + ESP*2^scale + disp]
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // BINXD: [R8..R15 + ESP*2^scale + disp]

  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BINXD: [EAX..EDI + EBP*2^scale + disp]
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // BINXD: [R8..R15 + EBP*2^scale + disp]

  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BINXD: [EAX..EDI + ESI*2^scale + disp]
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // BINXD: [R8..R15 + ESI*2^scale + disp]

  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BINXD: [EAX..EDI + EDI*2^scale + disp]
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,  // BINXD: [R8..R15 + EDI*2^scale + disp]

  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,  // BINXD: [EAX..EDI + R8*2^scale + disp]
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,  // BINXD: [R8..R15 + R8*2^scale + disp]

  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,  // BINXD: [EAX..EDI + R9*2^scale + disp]
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,  // BINXD: [R8..R15 + R9*2^scale + disp]

  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,  // BINXD: [EAX..EDI + R10*2^scale + disp]
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,  // BINXD: [R8..R15 + R10*2^scale + disp]

  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,  // BINXD: [EAX..EDI + R11*2^scale + disp]
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,  // BINXD: [R8..R15 + R11*2^scale + disp]

  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,  // BINXD: [EAX..EDI + R12*2^scale + disp]
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,  // BINXD: [R8..R15 + R12*2^scale + disp]

  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,  // BINXD: [EAX..EDI + R13*2^scale + disp]
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,  // BINXD: [R8..R15 + R13*2^scale + disp]

  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,  // BINXD: [EAX..EDI + R14*2^scale + disp]
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,  // BINXD: [R8..R15 + R14*2^scale + disp]

  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,  // BINXD: [EAX..EDI + R15*2^scale + disp]
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,  // BINXD: [R8..R15 + R15*2^scale + disp]

  0x00, 0x00, 0x00, 0x00                           // ABS, IMM, FST, CC
};


// Condition code mapping for comparing swapped operands
static const CondCode swapCC[] = {
  CC_O,       // CC_O
  CC_NO,      // CC_NO
  CC_A,       // CC_B
  CC_BE,      // CC_AE
  CC_E,       // CC_E
  CC_NE,      // CC_NE
  CC_AE,      // CC_BE
  CC_B,       // CC_A
  CC_S,       // CC_S
  CC_NS,      // CC_NS
  CC_PE,      // CC_PE
  CC_PO,      // CC_PO
  CC_G,       // CC_L
  CC_LE,      // CC_GE
  CC_GE,      // CC_LE
  CC_L,       // CC_G
  CC_FALSE,   // CC_FALSE
  CC_TRUE,    // CC_TRUE
};


CondCode SwapCC(CondCode cc) {
  assert(CC_O <= cc && cc <= CC_TRUE);
  return swapCC[cc];
}


// Condition code mapping for negated comparison of operands
CondCode NegateCC(CondCode cc) {
  assert(CC_O <= cc && cc <= CC_TRUE);
  return static_cast<CondCode>(cc ^ 1);  // no mapping table needed
}


// Condition code mapping for comparing unsigned operands
static const CondCode xsgnCC[] = {
  CC_O,       // CC_O
  CC_NO,      // CC_NO
  CC_L,       // CC_B
  CC_GE,      // CC_AE
  CC_E,       // CC_E
  CC_NE,      // CC_NE
  CC_LE,      // CC_BE
  CC_G,       // CC_A
  CC_S,       // CC_S
  CC_NS,      // CC_NS
  CC_PE,      // CC_PE
  CC_PO,      // CC_PO
  CC_B,       // CC_L
  CC_AE,      // CC_GE
  CC_BE,      // CC_LE
  CC_A,       // CC_G
  CC_FALSE,   // CC_FALSE
  CC_TRUE,    // CC_TRUE
};


CondCode XsignCC(CondCode cc) {
  assert(CC_O <= cc && cc <= CC_TRUE);
  return xsgnCC[cc];
}


// Condition code mapping for comparing higher part of long operands
static const CondCode highCC[] = {
  CC_O,       // CC_O
  CC_NO,      // CC_NO
  CC_B,       // CC_B
  CC_A,       // CC_AE
  CC_NE,      // CC_E
  CC_NE,      // CC_NE
  CC_B,       // CC_BE
  CC_A,       // CC_A
  CC_S,       // CC_S
  CC_NS,      // CC_NS
  CC_PE,      // CC_PE
  CC_PO,      // CC_PO
  CC_L,       // CC_L
  CC_G,       // CC_GE
  CC_L,       // CC_LE
  CC_G,       // CC_G
  CC_FALSE,   // CC_FALSE
  CC_TRUE,    // CC_TRUE
};


CondCode HighCC(CondCode cc) {
  assert(CC_O <= cc && cc <= CC_TRUE);
  return highCC[cc];
}


Assembler::Assembler() {
  // setup code buffer
  // (allocated and grown on demand)
  code_buffer_ = NULL;
  code_limit_ = NULL;
  emit_pos_ = NULL;
  dead_code_ = false;
  esp_offset_ = 0;
}


// x86 opcodes
// A name with a trailing underscore denotes the first byte of the opcode
// A name with a leading underscore denotes the second byte of the opcode
// This enum is far from complete, feel free to add opcodes as needed
// Note that we use the name of widest encodable operand in the Opcode names,
// e.g. the r/m64 operand is actually 32-bit wide in 32-bit mode,
// rax means eax in 32-bit mode, etc... (64-bit floating point operands are
// really 64-bit wide, also in 32-bit mode).
enum Opcode {
  OPNDSIZE      = 0x66,   // opnd size prefix
  REX           = 0x40,   // rex prefix in 64-bit mode
  REX_B         = 1,      // rex B bit mask
  REX_X         = 2,      // rex X bit mask
  REX_R         = 4,      // rex R bit mask
  REX_W         = 8,      // rex W bit mask

  ADC_r_rm      = 0x12,   // adc r,r/m
  ADC_rm_i_     = 0x80,   // 1st byte adc r/m,imm
  _ADC_rm_i     = 0x10,   // 2nd byte adc r/m,imm
  ADD_AL_i8     = 0x04,   // add al,imm8
  ADD_RAX_i32   = 0x05,   // add rax,imm32
  ADD_r_rm      = 0x02,   // add r,r/m
  ADD_rm_i_     = 0x80,   // 1st byte add r/m,imm
  _ADD_rm_i     = 0x00,   // 2nd byte add r/m,imm
  ADD_rm64_i32_ = 0x81,   // 1st byte add r/m64,imm32
  _ADD_rm64_i32 = 0x00,   // 2nd byte add r/m64,imm32
  ADD_rm64_i8_  = 0x83,   // 1st byte add r/m64,imm8
  _ADD_rm64_i8  = 0x00,   // 2nd byte add r/m64,imm8
  ADD_rm_r      = 0x00,   // add r/m,r
  AND_r_rm      = 0x22,   // and r,r/m
  AND_rm_i_     = 0x80,   // 1st byte and r/m,imm
  _AND_rm_i     = 0x20,   // 2nd byte and r/m,imm
  AND_rm_r      = 0x20,   // and r/m,r
  CALL_rel32    = 0xE8,   // call rel32
  CALL_rm_      = 0xFF,   // 1st byte call r/m
  _CALL_rm      = 0x10,   // 2nd byte call r/m
  CBW           = 0x98,   // cbw
  CDQ           = 0x99,   // cdq
  CMPSB         = 0xA6,   // cmpsb
  CMP_A_i       = 0x3C,   // cmp a,imm
  CMP_r_rm      = 0x3A,   // cmp r,r/m
  CMP_rm_i_     = 0x80,   // 1st byte cmp r/m,imm
  _CMP_rm_i     = 0x38,   // 2nd byte cmp r/m,imm
  CWD           = 0x99,   // cwd
  DEC_r32       = 0x48,   // dec r32, not valid in 64-bit mode
  DEC_rm_       = 0xFE,   // 1st byte dec r/m
  _DEC_rm       = 0x08,   // 2nd byte dec r/m
  DIV_rm_       = 0xF6,   // 1st byte div rdx:rax,r/m
  _DIV_rm       = 0x30,   // 2nd byte div rdx:rax,r/m
  FABS_         = 0xD9,   // 1st byte fabs
  _FABS         = 0xE1,   // 2nd byte fabs
  FADD_m32_     = 0xD8,   // 1st byte fadd m32
  FADD_m64_     = 0xDC,   // 1st byte fadd m64
  _FADD_m       = 0x00,   // 2nd byte fadd m32/64
  FADDP_        = 0xDE,   // 1st byte faddp
  _FADDP        = 0xC1,   // 2nd byte faddp
  FCHS_         = 0xD9,   // 1st byte fchs
  _FCHS         = 0xE0,   // 2nd byte fchs
  FCOMP_m32_    = 0xD8,   // 1st byte fcomp m32
  FCOMP_m64_    = 0xDC,   // 1st byte fcomp m64
  _FCOMP_m      = 0x18,   // 2nd byte fcomp m32/64
  FDIVP_        = 0xDE,   // 1st byte fdivp
  _FDIVP        = 0xF9,   // 2nd byte fdivp
  FDIV_m32_     = 0xD8,   // 1st byte fdiv m32
  FDIV_m64_     = 0xDC,   // 1st byte fdiv m64
  _FDIV_m       = 0x30,   // 2nd byte fdiv m32/64
  FDIVRP_       = 0xDE,   // 1st byte fdivrp
  _FDIVRP       = 0xF1,   // 2nd byte fdivrp
  FDIVR_m32_    = 0xD8,   // 1st byte fdivr m32
  FDIVR_m64_    = 0xDC,   // 1st byte fdivr m64
  _FDIVR_m      = 0x38,   // 2nd byte fdivr m32/64
  FILD_m32int_  = 0xDB,   // 1st byte fild m32int
  _FILD_m32int  = 0x00,   // 2nd byte fild m32int
  FILD_m64int_  = 0xDF,   // 1st byte fild m64int
  _FILD_m64int  = 0x28,   // 2nd byte fild m64int
  FLD1_         = 0xD9,   // 1st byte fld1
  _FLD1         = 0xE8,   // 2nd byte fld1
  FLDZ_         = 0xD9,   // 1st byte fldz
  _FLDZ         = 0xEE,   // 2nd byte fldz
  FLD_m32_      = 0xD9,   // 1st byte fld m32
  FLD_m64_      = 0xDD,   // 1st byte fld m64
  _FLD_m        = 0x00,   // 2nd byte fld m32/64
  FMULP_        = 0xDE,   // 1st byte fmulp
  _FMULP        = 0xC9,   // 2nd byte fmulp
  FMUL_m32_     = 0xD8,   // 1st byte fmul m32
  FMUL_m64_     = 0xDC,   // 1st byte fmul m64
  _FMUL_m       = 0x08,   // 2nd byte fmul m32/64
  FST_m32_      = 0xD9,   // 1st byte fst m32
  FST_m64_      = 0xDD,   // 1st byte fst m64
  _FST_m        = 0x10,   // 2nd byte fst m32/64
  FSTP_m32_     = 0xD9,   // 1st byte fstp m32
  FSTP_m64_     = 0xDD,   // 1st byte fstp m64
  _FSTP_m       = 0x18,   // 2nd byte fstp m32/64
  FSUBP_        = 0xDE,   // 1st byte fsubp
  _FSUBP        = 0xE9,   // 2nd byte fsubp
  FSUB_m32_     = 0xD8,   // 1st byte fsub m32
  FSUB_m64_     = 0xDC,   // 1st byte fsub m64
  _FSUB_m       = 0x20,   // 2nd byte fsub m32/64
  FSUBRP_       = 0xDE,   // 1st byte fsubrp
  _FSUBRP       = 0xE1,   // 2nd byte fsubrp
  FSUBR_m32_    = 0xD8,   // 1st byte fsubr m32
  FSUBR_m64_    = 0xDC,   // 1st byte fsubr m64
  _FSUBR_m      = 0x28,   // 2nd byte fsubr m32/64
  FWAIT         = 0x9B,   // fwait
  FXCH_         = 0xD9,   // 1st byte fxch st(0) st(1)
  _FXCH         = 0xC9,   // 2nd byte fxch st(0) st(1)
  IDIV_rm_      = 0xF6,   // 1st byte idiv rdx:rax,r/m
  _IDIV_rm      = 0x38,   // 2nd byte idiv rdx:rax,r/m
  IMUL_r_rm_    = 0x0F,   // 1st byte imul r,r/m
  _IMUL_r_rm    = 0xAF,   // 2nd byte imul r,r/m
  IMUL_r_rm_i32 = 0x69,   // imul r,r/m,imm32
  IMUL_r_rm_i8  = 0x6B,   // imul r,r/m,imm8
  IMUL_rm_      = 0xF6,   // 1st byte imul rdx:rax,r/m
  _IMUL_rm      = 0x28,   // 2nd byte imul rdx:rax,r/m
  INC_r32       = 0x40,   // inc r32, not valid in 64-bit mode
  INC_rm_       = 0xFE,   // 1st byte inc r/m
  _INC_rm       = 0x00,   // 2nd byte inc r/m
  INT_3         = 0xCC,   // int 3
  JMP_rel32     = 0xE9,   // jmp rel32
  JMP_rel8      = 0xEB,   // jmp rel8
  JMP_rm_       = 0xFF,   // 1st byte jmp r/m, no prefix in 64-bit mode
  _JMP_rm       = 0x20,   // 2nd byte jmp r/m
  Jcc_rel8      = 0x70,   // jcc rel8
  Jcc_rel32_    = 0x0F,   // 1st byte jcc rel32
  _Jcc_rel32    = 0x80,   // 2nd byte jcc rel32
  LEA_r_m       = 0x8D,   // lea r,m
  LEAVE         = 0xC9,   // leave
  MOVSB         = 0xA4,   // movsb
  MOVSD         = 0xA5,   // movsd
  MOV_A_m       = 0xA0,   // mov a,m
  MOV_m_A       = 0xA2,   // mov m,a
  MOV_r_i       = 0xB0,   // mov r,imm
  MOV_r64_i64   = 0xB8,   // mov r64,imm64
  MOV_r64_rm64  = 0x8B,   // mov r64,r/m64
  MOV_r_rm      = 0x8A,   // mov r,r/m
  MOV_rm_i_     = 0xC6,   // 1st byte mov r/m,imm
  _MOV_rm_i     = 0x00,   // 2nd byte mov r/m,imm
  MOV_rm64_i32_ = 0xC7,   // 1st byte mov r/m64,imm32
  _MOV_rm64_i32 = 0x00,   // 2nd byte mov r/m64,imm32
  MOV_rm_r      = 0x88,   // mov r/m,r
  NOP           = 0x90,   // nop
  NEG_rm_       = 0xF6,   // 1st byte neg r/m
  _NEG_rm       = 0x18,   // 2nd byte neg r/m
  NOT_rm_       = 0xF6,   // 1st byte not r/m
  _NOT_rm       = 0x10,   // 2nd byte not r/m
  OR_r_rm       = 0x0A,   // or r,r/m
  OR_rm_i_      = 0x80,   // 1st byte or r/m,imm
  _OR_rm_i      = 0x08,   // 2nd byte or r/m,imm
  OR_rm_i8_     = 0x83,   // 1st byte or r/m,imm8
  _OR_rm_i8     = 0x08,   // 2nd byte or r/m,imm8
  OR_rm_r       = 0x08,   // or r/m,r
  POPFD         = 0x9D,   // popfd
  POP_m_        = 0x8F,   // 1st byte pop m, no prefix in 64-bit mode
  _POP_m        = 0x00,   // 2nd byte pop m
  POP_r         = 0x58,   // pop r32, no prefix in 64-bit mode
  PUSHFD        = 0x9C,   // pushfd, no prefix in 64-bit mode
  PUSH_i32      = 0x68,   // push imm32, signed extended to stack width
  PUSH_i8       = 0x6A,   // push imm8, signed extended to stack width
  PUSH_r        = 0x50,   // push r, no prefix in 64-bit mode
  PUSH_rm_      = 0xFF,   // 1st byte push r/m, no prefix in 64-bit mode
  _PUSH_rm      = 0x30,   // 2nd byte push r/m
  REP           = 0xF3,   // rep
  REPE          = 0xF3,   // repe
  REPNE         = 0xF2,   // repne
  RET           = 0xC3,   // ret
  RET_i16       = 0xC2,   // ret imm16
  SAHF          = 0x9E,   // sahf
  SAR_rm_       = 0xD0,   // 1st byte of sar r/m,1
  _SAR_rm       = 0x38,   // 2nd byte of sar r/m,1
  SAR_rm_i8_    = 0xC0,   // 1st byte of sar r/m,imm8
  _SAR_rm_i8    = 0x38,   // 2nd byte of sar r/m,imm8
  SBB_r_rm      = 0x1A,   // sbb r,r/m
  _SBB_rm_i     = 0x18,   // 2nd byte sbb r/m,imm
  _SETcc_rm8    = 0x90,   // 2nd byte of setcc
  SHL_rm_       = 0xD0,   // 1st byte of shl r/m,1
  _SHL_rm       = 0x20,   // 2nd byte of shl r/m,1
  SHL_rm_i8_    = 0xC0,   // 1st byte of shl r/m,imm8
  _SHL_rm_i8    = 0x20,   // 2nd byte of shl r/m,imm8
  SHR_rm_       = 0xD0,   // 1st byte of shr r/m,1
  _SHR_rm       = 0x28,   // 2nd byte of shr r/m,1
  SHR_rm_i8_    = 0xC0,   // 1st byte of shr r/m,imm8
  _SHR_rm_i8    = 0x28,   // 2nd byte of shr r/m,imm8
  SUB_A_i       = 0x2C,   // sub al,imm8
  SUB_r_rm      = 0x2A,   // sub r,r/m
  SUB_rm_i_     = 0x80,   // 1st byte sub r/m,imm
  _SUB_rm_i     = 0x28,   // 2nd byte sub r/m,imm
  SUB_rm_r      = 0x28,   // sub r/m,r
  TEST_A_i      = 0xA8,   // test al,imm8
  TEST_rm_i_    = 0xF6,   // 1st byte test r/m,imm
  _TEST_rm_i    = 0x00,   // 2nd byte test r/m,imm
  TEST_rm_r     = 0x84,   // test r/m,r
  XCHG_RAX_r64  = 0x90,   // xchg rax,r64
  XCHG_r_rm     = 0x86,   // xchg r,r/m
  XOR_A_i       = 0x34,   // xor a,imm
  XOR_r_rm      = 0x32,   // xor r,r/m
  XOR_rm_i_     = 0x80,   // 1st byte xor r/m,imm
  _XOR_rm_i     = 0x30,   // 2nd byte xor r/m,imm
  XOR_rm_r      = 0x30,   // xor r/m,r
};


Assembler::~Assembler() {
  delete[] code_buffer_;  // allocated via MakeSpace()
}


void Assembler::MakeSpace() {
  assert(sizeof(Instr) == 1);  // otherwise fix the code below
  assert(emit_pos_ >= code_limit_);  // otherwise should not call this
  // code buffer too small => double size
  Instr* old_buffer = code_buffer_;
  size_t buffer_size = 2 * (code_limit_ - code_buffer_);
  if (buffer_size == 0)
    buffer_size = 32 * 1024;  // adjust as appropriate
  // add some extra space (+32): simplifies the space check
  // in EMIT_PROLOGUE and permits at least one emission
  code_buffer_ = new Instr[buffer_size + 32];  // explicitly deallocated
  CHECK(code_buffer_ != NULL) << "failed to allocate code buffer";
  code_limit_ = code_buffer_ + buffer_size;
  // copy old code
  size_t code_size = emit_pos_ - old_buffer;
  memcpy(code_buffer_, old_buffer, code_size);
  emit_pos_ = code_buffer_ + code_size;
  // get rid of old buffer
  delete[] old_buffer;
  assert(emit_pos_ < code_limit_);
}


// Call this function to determine if code should be emitted.
// Be careful to only disable actual code emission and not
// any surrounding logic in order to preserve code generation
// invariants.
bool Assembler::emit_ok() {
  if (FLAGS_eliminate_dead_code && dead_code_)
    return false;
  if (emit_pos_ >= code_limit_)
    MakeSpace();
  return true;
}


void Assembler::emit_int8(int8 x) {
  if (emit_ok())
    Code::int8_at(emit_pos_) = x;
}


void Assembler::emit_int16(int16 x) {
  if (emit_ok())
    Code::int16_at(emit_pos_) = x;
}


void Assembler::emit_int32(int32 x) {
  if (emit_ok())
    Code::int32_at(emit_pos_) = x;
}


void Assembler::align_emit_offset() {
  // since profiling of native code is not supported, it is not necessary to
  // align native code, but this cannot hurt
  // not particularly fast, but doesn't really matter
  while (emit_offset() % CodeDesc::kAlignment != 0)
    Code::uint8_at(emit_pos_) = NOP;  // do it even if !emit_ok()
}


int Assembler::EmitPrefixes(AddrMod reg3, AddrMod am, int size) {
  int rex_bits = rex_xb[am];
  if (reg3 > 7)
    rex_bits |= REX_R;

  int size_bit;
  switch (size) {
    case 1:
      size_bit = 0;
      break;

    case 2:
      EmitByte(OPNDSIZE);
      // fall through

    case 4:
      size_bit = 1;
      break;

    case 8:
      size_bit = 1;
      rex_bits |= REX_W;
      break;

    default:
      size_bit = 0;
      assert(false);
      break;
  }
  if (rex_bits != 0) {
    // REX prefix is required
    assert(kEmit64);
    EmitByte(REX + rex_bits);
  }
  return size_bit;
}


void Assembler::EmitByte(int b) {
  emit_int8(implicit_cast<int8>(b));
}


void Assembler::Emit2Bytes(int b1, int b2) {
  emit_int8(implicit_cast<int8>(b1));
  emit_int8(implicit_cast<int8>(b2));
}


void Assembler::Emit3Bytes(int b1, int b2, int b3) {
  emit_int8(implicit_cast<int8>(b1));
  emit_int8(implicit_cast<int8>(b2));
  emit_int8(implicit_cast<int8>(b3));
}


void Assembler::EmitWord(int w) {
  emit_int16(implicit_cast<int16>(w));
}


void Assembler::EmitDWord(int d) {
  emit_int32(implicit_cast<int32>(d));
}


void Assembler::EmitByteDWord(int b1, int d2) {
  emit_int8(implicit_cast<int8>(b1));
  emit_int32(implicit_cast<int32>(d2));
}


void Assembler::Emit2BytesDWord(int b1, int b2, int d3) {
  emit_int8(implicit_cast<int8>(b1));
  emit_int8(static_cast<int8>(b2));
  emit_int32(implicit_cast<int32>(d3));
}


// generate effective address
void Assembler::EmitEA(int reg_op, const Operand* n) {
  // emit the ModR/M byte, SIB byte if necessary and the offset (no fixups).
  static const int modrm_mask = 0xC7;
  assert((reg_op & modrm_mask) == 0);

  AddrMod am = n->am;
  if (IsIntReg(am) || IsIndir(am)) {
    assert(n->size > 1 || IsByteReg(am));

    if (am == AM_INDIR + AM_EBP || (kEmit64 && am == AM_INDIR + AM_R13))
      // avoid special case encoding with R/M == 101 and Mod == 00:
      // [EBP] with no disp actually means [no base] + disp32 in 32-bit mode
      // and [RIP] + disp32 in 64-bit mode
      // so change into [EBP] + disp8, with disp8 == 0
      // the same restriction applies to R13
      Emit2Bytes(mod_rm[am] + reg_op + 0x40, 0);
    else
      EmitByte(mod_rm[am] + reg_op);

  } else if (am == AM_ABS) {
    if (kEmit64) {
      // AM_ABS should be used with caution in 64-bit mode, since only 32 bits
      // of the address can be encoded in the instruction (a few opcodes excepted)
      assert(IsDWordRange(n->offset));
      // R/M == 101 and Mod == 00 means [RIP] + disp32 in 64-bit mode
      // use instead R/M == 100 (SIB present), Mod == 00, SIB == 00 100 101
      Emit2BytesDWord(0x04 + reg_op, 0x25, n->offset);
    } else {
      EmitByteDWord(0x05 + reg_op, n->offset);
    }
  } else {
    assert(IsRelMem(am));
    assert(IsDWordRange(n->offset));

    int offs = n->offset;
    if (BaseReg(am) == AM_ESP)
      offs -= esp_offset_;

    if (offs == 0) {
      if (BaseReg(am) == AM_EBP || (kEmit64 && BaseReg(am) == AM_R13))
        // avoid special case encoding with R/M == 101 and Mod == 00
        reg_op += 0x40;  // need 8 bit offset
    } else if (IsByteRange(offs)) {
      reg_op += 0x40;  // need 8 bit offset
    } else {
      reg_op += 0x80;  // need 32 bit offset
    }

    // if indexed with no scaling, but not based,
    // change to based and save a byte.
    if (IsIndexed(am) && n->scale == 0) {
      am = static_cast<AddrMod>(am - (AM_INXD - AM_BASED));
      reg_op += 0x80;
    }

    reg_op += mod_rm[am];
    int sib_byte = sib[am];
    if ((sib_byte & 0x80) == 0) {
      // ModRM byte and SIB byte
      if (HasIndex(am))
        Emit2Bytes(reg_op, (n->scale << 6) + sib_byte);
      else
        Emit2Bytes(reg_op, sib_byte);
    } else {
      // ModRM byte, no SIB byte
      EmitByte(reg_op);
    }

    if (reg_op & 0x40)
      EmitByte(offs);
    else if (reg_op & 0x80)
      EmitDWord(offs);
  }
}


void Assembler::EmitIndirEA(int b1, int b2, AddrMod base_reg, int off) {
  assert(IsIntReg(base_reg));
  EmitByte(b1);
  b2 += mod_rm[AM_BASED + base_reg];

  // avoid special case encoding with R/M == 101 and Mod == 00
  if (off == 0 && !(base_reg == AM_EBP || (kEmit64 && base_reg == AM_R13)))
    ;   // no disp
  else if (IsByteRange(off))
    b2 += 0x40;   // disp8
  else
    b2 += 0x80;   // disp32

  int sib_byte = sib[AM_BASED + base_reg];
  if ((sib_byte & 0x80) == 0)
    // ModRM byte and SIB byte
    Emit2Bytes(b2, sib_byte);
  else
    // ModRM byte, no SIB byte
    EmitByte(b2);

  if (b2 & 0x40)
    EmitByte(off);
  else if (b2 & 0x80)
    EmitDWord(off);
  // else no disp
}


void Assembler::OpSizeEA(int b1, int b2, const Operand* n) {
  int size_bit = EmitPrefixes(AM_NONE, n->am, n->size);
  EmitByte(b1 + size_bit);
  EmitEA(b2, n);
}


void Assembler::OpSizeReg(int b1, int b2, AddrMod reg, int size) {
  assert(IsIntReg(reg));
  assert(size > 1 || IsByteReg(reg));
  int size_bit = EmitPrefixes(AM_NONE, reg, size);
  Emit2Bytes(b1 + size_bit, b2 + mod_rm[reg]);
}


void Assembler::OpSizeRegEA(int op, AddrMod reg, const Operand* n) {
  assert(IsIntReg(reg));
  assert(n->size > 1 || IsByteReg(reg));
  int size_bit = EmitPrefixes(reg, n->am, n->size);
  EmitByte(op + size_bit);
  EmitEA(reg3_encoding[reg], n);
}


void Assembler::OpSizeRegReg(int op, AddrMod reg1, AddrMod reg2, int size) {
  assert(IsIntReg(reg1) && IsIntReg(reg2));
  assert(size > 1 || (IsByteReg(reg1) && IsByteReg(reg2)));
  int size_bit = EmitPrefixes(reg1, reg2, size);
  Emit2Bytes(op + size_bit, reg3_encoding[reg1] + mod_rm[reg2]);
}


void Assembler::OpRegReg(int op, AddrMod reg1, AddrMod reg2) {
  assert(IsIntReg(reg1) && IsIntReg(reg2));
  int size_bit = EmitPrefixes(reg1, reg2, sizeof(intptr_t));
  Emit2Bytes(op + size_bit, reg3_encoding[reg1] + mod_rm[reg2]);
}


void Assembler::MoveRegReg(AddrMod dst_reg, AddrMod src_reg) {
  assert(IsIntReg(dst_reg) && IsIntReg(src_reg));
  if (src_reg == dst_reg)  // move to same reg, suppress
    return;

  OpRegReg(MOV_r_rm, dst_reg, src_reg);
}


void Assembler::AddImmReg(AddrMod dst_reg, int32 val) {
  assert(IsIntReg(dst_reg));
  if (val == 0)
    return;

  if (dst_reg == AM_ESP)
    esp_offset_ += val;

  if (val == 1) {
    IncReg(dst_reg, sizeof(intptr_t));
  } else if (val == -1) {
    DecReg(dst_reg, sizeof(intptr_t));
  } else {
    EmitPrefixes(AM_NONE, dst_reg, sizeof(intptr_t));
    if (IsByteRange(val))
      Emit3Bytes(ADD_rm64_i8_, _ADD_rm64_i8 + mod_rm[dst_reg], val);
    else if (dst_reg == AM_EAX)
      EmitByteDWord(ADD_RAX_i32, val);
    else
      Emit2BytesDWord(ADD_rm64_i32_, _ADD_rm64_i32 + mod_rm[dst_reg], val);
  }
}


void Assembler::EmitImmVal(int32 val, int size) {
  switch (size) {
    case 1: EmitByte(val); break;
    case 2: EmitWord(val); break;
    case 8: assert(kEmit64);  // fall through
    case 4: EmitDWord(val); break;
    default: assert(false);
  }
}


void Assembler::OpImmReg(int b1, int b2, AddrMod reg, int32 val, int size) {
  assert(IsIntReg(reg));
  assert(b1 == 0x80);   // support immediate group 1 only
  assert(size > 1 || IsByteReg(reg));
  int size_bit = EmitPrefixes(AM_NONE, reg, size);
  if (size_bit == 0)
    b1 += 0;  // r8, imm8
  else if (IsByteRange(val))
    b1 += 3;  // r16/32, imm8
  else
    b1 += 1;  // r16/32, imm16/32

  if (reg == AM_EAX && b1 != 0x83) {
    EmitByte(b1 - 0x80 + 0x04 + b2);
    EmitImmVal(val, size);

  } else {
    Emit2Bytes(b1, b2 + mod_rm[reg]);

    if (b1 == 0x83)
      EmitByte(val);
    else
      EmitImmVal(val, size);
  }
}


void Assembler::OpImm(int b1, int b2, const Operand* n, int32 val) {
  assert(b1 == 0x80);   // support immediate group 1 only
  int size_bit = EmitPrefixes(AM_NONE, n->am, n->size);
  if (size_bit == 0)
    b1 += 0;  // r/m8, imm8
  else if (IsByteRange(val))
    b1 += 3;  // r/m16/32, imm8
  else
    b1 += 1;  // r/m16/32, imm16/32

  if (n->am == AM_EAX && b1 != 0x83) {
    EmitByte(b1 - 0x80 + 0x04 + b2);
    EmitImmVal(val, n->size);

  } else {
    EmitByte(b1);
    EmitEA(b2, n);

    if (b1 == 0x83)
      EmitByte(val);
    else
      EmitImmVal(val, n->size);
  }
}


void Assembler::SubImmRegSetCC(AddrMod dst_reg, int32 val, int size) {
  assert(IsIntReg(dst_reg));
  if (val == 0)
    OpSizeRegReg(TEST_rm_r, dst_reg, dst_reg, size);
  else
    OpImmReg(SUB_rm_i_, _SUB_rm_i, dst_reg, val, size);
}


void Assembler::Exg(AddrMod reg1, AddrMod reg2) {
  assert(IsIntReg(reg1) && IsIntReg(reg2));
  if (reg1 == AM_EAX) {
    EmitPrefixes(AM_NONE, reg2, sizeof(intptr_t));
    EmitByte(XCHG_RAX_r64 + reg_encoding[reg2]);
  } else if (reg2 == AM_EAX) {
    EmitPrefixes(AM_NONE, reg1, sizeof(intptr_t));
    EmitByte(XCHG_RAX_r64 + reg_encoding[reg1]);
  } else {
    OpRegReg(XCHG_r_rm, reg1, reg2);
  }
}


void Assembler::CmpRegEA(AddrMod reg, const Operand* r) {
  assert(IsIntReg(reg));
  if (r->am == AM_IMM) {
    assert(r->size > 1 || IsByteReg(reg));
    if (kEmit64 && !IsDWordRange(r->value)) {
      Load(AM_R11, r);
      OpSizeRegReg(CMP_r_rm, reg, AM_R11, r->size);
      return;
    }
    int size_bit = EmitPrefixes(AM_NONE, reg, r->size);
    if (reg == AM_EAX)
      EmitByte(CMP_A_i + size_bit);
    else
      Emit2Bytes(CMP_rm_i_ + size_bit, _CMP_rm_i + mod_rm[reg]);

    EmitImmVal(r->value, r->size);
  } else {
    OpSizeRegEA(CMP_r_rm, reg, r);
  }
}


void Assembler::TestReg(const Operand* n, AddrMod reg) {
  assert(IsIntReg(reg));
  assert(n->size > 1 || IsByteReg(reg));
  int size_bit = EmitPrefixes(reg, n->am, n->size);
  EmitByte(TEST_rm_r + size_bit);
  EmitEA(reg3_encoding[reg], n);
}


void Assembler::TestImm(const Operand* n, int32 val) {
  int size_bit = EmitPrefixes(AM_NONE, n->am, n->size);
  if (n->am == AM_EAX) {
    EmitByte(TEST_A_i + size_bit);
  } else {
    EmitByte(TEST_rm_i_ + size_bit);
    EmitEA(_TEST_rm_i, n);
  }
  EmitImmVal(val, n->size);
}


void Assembler::ShiftRegLeft(AddrMod reg, int power) {
  assert(IsIntReg(reg));
  if (power == 0)
    return;

  if (power == 1) {
    OpRegReg(ADD_r_rm, reg, reg);
  } else {
    int size_bit = EmitPrefixes(AM_NONE, reg, sizeof(intptr_t));
    Emit3Bytes(SHL_rm_i8_ + size_bit, _SHL_rm_i8 + mod_rm[reg], power);
  }
}


void Assembler::ShiftRegRight(AddrMod reg, int power, int size, bool signed_flag) {
  assert(IsIntReg(reg));
  assert(size > 1 || IsByteReg(reg));
  int8 b1 = EmitPrefixes(AM_NONE, reg, size);
  int8 b2 = _SHR_rm + mod_rm[reg];
  if (signed_flag)
    b2 += _SAR_rm - _SHR_rm;
  if (power == 1)
    Emit2Bytes(b1 + SAR_rm_, b2);  // SAR_rm_ == SHR_rm_
  else
    Emit3Bytes(b1 + SAR_rm_i8_, b2, power);  // SAR_rm_i8_ == SHR_rm_i8_
}


void Assembler::Load(AddrMod dst_reg, const Operand* s) {
  assert(IsIntReg(dst_reg));
  assert(s->size > 1 || IsByteReg(dst_reg));

  if (IsIntReg(s->am)) {  // load from reg, move a dword
    if (dst_reg != s->am)  // move to same reg, suppress
      OpRegReg(MOV_r_rm, dst_reg, s->am);

  } else if (dst_reg == AM_EAX && s->am == AM_ABS) {
    int size_bit = EmitPrefixes(AM_NONE, AM_ABS, s->size);
    EmitByteDWord(MOV_A_m + size_bit, s->offset);
    if (kEmit64)
      // MOV_A_m expects 64-bit offset in 64-bit mode, emit higher 32 bits
      EmitDWord(high32(s->offset));

  } else if (s->am == AM_IMM) {

    if (s->value == 0) {
      OpRegReg(XOR_r_rm, dst_reg, dst_reg);

    } else if (s->value == 1) {
      OpRegReg(XOR_r_rm, dst_reg, dst_reg);
      IncReg(dst_reg, sizeof(intptr_t));

    } else if (s->value == -1) {
      EmitPrefixes(AM_NONE, dst_reg, s->size);
      Emit3Bytes(OR_rm_i8_, _OR_rm_i8 + mod_rm[dst_reg], -1);

    } else {
      int size_bit = EmitPrefixes(AM_NONE, dst_reg, s->size);
      if (!kEmit64) {
        EmitByte(MOV_r_i + (size_bit << 3) + reg_encoding[dst_reg]);
        EmitImmVal(s->value, s->size);
      } else {
        if (IsDWordRange(s->value)) {
          // MOV_rm64_i32 is more compact than MOV_r64_i64
          // i32 is sign extended, not zero extended, Intel documentation is wrong
          Emit2Bytes(MOV_rm_i_ + size_bit, _MOV_rm_i + mod_rm[dst_reg]);
          EmitImmVal(s->value, s->size);
        } else {
          assert(s->size == 8);
          EmitByte(MOV_r_i + (size_bit << 3) + reg_encoding[dst_reg]);
          EmitImmVal(s->value, s->size);
          // MOV_r64_i64 expects 64-bit immediate value, emit higher 32 bits
          EmitDWord(high32(s->value));
        }
      }
    }
  } else {
    int size_bit = EmitPrefixes(dst_reg, s->am, s->size);
    EmitByte(MOV_r_rm + size_bit);
    EmitEA(reg3_encoding[dst_reg], s);
  }
}


void Assembler::LoadEA(AddrMod dst_reg, const Operand* s) {
  assert(IsIntReg(dst_reg));
  // optimize when lea can be replaced by shorter or faster instructions:
  // AM_ABS  is done by mov reg,imm32
  // AM_BASED is done by add reg,imm8/imm32 if reg == BaseReg(s->am)
  // AM_BASED is done by mov reg,reg if offs == 0
  if (s->am == AM_ABS) {
    int size_bit = EmitPrefixes(AM_NONE, dst_reg, sizeof(intptr_t));
    EmitByteDWord(MOV_r_i + (size_bit << 3) + reg_encoding[dst_reg], s->offset);
    if (kEmit64)
      // MOV_r64_i64 expects 64-bit immediate value, emit higher 32 bits
      EmitDWord(high32(s->offset));

  } else if (IsIndir(s->am)) {
    MoveRegReg(dst_reg, BaseReg(s->am));

  } else if (IsBased(s->am)) {
    assert(IsDWordRange(s->offset));
    int offs = s->offset;
    if (BaseReg(s->am) == AM_ESP)
      offs -= esp_offset_;

    if (dst_reg == BaseReg(s->am)) {
      AddImmReg(dst_reg, offs);

    } else if (offs == 0) {
      MoveRegReg(dst_reg, BaseReg(s->am));

    } else {
      EmitPrefixes(dst_reg, s->am, sizeof(intptr_t));
      EmitByte(LEA_r_m);
      EmitEA(reg3_encoding[dst_reg], s);
    }

  } else if (IsIndexed(s->am) && s->scale == 0 && BaseReg(s->am) == dst_reg) {
    assert(IsDWordRange(s->offset));
    EmitPrefixes(AM_NONE, dst_reg, sizeof(intptr_t));
    if (dst_reg == AM_EAX)
      EmitByteDWord(ADD_RAX_i32, s->offset);
    else
      Emit2BytesDWord(ADD_rm64_i32_, _ADD_rm64_i32 + mod_rm[dst_reg], s->offset);

  } else if (IsBasedIndexed(s->am) && s->scale == 0 &&
             BaseReg(s->am) == dst_reg && s->offset == 0) {
    // replace LEA reg1,[reg1+reg2] by ADD reg1,reg2
    OpRegReg(ADD_r_rm, BaseReg(s->am), static_cast<AddrMod>((s->am - AM_BINXD)>>4));

  } else {
    EmitPrefixes(dst_reg, s->am, sizeof(intptr_t));
    EmitByte(LEA_r_m);
    EmitEA(reg3_encoding[dst_reg], s);
  }
}


void Assembler::Store(const Operand* d, AddrMod src_reg) {
  assert(IsIntReg(src_reg));
  if (src_reg == d->am)   // move to same reg, suppress
    return;

  if (src_reg == AM_EAX && d->am == AM_ABS) {
    int size_bit = EmitPrefixes(AM_NONE, AM_ABS, d->size);
    EmitByteDWord(MOV_m_A + size_bit, d->offset);
    if (kEmit64)
      // MOV_m_A expects 64-bit offset in 64-bit mode, emit higher 32 bits
      EmitDWord(high32(d->offset));

  } else {
    int size_bit = EmitPrefixes(src_reg, d->am, d->size);
    EmitByte(MOV_rm_r + size_bit);
    EmitEA(reg3_encoding[src_reg], d);
  }
}


void Assembler::Push(const Operand* n) {
  if (kEmit64 && n->am == AM_IMM && !IsDWordRange(n->value)) {
    Load(AM_R11, n);
    PushReg(AM_R11);
    return;
  }
  // PUSH_r defaults to 64-bit operand size; REX_W not needed: set size to 4
  EmitPrefixes(AM_NONE, n->am, 4);
  if (IsIntReg(n->am)) {
    EmitByte(PUSH_r + reg_encoding[n->am]);
  } else if (n->am == AM_IMM) {
    if (IsByteRange(n->value)) {
      Emit2Bytes(PUSH_i8, n->value);
    } else {
      EmitByteDWord(PUSH_i32, n->value);
    }
  } else {
    EmitByte(PUSH_rm_);
    EmitEA(_PUSH_rm, n);
  }
  esp_offset_ -= sizeof(intptr_t);
}


void Assembler::PushReg(AddrMod reg) {
  assert(IsIntReg(reg));
  // PUSH_r defaults to 64-bit operand size; REX_W not needed: set size to 4
  EmitPrefixes(AM_NONE, reg, 4);
  EmitByte(PUSH_r + reg_encoding[reg]);
  esp_offset_ -= sizeof(intptr_t);
}


void Assembler::PopReg(AddrMod reg) {
  assert(IsIntReg(reg));
  // POP_r defaults to 64-bit operand size; REX_W not needed: set size to 4
  EmitPrefixes(AM_NONE, reg, 4);
  EmitByte(POP_r + reg_encoding[reg]);
  esp_offset_ += sizeof(intptr_t);
}


void Assembler::PushRegs(RegSet regs) {
  assert((regs & ~RS_ANY) == RS_EMPTY);
  AddrMod reg = AM_LAST_REG;
  RegSet reg_as_set = RS_LAST_REG;
  while (regs) {
    if ((reg_as_set & regs) != RS_EMPTY) {
      PushReg(reg);
      regs -= reg_as_set;
    }
    reg = static_cast<AddrMod>((implicit_cast<int>(reg)) - 1);
    reg_as_set >>= 1;
  }
}


// Patch the code emitted at offset in the code buffer by PushRegs(pushed)
// to only push 'subset' regs instead of 'pushed' regs
void Assembler::PatchPushRegs(int offset, RegSet pushed, RegSet subset) {
  assert((subset & ~pushed) == RS_EMPTY);
  AddrMod reg = AM_LAST_REG;
  RegSet reg_as_set = RS_LAST_REG;
  while (pushed) {
    if ((reg_as_set & pushed) != RS_EMPTY) {
      if ((reg_as_set & subset) == RS_EMPTY) {
        if (reg > 7) {
          PatchByte(offset, OPNDSIZE);  // patch REX prefix
          PatchByte(offset + 1, NOP);  // patch PUSH_r instruction
        } else {
          PatchByte(offset, NOP);  // patch PUSH_r instruction
        }
      }
      if (reg > 7)
        offset++;
      offset++;
      pushed -= reg_as_set;
    }
    reg = static_cast<AddrMod>((implicit_cast<int>(reg)) - 1);
    reg_as_set >>= 1;
  }
}


void Assembler::PopRegs(RegSet regs) {
  assert((regs & ~RS_ANY) == RS_EMPTY);
  AddrMod reg = AM_EAX;
  RegSet reg_as_set = RS_EAX;
  while (regs) {
    if ((reg_as_set & regs) != RS_EMPTY) {
      PopReg(reg);
      regs -= reg_as_set;
    }
    reg = static_cast<AddrMod>((implicit_cast<int>(reg)) + 1);
    reg_as_set <<= 1;
  }
}


void Assembler::FAdd(const Operand* n) {
  assert(n->size == sizeof(float) || n->size == sizeof(double));
  if (n->am == AM_FST) {
    Emit2Bytes(FADDP_, _FADDP);
  } else {
    EmitByte(n->size == sizeof(float) ? FADD_m32_ : FADD_m64_);
    EmitEA(_FADD_m, n);
  }
}


void Assembler::FSub(const Operand* n) {
  assert(n->size == sizeof(float) || n->size == sizeof(double));
  if (n->am == AM_FST) {
    Emit2Bytes(FSUBP_, _FSUBP);
  } else {
    EmitByte(n->size == sizeof(float) ? FSUB_m32_ : FSUB_m64_);
    EmitEA(_FSUB_m, n);
  }
}


void Assembler::FSubR(const Operand* n) {
  assert(n->size == sizeof(float) || n->size == sizeof(double));
  if (n->am == AM_FST) {
    Emit2Bytes(FSUBRP_, _FSUBRP);
  } else {
    EmitByte(n->size == sizeof(float) ? FSUBR_m32_ : FSUBR_m64_);
    EmitEA(_FSUBR_m, n);
  }
}


void Assembler::FMul(const Operand* n) {
  assert(n->size == sizeof(float) || n->size == sizeof(double));
  if (n->am == AM_FST) {
    Emit2Bytes(FMULP_, _FMULP);
  } else {
    EmitByte(n->size == sizeof(float) ? FMUL_m32_ : FMUL_m64_);
    EmitEA(_FMUL_m, n);
  }
}


void Assembler::FDiv(const Operand* n) {
  assert(n->size == sizeof(float) || n->size == sizeof(double));
  if (n->am == AM_FST) {
    Emit2Bytes(FDIVP_, _FDIVP);
  } else {
    EmitByte(n->size == sizeof(float) ? FDIV_m32_ : FDIV_m64_);
    EmitEA(_FDIV_m, n);
  }
}


void Assembler::FDivR(const Operand* n) {
  assert(n->size == sizeof(float) || n->size == sizeof(double));
  if (n->am == AM_FST) {
    Emit2Bytes(FDIVRP_, _FDIVRP);
  } else {
    EmitByte(n->size == sizeof(float) ? FDIVR_m32_ : FDIVR_m64_);
    EmitEA(_FDIVR_m, n);
  }
}


void Assembler::FLoad(const Operand* n) {
  assert(n->size == sizeof(float) || n->size == sizeof(double));
  assert(n->am != AM_FST);
  assert(!IsIntReg(n->am));
#if 0  // optimize loading of floating point constants 0.0 and 1.0
  if (n->am == AM_ABS) {
    if (n->offset == (sword_t)(&fconst_0)
        || n->offset == (sword_t)(&dconst_0) ) {
        Emit2Bytes(FLDZ_, _FLDZ);
        return;
    }
    if (n->offset == (sword_t)(&fconst_1)
        || n->offset == (sword_t)(&dconst_1)) {
        Emit2Bytes(FLD1_, _FLD1);
        return;
    }
  }
#endif
  EmitByte(n->size == sizeof(float) ? FLD_m32_ : FLD_m64_);
  EmitEA(_FLD_m, n);
}


void Assembler::FStore(const Operand* n, int pop_flag) {
  assert(n->size == sizeof(float) || n->size == sizeof(double));
  assert(n->am != AM_FST);
  assert(!IsIntReg(n->am));
  EmitByte(n->size == sizeof(float) ? FST_m32_ : FST_m64_);  // FST_mxx_ == FSTP_mxx_
  EmitEA(pop_flag ? _FSTP_m : _FST_m, n);

  // FWAIT is necessary for floating point exceptions at the exact location
  // EmitByte(FWAIT);
}


void Assembler::IncReg(AddrMod reg, int size) {
  if (size == 4 && !kEmit64)  // no INC_r32 in 64-bit mode
    EmitByte(INC_r32 + reg_encoding[reg]);
  else
    OpSizeReg(INC_rm_, _INC_rm, reg, size);
}


void Assembler::Inc(const Operand* n) {
  if (IsIntReg(n->am))
    IncReg(n->am, n->size);
  else
    OpSizeEA(INC_rm_, _INC_rm, n);
}


void Assembler::DecReg(AddrMod reg, int size) {
  if (size == 4 && !kEmit64)  // no DEC_r32 in 64-bit mode
    EmitByte(DEC_r32 + reg_encoding[reg]);
  else
    OpSizeReg(DEC_rm_, _DEC_rm, reg, size);
}


void Assembler::Dec(const Operand* n) {
  if (IsIntReg(n->am))
    DecReg(n->am, n->size);
  else
    OpSizeEA(DEC_rm_, _DEC_rm, n);
}


void Assembler::Leave() {
  EmitByte(LEAVE);
}


void Assembler::Ret() {
  EmitByte(RET);
}


void Assembler::Int3() {
  EmitByte(INT_3);
}


void Assembler::AddRegEA(AddrMod dst_reg, const Operand* n) {
  if (n->am == AM_IMM) {
    if (kEmit64 && !IsDWordRange(n->value)) {
      Load(AM_R11, n);
      OpSizeRegReg(ADD_r_rm, dst_reg, AM_R11, n->size);
    } else {
      OpImmReg(ADD_rm_i_, _ADD_rm_i, dst_reg, n->value, n->size);
    }
  } else {
    OpSizeRegEA(ADD_r_rm, dst_reg, n);
  }
}


void Assembler::SubRegEA(AddrMod dst_reg, const Operand* n) {
  if (n->am == AM_IMM) {
    if (kEmit64 && !IsDWordRange(n->value)) {
      Load(AM_R11, n);
      OpSizeRegReg(SUB_r_rm, dst_reg, AM_R11, n->size);
    } else {
      OpImmReg(SUB_rm_i_, _SUB_rm_i, dst_reg, n->value, n->size);
    }
  } else {
    OpSizeRegEA(SUB_r_rm, dst_reg, n);
  }
}


void Assembler::AndRegEA(AddrMod dst_reg, const Operand* n) {
  if (n->am == AM_IMM) {
    if (kEmit64 && !IsDWordRange(n->value)) {
      Load(AM_R11, n);
      OpSizeRegReg(AND_r_rm, dst_reg, AM_R11, n->size);
    } else {
      OpImmReg(AND_rm_i_, _AND_rm_i, dst_reg, n->value, n->size);
    }
  } else {
    OpSizeRegEA(AND_r_rm, dst_reg, n);
  }
}


void Assembler::OrRegEA(AddrMod dst_reg, const Operand* n) {
  if (n->am == AM_IMM) {
    if (kEmit64 && !IsDWordRange(n->value)) {
      Load(AM_R11, n);
      OpSizeRegReg(OR_r_rm, dst_reg, AM_R11, n->size);
    } else {
      OpImmReg(OR_rm_i_, _OR_rm_i, dst_reg, n->value, n->size);
    }
  } else {
    OpSizeRegEA(OR_r_rm, dst_reg, n);
  }
}


void Assembler::JmpIndir(const Operand* n) {
  // JMP_rm_ defaults to 64-bit operand size; REX_W not needed: set size to 4
  EmitPrefixes(AM_NONE, n->am, 4);
  EmitByte(JMP_rm_);
  EmitEA(_JMP_rm, n);
}


void Assembler::CallIndir(const Operand* n) {
  // CALL_rm_ defaults to 64-bit operand size; REX_W not needed: set size to 4
  EmitPrefixes(AM_NONE, n->am, 4);
  EmitByte(CALL_rm_);
  EmitEA(_CALL_rm, n);
}


// returns offset of rel8 in code buffer
int Assembler::JmpRel8(int8 rel8) {
  if (!emit_ok())
    return kDeadCodeOffset;  // dead code, nothing to patch

  EmitByte(JMP_rel8);
  int offset = emit_offset();
  EmitByte(rel8);
  return offset;
}


// returns offset of rel32 in code buffer
int Assembler::JmpRel32(int32 rel32) {
  if (!emit_ok())
    return kDeadCodeOffset;  // dead code, nothing to patch

  EmitByte(JMP_rel32);
  int offset = emit_offset();
  EmitDWord(rel32);
  return offset;
}


// returns offset of rel8 in code buffer
int Assembler::JccRel8(CondCode cc, int8 rel8) {
  if (!emit_ok())
    return kDeadCodeOffset;  // dead code, nothing to patch

  EmitByte(Jcc_rel8 + cc);
  int offset = emit_offset();
  EmitByte(rel8);
  return offset;
}


// returns offset of rel32 in code buffer
int Assembler::JccRel32(CondCode cc, int32 rel32) {
  if (!emit_ok())
    return kDeadCodeOffset;  // dead code, nothing to patch

  Emit2Bytes(Jcc_rel32_, _Jcc_rel32 + cc);
  int offset = emit_offset();
  EmitDWord(rel32);
  return offset;
}


int Assembler::CallRel32(int32 rel32) {
  if (!emit_ok())
    return kDeadCodeOffset;  // dead code, nothing to patch

  EmitByte(CALL_rel32);
  int offset = emit_offset();
  EmitDWord(rel32);
  return offset;
}


void Assembler::PatchRel8(int offset, int8 rel8) {
  if (offset == kDeadCodeOffset)
    return;  // do not patch dead code

  assert(implicit_cast<unsigned>(offset + sizeof(int8) + rel8) <= emit_offset());
  PatchByte(offset, rel8);
}


void Assembler::PatchRel32(int offset, int32 rel32) {
  if (offset == kDeadCodeOffset)
    return;  // do not patch dead code

  assert(implicit_cast<unsigned>(offset + sizeof(int32) + rel32) <= emit_offset());
  PatchDWord(offset, rel32);
}


int Assembler::AddImm8Esp(int imm8) {
  assert(IsByteRange(imm8));
  esp_offset_ += imm8;
  EmitPrefixes(AM_NONE, AM_ESP, sizeof(intptr_t));
  Emit3Bytes(ADD_rm64_i8_, _ADD_rm64_i8 + mod_rm[AM_ESP], imm8);
  return emit_offset() - sizeof(int8);
}


void Assembler::PatchImm8(int offset, int imm8) {
  assert(IsByteRange(imm8));
  PatchByte(offset, imm8);
}


void Assembler::PatchByte(unsigned offset, int b) {
  if (offset == kDeadCodeOffset)
    return;  // do not patch dead code

  assert(offset <= emit_offset() - sizeof(int8));
  Instr* emit_pos = code_buffer_ + offset;
  Code::int8_at(emit_pos) = implicit_cast<int8>(b);
}


void Assembler::PatchDWord(unsigned offset, int d) {
  if (offset == kDeadCodeOffset)
    return;  // do not patch dead code

  assert(offset <= emit_offset() - sizeof(int32));
  Instr* emit_pos = code_buffer_ + offset;
  Code::int32_at(emit_pos) = d;
}

}  // namespace sawzall
