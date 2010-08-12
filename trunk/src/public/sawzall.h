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

// This is one of a small number of top-level header files for the Sawzall
// component.  The complete list is:
//   porting.h
//   sawzall.h
//   emitterinterface.h
//   szltype.h
//   szlvalue.h
//   szlresults.h
//   szlencoder.h
//   szldecoder.h
//   szlnamedtype.h
//   value.h
// No other lower-level header files should be included by clients of the
// Sawzall implementation.


#ifndef _PUBLIC_SAWZALL_H_
#define _PUBLIC_SAWZALL_H_

#include <stdint.h>
#include <vector>

class ProtocolDB;

namespace sawzall {

// Some opaque forward declarations (they must be mentioned in the
// interface because of the way C++ works, but they are not accessible.
class CallContext;
class Compilation;
class Code;
class Debugger;
class Emitter;  // backend connection interface
class EmitterFactory;
class FunctionDecl;
class Proc;
class Process;
class Profile;
class Value;


// ----------------------------------------------------------------------------
// Global Sawzall interface and data types

// Return a version string for the Sawzall implementation.
const char* Version();

// Registers a new output table type. name must be legal Sawzall identifier,
// has_param indicates if the table requires an integer parameter, and
// has_weight indicates if the table requires a weight. Returns true if
// registration succeeded; returns false otherwise.
bool RegisterTableType(const char* name, bool has_param, bool has_weight);

// Registers all the standard table types.
void RegisterStandardTableTypes();

// Register an external protocol database (for embedded use).
void RegisterExternalProtocolDB(const ProtocolDB* db);

// Register emitters for all of exe's backend tables
void RegisterEmitters(Process* process);

// Print the names of all predeclared Sawzall identifiers.
void PrintUniverse();

// Print definition and documentation of a predeclared Sawzall identifier.
// Returns true if explanation was printed; returns false otherwise.
bool Explain(const char* name);

// Print definition and documentation of all predeclared Sawzall
// identifiers in HTML format.
void PrintHtmlDocumentation();


// ----------------------------------------------------------------------------
// Static information for an output table

class OutputType;  // internal use only, needed to satisfy C++

class TableInfo {
 public:
  // Allocation
  static TableInfo* New(Proc* proc, const char* name, OutputType* type);

  // Accessors
  const char* name() const  { return name_; }
  OutputType* type() const  { return type_; }
  const string& type_string();  // protocol buffer encoded; see sawzall.proto

  // Debugging
  void Print();  // prints table info in the form: "name: type;"

  bool is_evaluated(); // have expressions within the type been evaluated?

 private:
  const char* name_;
  OutputType* type_;
  string type_string_;
  Proc* proc_;

  // Prevent construction from outside the class (must use factory method)
  TableInfo() { /* nothing to do */ }
};


// ----------------------------------------------------------------------------
// Profile information for a Sawzall program

class ProfileInfo {
 public:
 // ticks for a given pc_index; 0 <= pc_index && pc_index < length()
 // (each pc_index represents a code interval)
  int top_ticks_at(int pc_index) const;
  int all_ticks_at(int pc_index) const;
  int length() const;

  // map a pc_index to a function index (each function index
  // represents a Sawzall function) - permits grouping of ticks: all
  // ticks with the same function index belong to the same function
  int FunctionIndex(int pc_index) const;

  // map a function index to a function name (for printing)
  const char* FunctionName(int f_index) const;

 private:
  // only Executable and Process can create a ProfileInfo
  static ProfileInfo* New(Proc* proc);
  friend class Executable;
  friend class Process;

  Profile* profile_;
  Code* code_;

  // Prevent construction from outside the class (must use factory method)
  ProfileInfo() { /* nothing to do */ }
};


// ----------------------------------------------------------------------------
// Debugger information for a Sawzall program

class DebuggerAPI {
 public:
  // Execute the program. This can be called either before the Sawzall program
  // has started, or when it is stopped after a call to Step().
  void Continue();

  // Execute to the next line, stepping into function calls. Returns false iff
  // the program has terminated (properly or with an error) and cannot be
  // continued.
  bool Step();

  int CurrentLineNumber();
  const char* CurrentFileName();
  const char* CurrentFunctionName();

  // TODO: add support for getting program stack and data

 private:
  // Prevent construction from outside the class.
  // Only Process can create DebuggerAPI.
  explicit DebuggerAPI(Proc* proc);
  friend class Process;

  Debugger* debugger_;
};


// ----------------------------------------------------------------------------
// Executable serves as abstraction for a Sawzall program.

// Operation mode
// Note: This enum must be in sync with Proc::Mode!
// (C++ doesn't allow an easy way to export Proc::Mode or
// use this Mode enum w/o breaking the Sawzall abstraction)
enum Mode {
  kNormal = 0 << 0,
  kDebug = 1 << 0,  // compiler generates extra debug information
  kHistogram = 1 << 1,  // process computes a byte code histogram
  kProfile = 1 << 2,  // process computes a profile (in interpreted mode only)
  // these modes are for internal use only
  // kPersistent = 1 << 3,  // process memory remains 'alive' over the Proc's lifetime
  // kInternal = 1 << 4,  // special process w/o stack, persistent (for initialization only)
  kNative = 1 << 5,  // compiler generates native code
  kPrintSource = 1 << 6,  // print source before running analyzer
  kIgnoreUndefs = 1 << 7,  // ignore undefs
  kDebugger = 1 << 8,  // support debugger (in interpreted mode only)
  kPipeline = 1 << 9,  // support SuperSawzall pipeline
  kPipelinePrintSource = 1 << 10,  // print SuperSawzall source before inlining
  kSecure = 1 << 11,  // Disallow subprocesses and reading any files.  Use
                      // SetDisallowedReads to specify a more limited blacklist.
  kDoCalls = 1 << 12,  // support DoCalls
};

// ErrorHandler defines the interface for custom error handlers.
class ErrorHandler {
 public:
  virtual void Report(const char* file_name, int line, int offset,
                      bool is_warning, const char* message) = 0;
  virtual ~ErrorHandler() = 0;
};

class Executable {
 public:
  // Create an executable. If source != NULL, then source is assumed
  // to be the Sawzall program to compile, and file_name is only used
  // for error messages. If source == NULL, then file_name is considered
  // to hold the names of files containing the Sawzall program to compile,
  // represented as a comma-separated list. If a custom error handler is
  // provided, it is invoked for each error encountered.  Otherwise,
  // error messages are sent to stderr.
  Executable(const char* file_name, const char* source, int mode,
             ErrorHandler* error_handler = NULL);
  virtual ~Executable();

  // Combined source of the entire original source, including inlined includes.
  // The include statements are removed and replaced with comments
  // lining out the begin and end of an included file. The source is a legal
  // Sawzall program. The source is alive for the lifetime of the executable.
  const char* RawSource();

  // Combined source of entire program, including inlined includes.
  // The source is generated by printing the compiler's syntax tree;
  // any comments, include statements, and specific formatting are
  // removed. The source is a legal Sawzall program. The source is alive
  // for the lifetime of the executable.
  const char* Source();

  // Protocol buffer attributes for Dremel.
  string InputProtoName() const;
  // Fill field_names with the names of fields in the specified tuple
  // that are referenced in the program.  If internal_fields is true,
  // include all referenced fields, otherwise, just include the scalar-valued,
  // leaf fields.
  void GetReferencedTupleFieldNames(const string& tuple_name,
                                    vector<string>* field_names,
                                    bool internal_fields) const;

  // Debugging
  void PrintSource();
  void PrintCode();
  void PrintTables();
  void PrintInputProtoName();
  // Wrapper around GetReferencedTupleFieldNames, which prints field names.
  void PrintReferencedTupleFieldNames(const string& tuple_name,
                                      bool internal_fields);

  // Profiling of native code
  // Generate an ELF file containing the native code, its symbols and line info;
  // map_beg, map_end, and map_offset (if non-NULL) are set to describe where
  // the text section of the generated ELF file would be mapped in memory;
  // these values are normally found in /proc/self/map for loaded libraries.
  // Returns true on success.
  bool GenerateELF(const char* name,
                   uintptr_t* map_beg, uintptr_t* map_end, int* map_offset);

  // Accessors
  // Note: profile and tableinfo live only as long as Executable is alive!
  const ProfileInfo* profile() const;  // NULL if there's no profile
  const vector<TableInfo*>* tableinfo() const  { return tableinfo_; }
  bool is_executable() const;
  uint64 fingerprint();                // returns the fingerprint of the source

  Compilation* compilation() { return compilation_; }

 private:
  Proc* proc_;
  Compilation* compilation_;
  vector<TableInfo*>* tableinfo_;
  uint64 fingerprint_;                 // lazily set (kIllegalFprint initially).

  // Helpers
  void MakeTables();

  friend class Process;  // needs access to proc_ and compilation_
};


// ----------------------------------------------------------------------------
// Process serves as abstraction for a runnable Sawzall program.
// It comes complete with its own state (execution stack and object heap).
//
// A Sawzall Process may be embedded within another application/service.
// The Process constructor provides a 'context' parameter, which may be
// used to pass relevant context information around (e.g. to application
// specific intrinsics).

class Process {
 public:
  // Creation
  // Note: The Executable must remain alive for the life time of the Process.
  Process(Executable* exe, void* context);
  // Obsolete version pending fixing clients.
  Process(Executable* exe, bool ignore_undefs, void* context);
  virtual ~Process();

  // Accessors & setters
  // ProfileInfo lives as long as Process is alive
  const ProfileInfo* profile() const;  // NULL if there's no profile
  Executable* exe() const  { return exe_; }
  DebuggerAPI* debugger();  // NULL if there's no debugger
  void* context() const;
  void set_memory_limit(int64 memory_limit);
  // Optional emitter factory used to install missing emitters at run-time.
  // To keep the constructor backward-compatible, can only be set with a setter
  void set_emitter_factory(EmitterFactory* emitter_factory);
  EmitterFactory* emitter_factory() const;

  uint64 ProcUndefCnt(); // get the undef cnt from proc_

  uint64 ProcProtoBytesRead();     // Total size of proto buffers read.
  uint64 ProcProtoBytesSkipped();  // Number of bytes skipped in proto buffers.
  void ProcClearProtoBytesRead();
  void ProcClearProtoBytesSkipped();

  // When running in kSecure mode, this controls which files are allowed to
  // be opened using the load and sstableopen intrinsics.
  // If the disallowed vector is empty (or never set), then disallow ALL loads.
  // If the disallowed vector is not empty, then a program may load any file
  // unless a substring of the filename is contained in this vector.
  // Do not call this when not running in kSecure mode.
  void SetDisallowedReadPaths(const vector<string>& disallowed);

  // Registration of output tables.
  // Client must register an Emitter for every output table.
  // RegisterEmitter returns true if successful, and false if
  // the table name doesn't exist. The OrDie variant exits the
  // program in case of an error.
  bool RegisterEmitter(const char* name, Emitter* emitter);
  void RegisterEmitterOrDie(const char* name, Emitter* emitter);

  // The seed for the PRNG used by the intrinsics.
  // The current time is used by default.
  void SetRandomSeed(int32 seed);

  // Incremental execution
  // Call protocol (in EBNF):
  // SetupInitialization { Execute } { SetupRun { Execute } }.
  void SetupInitialization();
  // strings here are just an abbreviation for (char *, len)
  void SetupRun(const char* input_ptr, size_t input_size,
                const char* key_ptr, size_t key_size);
  void SetupRun() { SetupRun(NULL, 0, NULL, 0); }

  // Execute() executes at most(*) max_steps instructions; it may
  // terminate earlier. In particular, no guarantee is given that max_steps
  // instructions are executed even in the absence of errors. The actual
  // number of steps executed is returned in num_steps. Execute()
  // returns true iff the program has terminated (properly or with an
  // error) and cannot be continued. In particular, while Execute()
  // returns false, program execution can be continued by calling
  // Execute() repeatedly, until it returns true. Once Execute() has
  // terminated, error_msg() should be checked: if it is != NULL,
  // execution terminated w/ an error, otherwise execution terminated
  // cleanly.
  // (*) Note that in rare cases num_steps may be slightly larger then
  // max_steps; usually by one or a couple instructions at the most.
  bool Execute(int max_steps, int* num_steps);

  // Execution
  // Call protocol (in ENBF): Initialize { Run }.
  // Initialize and Run return true if successful, and false if they terminated
  // abnormally. The OrDie variants exit the program in case of an error.
  bool Initialize();
  void InitializeOrDie();
  uint64 InitializationFingerprint() const;
  bool Run(const char* input_ptr, size_t input_size,
           const char* key_ptr, size_t key_size);
  bool Run() { return Run(NULL, 0, NULL, 0); }
  void RunOrDie(const char* input_ptr, size_t input_size,
                const char* key_ptr, size_t key_size);
  void RunOrDie() { RunOrDie(NULL, 0, NULL, 0); }
  bool RunAlreadySetup();
  // complete unfinished work.  (used for _line_counts presently)
  void Epilog(bool source);  // emit a copy of the source if true

  // --------------------------------------------------------------------------

  // Alternatively, the Sawzall interpreter can be run in a mode that
  // supports calling individual Sawzall functions instead of
  // processing input records.
  // The general pattern is the following:
  //
  // To set up the interpreter process and initialize all the Sawzall
  // functions and global variables:
  //   Executable executable("libraryOfFunctions.szl", NULL, kDoCalls);
  //   Process process(&executable, NULL);
  //   CHECK(process.InitializeDoCalls());
  // The Sawzall script (and the scripts it includes) can include
  // type, function, and other variable declarations and
  // initializations, but they should not output to any tables (assuming
  // emitters have not been installed).
  //
  // To look up a function to call:
  //   FunctionDecl* fun_decl = process.LookupFunction(function_name);
  //   CHECK(fun_decl != NULL) << "Didn't find the named function";
  //   // fun_decl can be reused across many calls
  //
  // To call a function that was previously looked up:
  //   CallContext* context = process.SetupCall();
  //   Value* args[] = {
  //     IntValue::New(context, 5),
  //     StringValue::New(context, "hi there");
  //     ...
  //   };
  //   int num_args = sizeof(args) / sizeof(args[0]);
  //   Value* result = process.DoCall(context, fun_decl, args, num_args);
  //   if (process.error_msg() != NULL) { ... }
  //   ... // use result here
  //   process.FinishCall(context);
  //   // all the argument and result Values and the context have been
  //   // deallocated now, and should not be used any more.
  //
  // Detailed specifications of the call-related functions follows.

  // Initializes the Sawzall interpreter process to allow
  // DoCall()-related operations.  Returns whether or not
  // initialization was successful.  Requires that the Executable be
  // instantiated with a mode including kDoCalls.  Currently disallows
  // kNative.  None of the other Setup, Initialize, Execute, Run, or
  // Epilog calls should be used.
  bool InitializeDoCalls();

  // Looks up a global (static or non-static) function with the given
  // name.  The result FunctionDecl is suitable for being passed in as
  // the first argument of DoCall().  The same FunctionDecl can be
  // reused across many calls.  Returns NULL if the given name doesn't
  // name a legal function, and sets error_msg() appropriately.
  // InitializeDoCalls() must have been invoked.
  const FunctionDecl* LookupFunction(const char* function_name);

  // Sets up for doing a call.  Returns a CallContext that should be
  // used to allocate the argument Values for this call, passed to the
  // DoCall(), and then cleaned up by passing it to FinishCall() once
  // all the argument and result Values are done being used with (and
  // before any other calls are started via a subsequent SetupCall()).
  // InitializeDoCalls() must have been invoked.
  CallContext* SetupCall();

  // Calls the given function on the given arguments.  On success,
  // returns the function's result (or NULL if the function returns no
  // result), sets status() == TERMINATED, and sets error_msg() ==
  // NULL.  On failure, returns NULL, sets status() == FAILED, and
  // sets error_msg() != NULL.  (A failed Sawzall function invocation
  // is not fatal for the interpreter process; it can still support
  // future DoCall() invocations just as if the Sawzall function call
  // succeeded.)  Before invoking DoCall(), the caller must first
  // invoke SetupCall() and then create any argument Value objects in
  // the CallContext it returns.  After DoCall() returns, the result
  // Value should be examined as needed, and then the caller must
  // invoke FinishCall() on the CallContext.  Only after FinishCall()
  // has been invoked can SetupCall() be invoked again.
  const Value* DoCall(CallContext* context,
                      const FunctionDecl* fun_decl,
                      const Value* const* args,
                      int num_args);

  // StartCall/ContinueCall allow calling a single function with a
  // bounded number of execution steps.  Call StartCall rather than
  // DoCall to initialize the call context.  StartCall does not execute
  // any steps.  Call ContinueCall to execute at most max_steps
  // instructions.  As with Execute(), the actual number of steps
  // executed is returned in num_steps and it may be slightly larger
  // than max_steps; usually by one or a few instructions at the most.
  // ContinueCall returns true iff the call has finished (properly or
  // with an error) and cannot be continued.  error_msg() should be
  // checked: if it is != NULL, execution terminated with an error,
  // otherwise execution terminated successfully.  A successful call's
  // return Value is stored in result.  If the function has no return
  // value, NULL is stored in result.
  //
  // To use StartCall/ContinueCall, initialize a Process and CallContext as
  // shown above for DoCall and then:
  //    Value* result = NULL;
  //    int max_steps = 1000;
  //    int num_steps = 0;
  //    process.StartCall(context, fun_decl, args, num_args);
  //    while (!process.ContinueCall(context, max_steps, &num_steps, &result)) {
  //        ... optionally allow more steps
  //    }
  //    if (process.error_msg() != NULL) { ... handle call failure }
  //    ... use result here
  //    process.FinishCall(context);
  //
  void StartCall(CallContext* context,
                 const FunctionDecl* fun_decl,
                 const Value* const* args,
                 int num_args);

  bool ContinueCall(CallContext* context,
                    int max_steps,
                    int* num_steps,
                    const Value** result);

  // Completes a call.  Should be invoked after all Value argument and
  // result objects are done being used.  Deallocates all the argument
  // and result Value objects and the CallContext object.  None of
  // these values can be used after FinishCall() is invoked.  Enables
  // SetupCall() to be invoked again.
  void FinishCall(CallContext* context);

  // --------------------------------------------------------------------------

  // Error message returned by Execute, Initialize, or Run in case
  // of abnormal termination.
  const char* error_msg() const;

  Proc* proc() { return proc_; }

  // Handling of per-Process environment variables. Allows having per-thread
  // environment variables - e.g. multiple szl mapper threads processing
  // different input files - each with corresponding SZL_INPUT environment
  // variable value.
  void set_env_value(const string& name, const string& value);
  const char* env_value(const string& name) const;
  void clear_env_values();

 private:
  Proc* proc_;
  Executable* exe_;

  // helper functions
  void DieIfFalse(bool b);

  // The legal state transitions for DoCallState are:s
  //
  // UNINITIALIZED => SetupInitialization() => ILLEGAL
  // UNINITIALIZED => InitializeDoCalls() => INITIALIZED
  // INITIALIZED, FINISHED => SetupCall() => SETUP
  // SETUP => DoCall() => CALLED
  // SETUP => StartCall() => STARTED
  // STARTED, CONTINUED => ContinueCall() => CONTINUED
  // SETUP, CALLED, STARTED, CONTINUED => FinishCall() => FINISHED
  //
  enum DoCallState {
    UNINITIALIZED = 0,  // The initially constructed, uninitialized state
    ILLEGAL,            // DoCalls operations disallowed
    INITIALIZED,        // InitializeDoCalls() invoked
    SETUP,              // SetupCall() invoked
    CALLED,             // DoCall() invoked
    STARTED,            // StartCall() invoked
    CONTINUED,          // ContinueCall() invoked
    FINISHED,           // FinishCall() invoked
  };
  DoCallState do_call_state_;
};

}  // namespace sawzall

#endif // _PUBLIC_SAWZALL_H_
