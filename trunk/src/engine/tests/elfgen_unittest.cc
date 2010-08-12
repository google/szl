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
#include <stdlib.h>
#include <string>

#include "engine/globals.h"
#include "public/logging.h"

#include "utilities/sysutils.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/code.h"
#include "engine/assembler.h"
#include "engine/elfgen.h"


namespace sawzall {

static void ELFGenTest() {
  printf("ELFGenTest\n");

  // Generate code
  Assembler asm_;
  asm_.Exg(AM_EAX, AM_EDX);
  asm_.Exg(AM_EDX, AM_EAX);
  const int prologue = asm_.emit_offset();
  asm_.PushReg(AM_EBP);
  asm_.MoveRegReg(AM_EBP, AM_ESP);
  const int body = asm_.emit_offset();
  asm_.Exg(AM_EAX, AM_EDX);
  asm_.Exg(AM_EDX, AM_EAX);
  const int epilogue = asm_.emit_offset();
  asm_.Leave();
  asm_.Ret();
  const int end = asm_.emit_offset();

  const Instr* code = asm_.code_buffer();
  const int size = asm_.emit_offset();

  // Generate ELF file
  ELFGen elf;

  // code
  uintptr_t map_beg;
  uintptr_t map_end;
  int map_offset;
  elf.AddCode(code, size, &map_beg, &map_end, &map_offset);

  // symbols
  elf.AddFunction("TestFun", code + prologue, end - prologue);

  // debug line info
  elf.AddLine("testsource", 99, code + body);  // +98 needs 2 bytes (signed varint)
  elf.AddLine("testsource", 8, code + epilogue);  // -91 line delta
  elf.EndLineSequence(code + end);

  // Write file to disk
  CHECK(elf.WriteFile("/tmp/elf"));

  // Read generated file
  const char* objdump = getenv("OBJDUMP_UTILITY");
  CHECK(objdump != NULL);
  string command = string(objdump) + " -d -l -t /tmp/elf";
  string disassembly;
  RunCommand(command.c_str(), &disassembly);

  // Check for symbol
  CHECK(disassembly.find("<TestFun>:\nTestFun():\n", 0) != string::npos);

  // Check for line info in code
  CHECK(disassembly.find("bp\ntestsource:99", 0) != string::npos);
  CHECK(disassembly.find("dx\ntestsource:8", 0) != string::npos);

  // Verify that the "TestFun" symbol is present and has the right value.
  size_t testfun = disassembly.find("TestFun\n", 0);
  CHECK(testfun != string::npos);
  size_t startofline = disassembly.rfind("\n", testfun);
  if (startofline == string::npos)
    startofline = 0;
  string actual = string(disassembly, startofline+1, 17);
  char expected[18];
  sprintf(expected, "%016llx ", reinterpret_cast<uint64>(code + prologue));
  CHECK_EQ(actual, expected);

  printf("done\n");
}

}  // namespace sawzall


int main(int argc, char **argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  sawzall::ELFGenTest();
  printf("PASS\n");
  return 0;
}
