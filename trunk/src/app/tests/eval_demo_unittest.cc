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

// A demo program to illustrate stand-alone Sawzall usage;
// formulated as a unit test. Covers both good and bad scenarios
// given each evaluation technique.

#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "public/porting.h"
#include "public/logging.h"

#include "public/sawzall.h"
#include "public/emitterinterface.h"
#include "public/nullemitter.h"

// Sample Sawzall programs - will pass or fail depending on evaluation methods.

// Takes a bytes (= C++ string) input, converts it into a float r,
// computes the circumference of a circle with radius r, and prints
// the result to stdout.
const char program1[] =
  "r: float = string(input);"
  "if (!def(r))"
  "  return;"
  "emit stdout <- format(\"r=%g, c=%g\", r, 2.0 * r * PI);  # uses no emitter";

// Takes a bytes (= C++ string) input, assumes it is a list of integers,
// computes the product of the numbers, and returns the result via table t.
// For this to work an emitter has to be registered for t.
const char program2[] =
  "t: table collection of bytes;"
  "a: array of int = sawzall(string(input), regex(int));"
  "p: int = 1;"
  "for (i: int = 0; i < len(a); i++)"
  "  p = p * a[i];"
  "emit t <- bytes(string(p));";

// Ignores input. Declares a variable, then assigns an undef value to it.
// This generates implicit emits to built-in tables for undef reporting.
const char program3[] =
  "i: int;"
  "i = 1 / 0;";

// Ignores input. Declares and emits to a sample table with an expression that
// requires run-time evaluation as a parameter. Note that sample type needs
// to be explicitely registered for the parser to recognize it.
const char program4[] =
  "t: table sample(min(1, int(now()))) of bytes;"
  "emit t <- B\"1\";"
  "emit t <- B\"2\";"
  "emit t <- B\"3\";";

// This emits a random value, to test that calling SetRandomSeed makes
// the random intrinsics deterministic
const char program5[] =
  "t: table collection of bytes;"
  "emit t <- bytes(string(nrand(1000)));";

// This program tests the getenv intrinsic.
const char program6[] =
    "assert(getenv(\"MY_ENV_VAR\") == \"myval\");";

// -----------------------------------------------------------------------------
// The simple, most direct usage of the Sawzall interpreter with no emitters
// is suitable for executing programs without emits to tables that
// require emitters. Explicit emits to such tables (user defined or built in)
// will cause fatal errors while implicit emits to built-in tables used for
// undef tracking will be ignored.

static string Evaluate(const char* program_name, const char* source,
                       const char* input) {
  // compile program
  sawzall::Executable exe(program_name, source, sawzall::kNormal);
  if (!exe.is_executable())
    return "compilation error";
  // run program
  sawzall::Process process(&exe, false, NULL);
  process.InitializeOrDie();
  if (!process.Run(input, strlen(input), NULL, 0))
    return process.error_msg();
  return "no error";
}

static void Example1() {
  string error;

  error = Evaluate("p1", program1, "2.18");
  CHECK(error == "no error");

  error = Evaluate("p2", program2, "1 2 3");
  CHECK(error == "no emitter installed for table t; cannot emit");

  error = Evaluate("p3", program3, "");
  CHECK(error ==
        "undefined value at p3:1: i = 1 / 0 (divide by zero error: 1 / 0)");

  error = Evaluate("p4", program4, "");
  CHECK(error == "compilation error");
}


// -----------------------------------------------------------------------------
// A more elaborate example that registers emitters and tests error conditions.

// A very primitive Emitter implementation that can only deal with Sawzall
// bytes (= C++ strings), strings and ints. Simply collects all bytes/string
// values emitted into a single string and appends "#" for int emits.
// Arbitrary complicated data types can be supported by implementing the other
// Emitter functions, or by using proto buffers.
//
// Note: In Example2, we use a single instance of DemoEmitter!
// (i.e., emits into all tables end up in a single emitter).
class DemoEmitter: public sawzall::Emitter {
 public:
  DemoEmitter(): s_("") {}
  void Begin(GroupType type, int len)     {}  // will be called for each element
  void End(GroupType type, int len)       {}  // will be called for each element
  void PutBool(bool b)                    { NotImplemented("bool"); }
  void PutBytes(const char* p, int len)   { s_.append(p, len); }
  void PutInt(int64 i)                    { s_.append("#", 1); }
  void PutFloat(double f)                 { NotImplemented("float"); }
  void PutFingerprint(uint64 fp)          { NotImplemented("fingerprint"); }
  void PutString(const char* s, int len)  { s_.append(s, len); }
  void PutTime(uint64 t)                  { NotImplemented("time"); }

  void EmitInt(int64 i)                   { PutInt(i); }
  void EmitFloat(double f)                { PutFloat(f); }

  // The concatenation of all bytes emitted.
  string result() const { return s_; }

 private:
  void NotImplemented(const char* type) {
    LOG(FATAL) << "no emitter support for " << type << " values yet";
  }
  string s_;
};

static string EvaluateAndLog(const char* program_name, const char* source,
                             const char* input, string* result) {
  *result = "no result";
  // register additional table type for the parser to recognize
  CHECK(sawzall::RegisterTableType("sample", true, false));
  // compile program
  sawzall::Executable exe(program_name, source, sawzall::kNormal);
  if (!exe.is_executable()) {
    LOG(ERROR) << "could not compile " << program_name;
    return "compilation error";
  }
  // create a Sawzall process and register an
  // emitter for each table in the program
  sawzall::Process process(&exe, false, NULL);
  // for simplicity, we use the same emitter for all tables!
  // (thus, you may have many tables in the program but all
  // emits go into the same emitter - to change that behaviour,
  // install different emitters for different tables)
  DemoEmitter emitter;
  const vector<sawzall::TableInfo*>* tables = exe.tableinfo();
  for (int i = 0; i < tables->size(); i++) {
    VLOG(1) << "registering emitter for table " << (*tables)[i]->name();
    process.RegisterEmitter((*tables)[i]->name(), &emitter);
  }
  // you can specify a seed if you want the random intrinsics to return
  // deterministic values.
  process.SetRandomSeed(1234567);
  // run program
  if (!process.Initialize()) {
    LOG(ERROR) << "could not initialize " << program_name;
    return process.error_msg();
  }
  if (!process.Run(input, strlen(input), NULL, 0)) {
    LOG(ERROR) << "could not successfully execute " << program_name;
    *result = emitter.result();
    return process.error_msg();
  }
  // done
  *result = emitter.result();
  return "no error";
}

static void Example2() {
  string error;
  string result;

  error = EvaluateAndLog("p1", program1, "", &result);
  CHECK(error == "no error" && result == "");

  error = EvaluateAndLog("p2", program2, "2 3 5 7 11", &result);
  CHECK(error == "no error" && result == "2310");

  error = EvaluateAndLog("p3", program3, "", &result);
  CHECK(error ==
        "undefined value at p3:1: i = 1 / 0 (divide by zero error: 1 / 0)"
        && result == "#p3:1: i = 1 / 0 (divide by zero error: 1 / 0)#");

  error = EvaluateAndLog(program4, program4, "", &result);
  CHECK(error ==
        "parameter 'min(1, convert(int, now()))' must be a constant expression"
        && result == "no result");

  // "829" is the value of nrand(1000) with the seed 1234567 and Sawzall's
  // current pseudorandom-number implementation.
  error = EvaluateAndLog("p5", program5, "", &result);
  CHECK(error == "no error" && result == "829");
}


// -----------------------------------------------------------------------------
// An example that delays emitter registration until run-time static
// initialization by supplying an emitter factory - this enables run-time table
// parameter evaluation, not available otherwise. Also enables ignore_undefs.

class DemoEmitterFactory : public sawzall::EmitterFactory {
 public:
  DemoEmitterFactory() {}
  sawzall::Emitter* NewEmitter(sawzall::TableInfo* table_info, string* error) {
    return &emitter_;
  }

  string result() const { return emitter_.result(); }

 private:
  // for simplicity, we use the same emitter for all tables!
  // (thus, you may have many tables in the program but all
  // emits go into the same emitter - to change that behaviour,
  // install different emitters for different tables)
  DemoEmitter emitter_;
};

static void EvaluateOrDie(const char* program_name, const char* source,
                          const char* input, string* result) {
  // register additional table type for the parser to recognize
  CHECK(sawzall::RegisterTableType("sample", true, false));
  // compile program
  sawzall::Mode mode = static_cast<sawzall::Mode>(sawzall::kNormal |
                                                  sawzall::kIgnoreUndefs);
  sawzall::Executable exe(program_name, source, mode);
  CHECK(exe.is_executable());
  // initialize run-time environment
  sawzall::Process process(&exe, true, NULL);
  // register factory to install emitters during static initialization
  DemoEmitterFactory ef;
  process.set_emitter_factory(&ef);
  // run static initialization
  process.InitializeOrDie();
  // run main code (with empty input)
  process.RunOrDie(input, strlen(input), NULL, 0);
  // record the result emitted
  *result = ef.result();
}

static void Example3() {
  string result;

  EvaluateOrDie("p1", program1, "2.18", &result);
  CHECK(result == "");

  EvaluateOrDie("p2", program2, "2 3 5 7 11", &result);
  CHECK(result == "2310");

  EvaluateOrDie("p3", program3, "", &result);
  CHECK(result == "#p3:1: i = 1 / 0 (divide by zero error: 1 / 0)#");

  EvaluateOrDie("p4", program4, "", &result);
  CHECK(result == "123");
}

// -----------------------------------------------------------------------------
// An example that uses an already available NullEmitterFactory that can execute
// all the same code as the example above, but does not care about emitted data.

static void EvaluateOrDie2(const char* program_name, const char* source,
                           const char* input) {
  // compile program
  CHECK(sawzall::RegisterTableType("sample", true, false));
  sawzall::Mode mode = static_cast<sawzall::Mode>(sawzall::kNormal |
                                                  sawzall::kIgnoreUndefs);
  sawzall::Executable exe(program_name, source, mode);
  CHECK(exe.is_executable());
  // run program
  sawzall::Process process(&exe, true, NULL);
  NullEmitterFactory ef;
  process.set_emitter_factory(&ef);
  process.InitializeOrDie();
  process.RunOrDie(input, strlen(input), NULL, 0);
}

static void Example4() {
  EvaluateOrDie2("p1", program1, "2.18");
  EvaluateOrDie2("p2", program2, "2 3 5 7 11");
  EvaluateOrDie2("p3", program3, "");
  EvaluateOrDie2("p4", program4, "");
}

// -----------------------------------------------------------------------------
// An example that uses kSecure mode to limit what the application can do.

static string EvaluateOrDie3(const char* program_name, const char* source,
                             sawzall::Mode mode,
                             const string& set_disallowed_path) {
  // compile program
  sawzall::Executable exe(program_name, source, mode);
  if (!exe.is_executable())
    return "compilation error";
  // run program
  sawzall::Process process(&exe, false, NULL);
  // The default is to block everything.
  if (!set_disallowed_path.empty()) {
    vector<string> disallowedPaths;
    disallowedPaths.push_back(set_disallowed_path);
    process.SetDisallowedReadPaths(disallowedPaths);
  }
  process.InitializeOrDie();
  if (!process.Run())
    return process.error_msg();
  return "no error";
}


void Example5() {
  // Create a temporary file for load() to see.
  const char* dir = getenv("SZL_TMP");
  if (dir == NULL)
    dir = "/tmp";
  string tmpfilename = string(dir) + "/szltempforloadtest";
  FILE* tmpfile = fopen(tmpfilename.c_str(), "w");
  CHECK(tmpfile != NULL);
  CHECK(fclose(tmpfile) == 0);
  string loadtmpfile = string("load(\"") + tmpfilename + "\");";
  string error;

  // This program is legal in kSecure mode only if SetDisallowedReadPaths is
  // invoked and doesn't explicitly disallow the path.
  error = EvaluateOrDie3("secure1", loadtmpfile.c_str(), sawzall::kNormal, "");
  CHECK(error == "no error") << error;
  error = EvaluateOrDie3("secure1", loadtmpfile.c_str(),
                         sawzall::kSecure, "/baddir/");
  CHECK(error == "no error") << error;
  remove(tmpfilename.c_str());

  // In kSecure mode, reading a file matching a pattern can be blocked both
  // by the default "block everything" policy and by the more specific
  // disallowedPaths used in the first test below.
  error = EvaluateOrDie3("secure2", "load(\"/any/path/baddir/more\");",
                         sawzall::kSecure, "/baddir/");
  CHECK(error ==
         "undefined value at secure2:1: load(\"/any/path/baddir/more\") "
         "(file paths containing \"/baddir/\" may not be read in this context)")
      << error;
  error = EvaluateOrDie3("secure2", "load(\"/any/path/baddir/more\");",
                         sawzall::kSecure, "");
  CHECK(error ==
         "undefined value at secure2:1: load(\"/any/path/baddir/more\")"
         " (file reads are disallowed in this context)") << error;

  // In kSecure mode, file and proc outputs may not be used.
  const char program_proc[] =
      "type proc_table = table collection of e: bytes proc(\"echo hello\");";
  error = EvaluateOrDie3("secure3", program_proc, sawzall::kNormal, "");
  CHECK(error == "no error") << error;
  error = EvaluateOrDie3("secure3", program_proc, sawzall::kSecure,
                         "/baddir/");
  CHECK(error == "compilation error") << error;
}

// -----------------------------------------------------------------------------
// An example for per-process environment variables.

// Similar to Evaluate but allows you to set a per-process environment
// variable through the env_var_name and env_var_val parameters.
static string EvaluateEnvVar(const char* program_name, const char* source,
                             const char* env_var_name,
                             const char* env_var_val) {
  // compile program
  sawzall::Executable exe(program_name, source, sawzall::kNormal);
  if (!exe.is_executable())
    return "compilation error";
  // run program
  sawzall::Process process(&exe, false, NULL);
  // Set the environment variable if specified.
  if (env_var_name != NULL)
    process.set_env_value(env_var_name, env_var_val);
  process.InitializeOrDie();
  if (!process.Run())
    return process.error_msg();
  return "no error";
}

static void Example6() {
  string error;

  // Test the success case - we set a process-local environment variable and
  // can read it back.
  error = EvaluateEnvVar("p1", program6, "MY_ENV_VAR", "myval");
  CHECK_EQ("no error", error);

  // Test the failure case - we have not set the environment variable
  // and attempt to read it back.
  error = EvaluateEnvVar("p2", program6, "", "");
  CHECK_EQ("undefined value at p2:1: assert(getenv(\"MY_ENV_VAR\") == "
           "\"myval\") (getenv: environment variable \"MY_ENV_VAR\" undefined)",
           error);

  // Test the case where we set a global environment variable, and not
  // per-Process one - no failure.
  setenv("MY_ENV_VAR", "myval", 1);
  error = EvaluateEnvVar("p3", program6, NULL, NULL);
  CHECK_EQ("no error", error);

  // Test the case where the Process environment variable overwrites the
  // global one.
  setenv("MY_ENV_VAR", "myval_other", 1);
  error = EvaluateEnvVar("p4", program6, "MY_ENV_VAR", "myval");
  CHECK_EQ("no error", error);
}

// -----------------------------------------------------------------------------

int main(int argc, char **argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  Example1();
  Example2();
  Example3();
  Example4();
  Example5();
  Example6();

  printf("PASS\n");
  return 0;
}
