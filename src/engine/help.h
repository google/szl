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

// Help is a class providing various self-documentation
// tools for the Sawzall component.

class Help {
 public:
   // Print all predefined identifiers in the universe scope.
  static void PrintUniverse();
  
  // Print the definition for Object obj.
  static void PrintDefinition(Object* obj);
 
  // Print definition and documentation of all predeclared Sawzall
  // identifiers in HTML format.
  static void PrintHtmlDocumentation(const char* title);
  
  // Print definition and documentation of a predeclared Sawzall identifier.
  // Returns true if explanation was printed; returns false otherwise.
  static bool Explain(const char* name);
};

} // namespace sawzall
