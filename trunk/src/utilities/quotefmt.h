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

// Formatting functions that handle quoted characters and strings

int DQRuneStrFmt(Fmt::State*);   // %Q: double-quoted rune string
int DQUTF8StrFmt(Fmt::State*);   // %q: double-quoted UTF-8 string
int ZDQRuneStrFmt(Fmt::State*);  // %Z: double-quoted rune string; embedded \0 OK
int ZDQUTF8StrFmt(Fmt::State*);  // %z: double-quoted UTF-8 string embedded \0 OK
int SQRuneFmt(Fmt::State*);      // %k: single-quoted unicode character

}  // namespace sawzall
