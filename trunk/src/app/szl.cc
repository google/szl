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

// We need PRIu64, which is only defined if we explicitly ask for it.
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <stdio.h>
#include <algorithm>
#include <string>
#include <vector>
#include <set>
#include <list>
#include <utility>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <regex.h>
#include <string.h>

#include "config.h"

#include "public/hash_set.h"
#include "public/hash_map.h"

#include "public/porting.h"
#include "public/commandlineflags.h"
#include "public/logging.h"
#include "public/recordio.h"

#include "utilities/strutils.h"
#include "fmt/fmt.h"

#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/sawzall.h"
#include "public/emitterinterface.h"
#include "public/szlemitter.h"
#include "public/szltabentry.h"

#include "app/szlemitterfactory.h"
#include "app/printemitter.h"
#include "app/szlutils.h"

DEFINE_bool(V, false, "print version");

// Special flag: if --program is set, next arg is .szl file.
// If not, the first non-option argument is the .szl file.
DEFINE_string(program, "", "sawzall source file.  If the file is not found in "
                           "the current directory, look for it in "
                           "--szl_includepath");

// szl flags
DEFINE_bool(execute, true, "execute program");
DEFINE_bool(skip_files, false, "skip processing of input files");
DEFINE_bool(print_source, false, "print program source");
DEFINE_bool(print_raw_source, false, "print raw program source");
DEFINE_bool(always_print_raw_source, false, "print raw program source");
DECLARE_bool(print_rewritten_source);
DEFINE_bool(print_code, false, "print generated code");
DEFINE_bool(trace_files, false, "trace input files");
DEFINE_bool(trace_input, false, "trace input records");
DEFINE_bool(use_recordio, false, "use record I/O to read input files");
DEFINE_bool(ignore_undefs, false,
            "silently ignore undefined variables/statements");
DEFINE_bool(info, false, "print Sawzall version information");
DEFINE_int64(begin_record, 0, "first record to process");
DEFINE_int64(end_record, -1, "first record not to process (-1 => end of file)");
DEFINE_int64(num_records, -1, "number of input records to process (-1 => all)");
DEFINE_string(e, "", "program snippet on command line");
DEFINE_string(explain, explain_default,
              "print definition of a predeclared identifier");
DEFINE_bool(print_html, false, "print html documentation");
DEFINE_bool(print_histogram, false, "print byte code histogram for each process");
DEFINE_bool(print_tables, false, "print output tables");
DEFINE_bool(print_input_proto_name, false,
            "print the name of the protocol buffer associated with \"input\"");
DEFINE_string(print_referenced_tuple_field_names, "",
            "print the names of the referenced fields in the specified tuple; "
            "use \"<input>\" to specify the input proto tuple and \"<all>\" "
            "to specify all named tuples");
DEFINE_bool(profile, false, "print function use profile for each process");
DEFINE_bool(native, true,
            "generate native code instead of interpreted byte code");
DEFINE_string(gen_elf, "",
              "generate ELF file representing generated native code");
DEFINE_string(table_output, "", "comma-separated list of table names or * to "
              "display the aggregated output for.");

#ifdef OS_LINUX
DEFINE_int32(memory_limit, 0,
             "memory limit in MB (0 is size of RAM, -1 is unlimited); memory "
             "manager will reclaim memory to try to stay below this limit");
#endif


static void TraceBinaryInput(uint64 record_number, const void* input, size_t size) {
  Fmt::print("%4"PRIu64". input = bytes({", record_number);
  for (int i = 0; i < size; i++) {
    if (i > 0)
      Fmt::print(", ");
    Fmt::print("%02x", static_cast<const char*>(input)[i]);
  }
  Fmt::print("});  # size = %d bytes\n", size);
}


static void ApplyToRecords(sawzall::Process* process, const char* file_name,
                           uint64 begin, uint64 end) {
  // TODO: support sequence file input
  assert(false);
  sawzall::RecordReader* reader = sawzall::RecordReader::Open(file_name);
  if (reader != NULL) {
    uint64 record_number = 0;
    char* record_ptr;
    size_t record_size;
    while (record_number < end && reader->Read(&record_ptr, &record_size)) {
      if (begin <= record_number) {
        if (FLAGS_trace_input)
          TraceBinaryInput(record_number, record_ptr, record_size);
        string key = StringPrintf("%"PRIu64, record_number);
        process->RunOrDie(record_ptr, record_size, key.data(), key.size());
      }
      record_number++;
    }
    if (!reader->error_message().empty())
      fprintf(stderr, "error reading file: %s: %s\n",
                      file_name,
                      reader->error_message().c_str());
    delete reader;
  } else {
    fprintf(stderr, "can't open file: ");
    perror(file_name);
  }
}


static bool Execute(const char* program, const char* cmd,
                    int argc, char* argv[], uint64 begin, uint64 end) {
  sawzall::Executable exe(program, cmd, ExecMode());

  if (FLAGS_always_print_raw_source)
    Fmt::print("%s\n", exe.RawSource());

  // do not execute if there were compilation errors
  if (!exe.is_executable())
    return false;

  // debugging output
  if (FLAGS_print_raw_source && !FLAGS_always_print_raw_source)
    Fmt::print("%s\n", exe.RawSource());
  if (FLAGS_print_rewritten_source)
    exe.PrintSource();  // see DoCompile for the pre-rewrite source printing
  if (FLAGS_print_code)
    exe.PrintCode();
  if (FLAGS_print_tables)
    exe.PrintTables();
  if (FLAGS_native && !FLAGS_gen_elf.empty()) {
    if (!exe.GenerateELF(FLAGS_gen_elf.c_str(), NULL, NULL, NULL)) {
      fprintf(stderr, "could not write elf file %s\n", FLAGS_gen_elf.c_str());
      return false;
    }
  }
  if (FLAGS_print_input_proto_name)
    exe.PrintInputProtoName();
  if (!FLAGS_print_referenced_tuple_field_names.empty())
    exe.PrintReferencedTupleFieldNames(FLAGS_print_referenced_tuple_field_names,
                                       true);

  // execute the program
  if (FLAGS_execute) {
    sawzall::Process process(&exe, NULL);
#ifdef OS_LINUX
    process.set_memory_limit(FLAGS_memory_limit);
#endif

    // set up print output buffer
    Fmt::State fmt;
    char buf[1024];
    Fmt::fmtfdinit(&fmt, 1, buf, sizeof buf);

    // register backend emitters for tables
    SzlEmitterFactory emitter_factory(&fmt, TableOutput(&process));
    process.set_emitter_factory(&emitter_factory);
    sawzall::RegisterEmitters(&process);

    process.InitializeOrDie();

    // run for each input line, if any
    if (argc > 0) {
      // we have an input file
      // => run the Sawzall program for all lines in each file
      for (int i = 0; i < argc; i++) {
        const char* file_name = argv[i];
        if (FLAGS_skip_files) {
          printf("%d. skipping %s\n", i, file_name);
        } else {
          if (FLAGS_trace_files)
            printf("%d. processing %s\n", i, file_name);
          if (FLAGS_use_recordio)
            ApplyToRecords(&process, file_name, begin, end);
          else
            ApplyToLines(&process, file_name, begin, end);
        }
      }
    } else {
      // we have no input file
      // => run the Sawzall program once
      process.RunOrDie("", 0, "", 0);
    }

    // cleanup
    process.Epilog(true);
  }
  return true;
}


int main(int argc, char* argv[]) {
  Fmt::quoteinstall();  // For TraceStringInput

  // save the current directory so we can restore it
  char pre_init_directory[PATH_MAX + 1];
  CHECK(getcwd(pre_init_directory, sizeof(pre_init_directory)) != NULL);

  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  if (FLAGS_V)
    fprintf(stderr, "Szl version %d.%d.%d\n",
            GOOGLE_SZL_VERSION/1000000, GOOGLE_SZL_VERSION/1000%1000);
  // check if the directory changed; if so, complain and restore it
  char post_init_directory[PATH_MAX + 1];
  CHECK(getcwd(post_init_directory, sizeof(post_init_directory)) != NULL) <<
    "getcwd() failed - unable to get current directory";
  if (strcmp(pre_init_directory, post_init_directory) != 0) {
    LOG(ERROR) << "Current directory was changed to \"" << post_init_directory
               << "\" and will be restored to \"" << pre_init_directory << "\"";
    CHECK_EQ(chdir(pre_init_directory), 0) <<
      "chdir() failed - unable to restore current directory";
  }

  // Set the job start time.
  timeval tv;
  gettimeofday(&tv, NULL);
  setenv("SZL_START_TIME",
         StringPrintf("%lld",
                      static_cast<int64>(tv.tv_sec * 1e6 + tv.tv_usec)).c_str(),
         0);   // Do not override SZL_START_TIME if it's already present.

  sawzall::RegisterStandardTableTypes();

  // process some command line flags
  if (FLAGS_info)
    printf("szl using %s\n", sawzall::Version());

  if (FLAGS_explain != explain_default) {
    Explain();
    return 0;
  }

  if (FLAGS_print_html)
    sawzall::PrintHtmlDocumentation();

  // determine file interval
  uint64 begin = FLAGS_begin_record;
  uint64 end = FLAGS_end_record;
  if (FLAGS_num_records != -1) {
    if (FLAGS_end_record != -1) {
      // cannot set both flags at the same time
      Fmt::fprint(
          2, "cannot use --end_record and --num_records at the same time\n");
      return 1;
    }
    end = begin + FLAGS_num_records;
  }

  // process extra argument or --e arg or --program arg as szl program
  --argc;
  ++argv;  // step over argv[0]; our args start at argv[1]
  const char* program = FLAGS_program.c_str();
  const char* ecommand = FLAGS_e.c_str();
  if (strlen(ecommand) > 0) {
    if (strlen(program) > 0) {
      Fmt::fprint(2, "cannot use --e and --program at the same time\n");
      return 1;
    }
    program = "<commandline>";
  } else {
    ecommand = NULL;
    if (strlen(program) == 0) {
      if (argc < 1)
        return 0;  // nothing to run
      program = *argv;
      --argc;
      ++argv;
    }
  }

  bool success = Execute(program, ecommand, argc, argv, begin, end);

  if (success)
    return 0;
  return 1;
}
