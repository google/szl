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

// Szl's implementation of the EmitterFactory.
// The factory can create two types of emitters - print emitters that print
// emit statements and szl emitters that aggregate data and display the totals.

class SzlEmitterFactory : public sawzall::EmitterFactory {
 public:
  // vocal_szl_emitters is a comma-separated list of tables that require
  // szl emitters with enabled display of aggregated totals; if the list is
  // empty, print emitters are to be created for all tables.
  SzlEmitterFactory(Fmt::State* f, string vocal_szl_emitters);
  ~SzlEmitterFactory();

  // If the factory is configured to create all print emitters, returns a print
  // emitter. Otherwise, returns szl emitters for most table kinds and
  // print emitters for the few tables that don't have aggregation support -
  // the emitter is silent if the table is not listed in vocal_szl_emitters.
  // Returns NULL if an error occurs and reports the error message via the
  // error argument.
  sawzall::Emitter* NewEmitter(sawzall::TableInfo* table_info, string* error);

 private:
  sawzall::Emitter* NewSzlEmitter(sawzall::TableInfo* table_info,
                                  string* error);

  bool is_vocal_szl_emitter(string name);
  vector<string> vocal_szl_emitters_;  // these will display aggregated totals

  bool all_print_emitters_;
  vector<sawzall::Emitter*> emitters_;

  Fmt::State* f_;
};
