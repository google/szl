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

class ErrorHandler;
class FileLine;
class Scanner;

// Print errors and maintain an error counter.

class Error {
 public:
  explicit Error(ErrorHandler* error_handler);

  // Reporting.  If scanner is non-NULL, include the traceback of source files.
  void Report(Scanner* scanner, bool is_warning, const char* fmt, ...);
  void Reportv(Scanner* scanner, bool is_warning, const char* fmt,
               va_list* args);
  void Reportv(const FileLine* fileline, bool is_warning, const char* fmt,
               va_list* args);

  // Number of errors seen.
  int count() const  { return count_; }

 private:
  int count_;
  ErrorHandler* error_handler_;
  void Reportv(const Scanner* scanner, const FileLine* fileline, bool is_warning,
               const char* fmt, va_list* args);
};

}  // namespace sawzall
