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

#include <stdio.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/code.h"
#include "engine/assembler.h"


namespace sawzall {

static void AssemblerTest() {
  F.print("AssemblerTest\n");
  Assembler asm_;

  // generate prologue
  asm_.PushReg(AM_EBP);
  asm_.MoveRegReg(AM_EBP, AM_ESP);
  asm_.PushRegs(RS_CALLEE_SAVED);  // not necessary, just testing

  // generate subtraction of 2 args and return result in eax
#if defined(__i386__)
  Operand x_arg(AM_BASED + AM_EBP, sizeof(int), 8);  // 1st argument
  Operand y_arg(AM_BASED + AM_EBP, sizeof(int), 12);  // 2nd argument
#elif defined(__x86_64__)
  Operand x_arg(AM_EDI);  // 1st argument
  Operand y_arg(AM_ESI);  // 2nd argument
#endif
  asm_.Load(AM_EAX, &x_arg);
  asm_.SubRegEA(AM_EAX, &y_arg);

  // epilogue
  asm_.PopRegs(RS_CALLEE_SAVED);
  asm_.Leave();
  asm_.Ret();

  // map generated code in executable page
  Instr* mapped_code;
  size_t mapped_size;
  Code::MemMapCode(asm_.code_buffer(), asm_.emit_offset(),
                   &mapped_code, &mapped_size);
  Code::FlushInstructionCache(mapped_code, mapped_size);

  // invoke generated code
  typedef int (*fun) (int x, int y);
  int x = 10;
  int y = 20;
  int diff = (*(fun)mapped_code)(x, y);
  CHECK_EQ(diff, x - y);

  // unmap code
  Code::MemUnmapCode(mapped_code, mapped_size);

  F.print("done\n");
}

}  // namespace sawzall


int main(int argc, char **argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  sawzall::AssemblerTest();
  printf("PASS\n");
  return 0;
}
