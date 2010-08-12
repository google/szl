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

#ifndef _GZIPWRAPPER_H
#define _GZIPWRAPPER_H

#include <string>

using namespace std;

// Compression levels are 0 (none) to 9 (best); pass -1 for the default.

bool GunzipString(const unsigned char* source, int source_len, string* dest);

// Returns true if successful. If there was an error, returns false and
// leaves "*uncompressed" in an indeterminate state.
bool GzipString(const unsigned char* source, int source_len, string* dest,
                int compressionLevel = -1);


#endif  // _GZIPWRAPPER_H
