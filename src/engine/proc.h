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

#include <map>
#include <string>
#include <vector>

class SzlACMRandom;

namespace sawzall {

typedef List<TableInfo*> OutputTables;
class TrapDesc;
class EmitterFactory;
class ErrorHandler;

class ResourceStats {
  // Helper class to manage run-time statistics.
 public:
  ResourceStats(Proc* proc);

  void Update();

  size_t available_mem() const  { return available_mem_; }
  size_t allocated_mem() const  { return allocated_mem_; }
  szl_time user_time() const  { return user_time_; }
  szl_time system_time() const  { return system_time_; }

 private:
  Proc* proc_;
  // The statistics
  size_t available_mem_;
  size_t allocated_mem_;
  szl_time user_time_;
  szl_time system_time_;
};


// An interface for data cache for individual intrinsics. Each intrinsic can
// extend this interface for customized storage and register the cache with
// Proc object.
class IntrinsicCache {
 public:
  virtual ~IntrinsicCache() {}
};


class Proc {
 public:
  // Operation mode
  // Note: If this enum changes, update the corresponding
  // enum in sawzall.h!
  // (C++ doesn't allow an easy way to export Proc::Mode or
  // use this Mode enum w/o breaking the Sawzall abstraction)
  enum Mode {
    kNormal = 0 << 0,
    kDebug = 1 << 0,  // compiler generates extra debug information
    kHistogram = 1 << 1,  // process computes a byte code histogram
    kProfile = 1 << 2,  // process computes a profile
    // these modes are for internal use only
    kPersistent = 1 << 3,  // process memory remains 'alive' over the Proc's lifetime
    kInternal = 1 << 4,  // special process w/o stack, persistent (for initialization only)
    kNative = 1 << 5,  // compiler generates native code
    kPrintSource = 1 << 6,  // print source before running analyzer
    kIgnoreUndefs = 1 << 7,  // ignore undefs
    kDebugger = 1 << 8,  // support debugger
    kPipeline = 1 << 9,  // support SuperSawzall pipeline
    kPipelinePrintSource = 1 << 10,  // print SuperSawzall source before inlining
    kSecure = 1 << 11,  // Disallow subprocesses and loading certain files
    kDoCalls = 1 << 12,  // support DoCalls
  };

  // Has completed static initialization
  bool is_initialized() const  { return initialized_; }

  void set_calls_getresourcestats() { calls_getresourcestats_ = true; }

  // Execution status
  //
  //   RUNNING => via Execute() => SUSPENDED | TRAPPED | TERMINATED | FAILED
  //   TRAPPED => via Execute() => SUSPENDED | FAILED
  //   SUSPENDED => Execute() => RUNNING
  //   TERMINATED | FAILED => SetupInitialization() | SetupRun() => SUSPENDED
  //
  // Note: The states RUNNING and TRAPPED cannot be observed outside Proc.
  enum Status {
    // Note: enumeration order is relevant!
    RUNNING,  // process is running (i.e. in Engine::Execute)
    SUSPENDED,  // process was suspended and is not running
    TRAPPED,  // process encountered a trap and is not running
    TERMINATED,  // process has terminated cleanly
    FAILED  // process has terminated with an execution error
  };

  // Construction/Destruction
  Proc(int mode, ErrorHandler* error_handler);
  ~Proc();

  // Fork the Proc (use a different stack & heap)
  // - status must be TERMINATED (for now)
  // - new mode bits can be added/removed
  Proc* Fork(int mode) const;

  // Incremental execution
  // Call protocol (in EBNF):
  // SetupInitialization { Execute } { SetupRun { Execute } }.
  void SetupInitialization();
  void SetupRun(const char* input_ptr, size_t input_size,
                const char* key_ptr, size_t key_size);
  szl_fingerprint InitializationFingerprint();

  // Execute() executes at most(*) max_steps instructions; it may
  // terminate earlier. In particular, no guarantee is given that max_steps
  // instructions are executed even in the absence of errors. The actual
  // number of steps executed is returned in num_steps. The result value
  // is the execution state, which is one of the following 3 states:
  //
  //   SUSPENDED   executed suspended, can be continued by calling Execute again
  //   TERMINATED  program terminated w/o error, cannot be continued
  //   FAILED      execution failed w/ an error, cannot be continued
  //
  // (*) Note that in rare cases num_steps may be slightly larger than
  // max_steps; usually by one or a couple instructions at the most.
  Status Execute(int max_steps, int* num_steps);

  // Looks up a global (static or non-static) function with the given
  // name, returning its VarDecl.  This VarDecl is suitable for being
  // passed in as the first argument of DoCall().  Returns NULL if the
  // given name doesn't name a legal function, and sets error_msg()
  // appropriately.
  const VarDecl* LookupFunction(const char* function_name);

  // Call protocol for DoCall-related operations (in EBNF):
  //   SetupInitialization Execute
  //     { SetupCall <alloc arg Vals> DoCall <use result Val> FinishCall }.

  // Sets up for doing a call.  Must be invoked before any Val*
  // argument objects are allocated.
  // Assumes Execute has been completed (which itself requires that
  // SetupIniitalization has been done).
  // Requires that the Executable be instantiated with a mode
  // including kDoCalls.  Currently disallows kNative.
  void SetupCall();

  // Calls the given function, which must be global (static or
  // non-static), on the given arguments.  On success, DoCall()
  // returns the function's result (or NULL if the function returns no
  // result), status() == TERMINATED, and error_msg() == NULL.  On
  // failure, DoCall() returns NULL, status() == FAILED, and
  // error_msg() != NULL.  (A failed Sawzall function invocation is
  // not fatal for the interpreter process; it can still support
  // future DoCall() invocations just as if the Sawzall function call
  // succeeded.)  Before invoking DoCall(), the caller must first
  // invoke SetupCall() and then create any Val argument objects.
  // After DoCall() returns, and before invoking SetupCall() again,
  // the caller must decrement the reference counts (by calling
  // Val::dec_ref()) of the argument Val objects passed in to DoCall()
  // and the result Val object returned by DoCall(), and then invoke
  // FinishCall().
  Val* DoCall(const VarDecl* fun_decl, Val* args[], int num_args);

  // StartCall/ContinueCall allow calling a single function with a
  // bounded number of execution steps.  Call StartCall rather than
  // DoCall to initialize the call context.
  // StartCall initializes state_ for a call of the function given
  // in fun_decl, with the arguments specified in args.  Prior to calling
  // StartCall, status() should be SUSPENDED.  Afterward, status() will be
  // FAILED if some inconsistency is detected, or SUSPENDED if everything
  // is OK.
  void StartCall(const VarDecl* fun_decl, Val* args[], int num_args);

  // ContinueCall executes at most max_steps of a function call
  // previously initialized by StartCall.  Before calling
  // ContinueCall, status() should be SUSPENDED or RUNNING.  If the
  // function terminates within max_steps, then status() will be
  // TERMINATED and the function's return Val*, if any, will be
  // returned from ContinueCall.  If the function runs normally, but
  // does not terminate, then status() will be RUNNING, the return
  // value will be NULL and you may call ContinueCall again.  If a
  // trap, failure or suspension occurs within max_steps, status()
  // will be set accordingly and the return value will be NULL.
  Val* ContinueCall(int max_steps, int* num_steps);

  // Completes a call.  Should be invoked after all Val* argument and
  // result objects are done being used, and had their reference
  // counts decremented via Val::dec_ref().  Deallocates the
  // Sawzall-related memory used between SetupCall() and FinishCall().
  void FinishCall();

  // Execution status
  void set_error()  { status_ = FAILED; }
  Status status() const  { return status_; }
  const char* error_msg() const  { return trap_info_; }  // if status == FAILED
  Instr* pc()  { return state_.pc_; }

  // Support for error handling, debug messages, etc.
  // - allocates a string (char*) on the Proc heap
  // - no direct changes to the Proc state (except for allocation)
  char* PrintString(const char* fmt, ...);
  char* CopyString(const char* s);

  // Support for error messages using a persistent buffer instead of the heap.
  // See error_messages_ below: strings from the the last
  // kNumErrorMessageBuffers calls to PrintError will be valid at any
  // one time.
  char* PrintError(const char* fmt, ...);

  // Attributes
  int mode() const  { return mode_; }
  const char* name() const  { return name_; }
  void set_name(const char* name)  { name_ = name; }
  void set_memory_limit(int64 limit)  { heap_->set_memory_limit(limit); }

  // Context (access to embedding app/service)
  void* context() const  { return context_; }
  void set_context(void* context)  { context_ = context; }

  // Handling of per-Proc environment variables. Allows having per-thread
  // environment variables - e.g. multiple szl mapper threads processing
  // different input files - each with corresponding SZL_INPUT environment
  // variable value.
  void set_env_value(const string& name, const string& value);
  const char* env_value(const string& name) const;
  void clear_env_values();

  // Emitter factory (specific to embedding app/service)
  EmitterFactory* emitter_factory() const { return emitter_factory_; }
  void set_emitter_factory(EmitterFactory* factory) {
    emitter_factory_ = factory;
  }

  // Resources
  void set_code(Code* code);
  Code* code() const  { return code_; }
  void set_statics_size(size_t statics_size)  { statics_size_ = statics_size; }
  size_t statics_size() const  { return statics_size_; }
  Memory* heap() const  { return heap_; }
  Histogram* histo() const  { return histo_; }
  Profile* profile() const  { return profile_; }
  Executable* executable() const { return executable_; }
  void set_executable(Executable* executable) { executable_ = executable; }
  SymbolTable* symbol_table();
  void set_symbol_table(SymbolTable* table) { symbol_table_ = table; }
  Debugger* debugger() const  { return debugger_; }

  // Emitters/Outputters
  void AllocateOutputters(OutputTables* tables);
  Outputter* outputter(int index) const {
    assert(index >= 0 && index < outputter_count_);
    return outputters_[index];
  }

  // Keep a list of tuple types.
  void RegisterTupleType(TupleType* t);
  const List<TupleType*>* GetTupleTypes() const  { return tuple_types_; }
  // Apply a method to each registered tuple type.
  void ApplyToAllTupleTypes(void (TupleType::*f)(Proc* proc));

  // Keep a list of regular expressions, which are allocated outside of the
  // managed heap and that need to be explicitly deleted when this Proc is
  // deleted.
  void RegisterRegexp(void* regexp);

  // The PRNG used by the intrinsics.
  void SetRandomSeed(int32 seed);
  SzlACMRandom* rand() { return rand_; }

  // Reporting errors
  Error* error() { return error_; }
  int AllocateVarTrapinfoIndex()  { return var_trapinfo_count_++; }
  void ClearVarTrapinfo();

  // Reporting undefined values
  void RememberOutputter(const string& outputter_name, int var_index);
  uint64 UndefCnt()  { return undef_cnt_; }

  // Tuple decoding statistics
  uint64 proto_bytes_read() const { return proto_bytes_read_; }
  uint64 proto_bytes_skipped() const { return proto_bytes_skipped_; }
  void clear_proto_bytes_read() { proto_bytes_read_ = 0; }
  void clear_proto_bytes_skipped() { proto_bytes_skipped_ = 0; }
  void add_proto_bytes_read(uint64 bytes) { proto_bytes_read_ += bytes; }
  void add_proto_bytes_skipped(uint64 bytes) { proto_bytes_skipped_ += bytes; }

  // Paths that may not be read in kSecure mode
  void set_disallowed_read_paths(const vector<string>& disallowed) {
    disallowed_read_paths_ = disallowed;
  }
  const vector<string>& get_disallowed_read_paths() const {
    return disallowed_read_paths_;
  }

  // Initial process (for initializing the system)
  static Proc* initial_proc();

  // Miscellaneous program-wide state
  bool AlreadyIncluded(const char* file_name);

  // Resource Statistics
  ResourceStats* initialized_stats() const  { return initialized_stats_; }
  ResourceStats* current_stats() const  { return current_stats_; }
  void set_initialized_stats() {
    delete initialized_stats_;
    initialized_stats_ = new ResourceStats(this);
  }
  void set_current_stats() {
    if (calls_getresourcestats_) {
      delete current_stats_;
      current_stats_ = new ResourceStats(this);
    }
  }

  // Stack pointer of bottom frame of native code used for stack unwinding.
  // The bottom frame is the frame of init during static initialization or of
  // main during program execution.
  Val** native_bottom_sp()  { return native_.bottom_sp_; }

  // Direct member access from generated native code
  static size_t state_sp_offset()  { return OFFSETOF_MEMBER(Proc, state_.sp_); }
  static size_t native_bottom_sp_offset()  { return OFFSETOF_MEMBER(Proc, native_.bottom_sp_); }
  static size_t trap_info_offset()  { return OFFSETOF_MEMBER(Proc, trap_info_); }

  // Line profiling counters
  LineCount* linecount() { return linecount_; }
  List<Node*>* LineNumInfo();

  // Pass additional inputs
  void AddInput(const char* key, const char* value, int value_size);
  void AddInput(const char* key, BytesVal* value);
  void ClearInputs();
  BytesVal* GetInput(StringVal* key);

  // SuperSawzall parsing support
  bool RecognizePipelineKeywords() { return (mode_ & kPipeline) != 0; }
  bool IsSawzallJobBeingParsed() { return is_sawzall_job_being_parsed; }
  void NowParsingSawzallJob();
  void DoneParsingSawzallJob();

  // Get intrinsic cache based on registered name.
  // We assume that a Proc object will be accessed only
  // by one thread at a time.
  // Lookup the cache entry, return NULL if there is no such entry.
  IntrinsicCache* Lookup(const char* name_ptr);
  // Update the cache entry with the given value
  // If there is no such entry, insert a new name/entry pair
  void Update(const char* name_ptr, IntrinsicCache* entry);
  // Remove all the entries in the cache
  void ClearCache();

 private:
  struct AdditionalInput {
    StringVal* key;
    BytesVal* value;
  };
  int mode_;  // a bitmap of Modes
  const char* name_;
  Executable* executable_;  // Executable that owns us or NULL in SuperSawzall
  SymbolTable* symbol_table_;  // from Executable or set by hand in SuperSawzall
  Code* code_;
  size_t statics_size_;
  vector<AdditionalInput>* additional_input_;
  Error* error_;

  // Heap
  Memory* heap_;

  // Context (access to embedding app/service)
  void* context_;

  // Backend emitter factory (specific to embedding app/service):
  // used to install missing emitters at run-time
  EmitterFactory* emitter_factory_;

  // Profiling
  Histogram* histo_;
  Profile* profile_;
  LineCount* linecount_;

  // Debugger
  Debugger* debugger_;

  // Runtime traps
  const char* trap_info_;  // information on the cause of the trap
  struct VarTrapinfo {
    BytesVal* message;            // message
    union {
      const VarDecl* var;         // when no message, this var was undefined
      const TrapDesc* trap_desc;  // when message, says where error occurred
    };
  };
  VarTrapinfo* var_trapinfo_;  // one per variable for which we can trap
  int var_trapinfo_count_;     // size of per-variable trap info array
  bool clear_var_trapinfo_;    // set if var_trapinfo_ needs to be cleared

  // Outputters
  int outputter_count_;
  Outputter** outputters_;  // one for each output table variable

  // Tuple types allocated by this Proc
  List<TupleType*>* tuple_types_;

  // Objects allocated outside of the managed heap to be freed explicitly
  List<void*>* regexp_objects_;

  // Pseudorandom numbers for the intrinsics that generate random numbers
  SzlACMRandom* rand_;

  // Only one _undef_cnt per record (so only one per invocation)
  bool seen_undef_;

  // Undef reporting
  int undef_cnt_index_; // var index of outputter for _undef_cnt
  int undef_details_index_; // var index of outputter for _undef_details
  uint64 undef_cnt_; // count of undefs, for MR status page

  // Proto buffer decoding statistics
  uint64 proto_bytes_read_;       // Total bytes read = decoded + skipped
  uint64 proto_bytes_skipped_;    // Number of bytes skipped

  // Paths that may not be read in kSecure mode: empty if no loads are allowed
  vector<string> disallowed_read_paths_;

  // Stack
  // For now we assume a minimum amount of space
  // (the yellow zone) to be available at the top of the
  // stack when entering a new function - otherwise we
  // assume stack overflow happened. This should leave
  // plenty of space for the function locals and expression
  // stack - though one can create (extremely unlikely) cases
  // where the expression stack grows too large.
  size_t stack_size_;  // the stack size in bytes
  char* stack_;  // the very top of the stack (stack grows towards low addresses)
  enum { YELLOW_ZONE = 10*1024 };
  Val** initial_sp() const  { return reinterpret_cast<Val**>(stack_ + stack_size_); }
  Val** limit_sp() const  { return reinterpret_cast<Val**>(stack_ + YELLOW_ZONE); }

  // Execution status
  Status status_;  // execution status
  bool initialized_;  // statics have been initialized
  bool calls_getresourcestats_;
  const char* error_msg_;
  Instr* trap_pc_;  // PC of undefined trap or assertion failure

  // Execution state copied into (local) interpreter variables (for speed)
  struct {
    Frame* gp_;  // globals pointer
    Frame* fp_;  // frame pointer
    Val** sp_;  // stack pointer
    Instr* pc_;  // program counter
    bool cc_;  // condition code
  } state_;

  // Native execution state (in addition to state_'s gp_, fp_, sp_, and pc_)
  struct {
    NFrame* fp_;
    Val** sp_;
    // Keep track of the bottom stack frame in order to unwind the native stack
    Val** bottom_sp_;  // set in generated native code - see NCodeGen::Prologue
  } native_;

  // Execution state saved in StartCall for use in ContinueCall
  struct {
    Frame* fp_;
    Frame* bp_;
    const VarDecl* fun_decl_;
  } start_call_;

  Frame* saved_fp_;  // frame pointer of initial kDoCalls function call

  // For the creation of the initial proc (used to allocate memory from)
  Proc();

  // Handler for runtime traps
  void HandleTrap(intptr_t sp_adjust,
                  int native_sp_adjust,
                  bool is_fatal);

  // We include files and protos only once and do this by keeping a list
  // of what we've seen. User-specified pathnames aren't good enough because of
  // symlinks and .. and so on. Device and i-number pairs don't work with
  // systems that make no guarantees about a file's inode staying constant (e.g.
  // srcfs). Therefore we attempt to identify duplicate files with either of two
  // checks: (device, inode) pairs or canonicalized absolute path names
  // generated by realpath().
  struct Inode {
    int dev;
    int ino;
    string real_path;
    bool reported;  // Warning has been issued.
  };

  // list of inode info of files and protos already included
  // (cannot use List<> here because it would be allocated on
  // the current heap which is not the correct heap for Fork())
  vector<Inode>* szl_file_inodes_;  // keeps track of Sawzall includes
  vector<Inode>* sszl_file_inodes_;  // keeps track of SuperSawzall includes

  // Resource Statistics
  ResourceStats* initialized_stats_;  // after initialization
  ResourceStats* current_stats_;  // at end of prior record

  // Buffers to hold messages formatted by PrintError().
  // We sometimes use PrintError to generate text that will be incorporated
  // into another message with a subsequent PrintError; by alternating
  // between two preallocated buffers, we avoid dynamic allocation.
  // TODO: revisit this issue
  static const int kMaxErrorMessageLength = 1024;
  static const int kNumErrorMessageBuffers = 2;
  char (*error_messages_)[kMaxErrorMessageLength + 1];
  int error_message_index_;

  // Common code shared across SetupRun() and DoCall().
  // mark_heap controls whether the memory manager should set marks at
  // the current heap location, for use when validating
  // that all memory has been released when FinishExecuteOrCall() is run.
  void SetupRunOrCall(bool mark_heap);

  // Common code shared across Execute() and DoCall().
  void FinishExecuteOrCall(bool do_cleanup, bool traps_are_fatal);

  // Stack trace: only print once
  void PrintStackTrace();
  bool stack_trace_printed_;

  // SuperSawzall parsing support
  // A flag to differentiate between top-level SuperSawzall code and Sawzall
  // code within jobs.
  bool is_sawzall_job_being_parsed;

  // vector of IntrinsicCache for registered intrinsics.
  // The size is expected to be small, so vector should be fine.
  vector<IntrinsicCache*> intrinsic_cache_;
  vector<const char*> intrinsic_slots_;

  // List of the Proc environment values to be read by the getenv szl intrinsic.
  // getenv will first look into this list and then into the global environment.
  struct NameValuePair {
    NameValuePair(const string& name_, const string& value_)
        : name(name_), value(value_) {}
    string name;
    string value;
  };
  vector<NameValuePair> env_values_;

  friend class Engine;  // needs access to state_
  friend class Memory;  // needs access to state_ and initial_sp()
  friend class NSupport;  // needs access to state_ and native_ and trap_pc_
  friend class ClosureVal;  // needs access to state_
};


}  // namespace sawzall
