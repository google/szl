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

// Helper functions and constants used by szl.

#include <stdio.h>
#include <string>
#include <vector>
#include <string.h>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/strutils.h"
#include "fmt/fmt.h"

#include "public/sawzall.h"
#include "public/emitterinterface.h"
#include "app/szlutils.h"


DECLARE_string(table_output);
DECLARE_string(undefok);
DECLARE_string(explain);
DECLARE_bool(trace_input);
DECLARE_bool(print_source);
DECLARE_bool(print_code);
DECLARE_bool(print_histogram);
DECLARE_bool(ignore_undefs);
DECLARE_bool(native);
DECLARE_bool(profile);

// We need a unlikely default value for --explain so we can
// distinguish between flag value set, and flag value set to ""
// (via --explain=)
const char* explain_default = "zlitslepmur";

// Handle --explain flag
void Explain() {
  if (FLAGS_explain == "") {
    sawzall::PrintUniverse();
  } else if (FLAGS_explain != explain_default) {
    bool found = sawzall::Explain(FLAGS_explain.c_str());
    if (!found)
      fprintf(stderr, "%s not found "
                      "(use -explain= for a list of predeclared identifiers)\n",
                      FLAGS_explain.c_str());
  }
}


void TraceStringInput(uint64 record_number, const char* input, size_t size) {
  Fmt::print("%4lld. input = %q;  # size = %d bytes\n",
             record_number, input, size);
}


void ApplyToLines(sawzall::Process* process, const char* file_name,
                         uint64 begin, uint64 end) {
  // process file
  FILE* f;

  // This special case for /dev/stdin is necessary because if szl is invoked
  // via as a subprocess our stdin may be connected to a unix domain socket
  // rather than a pipe.
  // While we can access this through the already-opened stdin, we cannot
  // fopen("/dev/stdin") when it is mapped to a unix domain socket.

  if (strcmp(file_name, "/dev/stdin") == 0) {
    f = stdin;
  } else {
    f = fopen(file_name, "r");
  }
  if (f != NULL) {
    char line[4096];
    uint64 record_number = 0;
    while (record_number < end && fgets(line, sizeof line, f) != NULL) {
      // 0-terminate if neccessary
      char* p = strchr(line, '\n');
      if (p != NULL)
        *p = '\0';
      if (begin <= record_number) {
        if (FLAGS_trace_input)
          TraceStringInput(record_number, line, strlen(line));
        string key = StringPrintf("%lld", record_number);
        process->RunOrDie(line, strlen(line), key.data(), key.size());
      }
      record_number++;
    }
    fclose(f);
  } else {
    fprintf(stderr, "can't open non-RecordIO file: ");
    perror(file_name);
  }
}


// Process FLAG_table_output expanding * into a list of all tables names.
string TableOutput(sawzall::Process* process) {
  string table_output = "";
  const vector<sawzall::TableInfo*>* tables = process->exe()->tableinfo();

  if (FLAGS_table_output == "*") {
    // construct a list of all table names
    for (int i = 0; i < tables->size(); i++) {
      if (i > 0)
        table_output += ",";
      table_output += ((*tables)[i]->name());
    }
  } else {
    // error out on unknown tables
    vector<string> table_names;
    SplitStringAtCommas(FLAGS_table_output, &table_names);
    for (int i = 0; i < table_names.size(); i++) {
      // linear search is good enough here given the (small) number of tables
      bool found = false;
      for (int j = 0; !found && j < tables->size(); j++) {
        found = (table_names[i] == (*tables)[j]->name());
      }

      if (!found) {
        LOG(ERROR) << "Unknown table name " << table_names[i]
                   << " in --table_output";
      }
    }

    table_output = FLAGS_table_output;
  }
  return table_output;
}

// Determine execution mode for Sawzall Process.
sawzall::Mode ExecMode() {
  sawzall::Mode mode = sawzall::kNormal;
  if (FLAGS_print_source)
    mode = static_cast<sawzall::Mode>(mode | sawzall::kPrintSource);
  if (FLAGS_print_code && !FLAGS_print_histogram)
    mode = static_cast<sawzall::Mode>(mode | sawzall::kDebug);
  if (FLAGS_ignore_undefs)
    mode = static_cast<sawzall::Mode>(mode | sawzall::kIgnoreUndefs);
  if (FLAGS_native)
    // flags below not supported in native mode
    return static_cast<sawzall::Mode>(mode | sawzall::kNative);
  if (FLAGS_print_histogram)
    mode = static_cast<sawzall::Mode>(mode | sawzall::kHistogram);
  if (FLAGS_profile)
    mode = static_cast<sawzall::Mode>(mode | sawzall::kProfile);
  return mode;
}
