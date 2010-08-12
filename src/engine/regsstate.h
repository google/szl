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

// ----------------------------------------------------------------------------
// RegsState
//
// RegsState is a helper class keeping track of allocated registers and their
// reference count as used by operands.

class RegsState {
 public:
  RegsState()  { Clear(); }
  explicit RegsState(const RegsState& rs)  { *this = rs; }

  int ref(int r) const { assert(AM_EAX <= r && r <= AM_LAST_REG); return ref_[r]; }
  RegSet live() const { return live_; }
  RegSet used() const { return used_; }

  // Initialize state to "no register used yet"
  void Clear();

  // Find an available register out of the given set and reserve it
  // RegSet is defined in iassembler.h
  AddrMod GetReg(RegSet rs);

  // Increment the ref count of the registers used by the given addressing mode
  void ReserveRegs(AddrMod am);

  // Add the registers of the given register state to this state
  void ReserveRegs(const RegsState* rs);

  // Decrement the ref count of the registers used by the given addressing mode
  void ReleaseRegs(AddrMod am);

  // Remove the registers of the given register state from this state
  void ReleaseRegs(const RegsState* rs);

  // Remove the registers of the given register set from this state
  void ReleaseRegs(RegSet regs);

 private:
  inline void IncRef(int reg, int cnt);
  inline void DecRef(int reg, int cnt);

  int ref_[AM_LAST_REG + 1];  // a reg may be used by several operands
  RegSet live_;  // the set of regs currently used by this state
  RegSet used_;  // the set of regs used by this state since last call to Clear()
};

}  // namespace sawzall
