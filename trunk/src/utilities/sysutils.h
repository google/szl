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


// ----------------------------------------------------------------------
// PhysicalMem()
//    The amount of physical memory (RAM) a machine has.
//    Returns 0 if it couldn't figure out the memory.
// ----------------------------------------------------------------------

uint64 PhysicalMem();


// ----------------------------------------------------------------------
// VirtualProcesSize()
//    Returns the virtual memory size of this process.
//    Returns -1 on error.
//    We get this information from /proc/self/stat
// ----------------------------------------------------------------------

int64 VirtualProcessSize();


// ----------------------------------------------------------------------
// Other


uint64 CycleClockNow();

bool RunCommand(const char* command, string* stdout_contents);


}  // namespace sawzall
