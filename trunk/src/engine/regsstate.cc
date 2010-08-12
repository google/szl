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

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/code.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/assembler.h"
#include "engine/regsstate.h"


namespace sawzall {

// ----------------------------------------------------------------------------
// Implementation of RegsState

// Initialize state to "no register used yet"
void RegsState::Clear() {
  for (int i = AM_EAX; i <= AM_LAST_REG; ++i)
    ref_[i] = 0;

  live_ = RS_EMPTY;
  used_ = RS_EMPTY;
}


// increments the ref count of reg in regs by cnt
inline void RegsState::IncRef(int reg, int cnt) {
  assert(AM_EAX <= reg && reg <= AM_LAST_REG);
  if (((1 << reg) & RS_ALL) != RS_EMPTY) {
    ref_[reg] += cnt;
    if (ref_[reg] > 0) {
      RegSet rs = static_cast<RegSet>(1 << reg);
      live_ |= rs;
      used_ |= rs;
    }
  }
}


// decrements the ref cnt of reg in regs by cnt
inline void RegsState::DecRef(int reg, int cnt) {
  assert(AM_EAX <= reg && reg <= AM_LAST_REG);
  if (((1 << reg) & RS_ALL) != RS_EMPTY) {
    ref_[reg] -= cnt;
    if (ref_[reg] == 0)
      live_ &= ~static_cast<RegSet>(1 << reg);

    assert(ref_[reg] >= 0);
  }
}


// Find an available register out of the given set and reserve it
AddrMod RegsState::GetReg(RegSet rs) {
  RegSet avail_regs = rs & ~live_ & RS_ALL;
  if (avail_regs == RS_EMPTY)
    return AM_NONE;

  AddrMod reg = FirstReg(avail_regs);
  assert(ref_[reg] == 0);
  IncRef(reg, 1);
  return reg;
}


// Increment the ref count of the registers used by the given addressing mode
void RegsState::ReserveRegs(AddrMod am) {
  AddrMod reg1 = Reg1(am);
  if (reg1 != AM_NONE) {
    IncRef(reg1, 1);
    AddrMod reg2 = Reg2(am);
    if (reg2 != AM_NONE)
      IncRef(reg2, 1);
  }
}


// Add the registers of the given register state to this state
void RegsState::ReserveRegs(const RegsState* rs) {
  if (rs != NULL)
    for (int r = AM_EAX; r <= AM_LAST_REG; ++r)
      IncRef(r, rs->ref(r));
}


// Decrement the ref count of the registers used by the given addressing mode
void RegsState::ReleaseRegs(AddrMod am) {
  AddrMod reg1 = Reg1(am);
  if (reg1 != AM_NONE) {
    DecRef(reg1, 1);
    AddrMod reg2 = Reg2(am);
    if (reg2 != AM_NONE)
      DecRef(reg2, 1);
  }
}


// Remove the registers of the given register state from this state
void RegsState::ReleaseRegs(const RegsState* rs) {
  if (rs != NULL)
    for (int r = AM_EAX; r <= AM_LAST_REG; ++r)
      DecRef(r, rs->ref(r));
}


// Remove the registers of the given register set from this state
void RegsState::ReleaseRegs(RegSet regs) {
  AddrMod reg = AM_EAX;
  RegSet reg_as_set = RS_EAX;
  while (regs) {
    if ((reg_as_set & regs) != RS_EMPTY) {
      ref_[reg] = 0;
      live_ &= ~reg_as_set;
      regs -= reg_as_set;
    }
    reg = static_cast<AddrMod>(static_cast<int>(reg) + 1);
    reg_as_set <<= 1;
  }
}

}  // namespace sawzall
