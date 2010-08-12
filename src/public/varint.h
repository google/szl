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


const int kMaxUnsignedVarint32Length = 5;
const int kMaxUnsignedVarint64Length = 10;

// Returns pointer to the byte after the last one written.
char* EncodeUnsignedVarint32(char* sptr, uint32 v);

// Returns pointer to the byte after the last one written.
char* EncodeUnsignedVarint64(char* sptr, uint64 v);

// Returns pointer to the byte after the last one read, or NULL if error.
const char* DecodeUnsignedVarint32(const char* p, uint32* OUTPUT);

// Returns pointer to the byte after the last one read, or NULL if error.
const char* DecodeUnsignedVarint64(const char* p, uint64* OUTPUT);


}  // namespace sawzall
