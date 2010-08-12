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

// Database interface - connection to MySQL not yet implemented.

class SzlDB {
 public:
  static SzlDB* Connect(const char* dbspec, const char* defaultspec) {
    return NULL;
  }
  bool SafeExecuteQuery(const char* query)  { return false; }
  int GetRowCount()  { return 0; }
  int GetColCount()  { return 0; }
  bool Fetch()  { return false; }
  const char* GetString(int col)  { return NULL; }
};
