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

// Helper functions used by szl

extern const char* explain_default;

void TraceStringInput(uint64 record_number, const char* input, size_t size);
void ApplyToLines(sawzall::Process* process, const char* file_name,
                  uint64 begin, uint64 end);

string TableOutput(sawzall::Process* process);

void Explain();

sawzall::Mode ExecMode();
