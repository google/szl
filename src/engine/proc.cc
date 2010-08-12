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

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>  // For stat in AlreadyIncluded
#include <sys/stat.h>   // For stat in AlreadyIncluded
#include <unistd.h>     // For stat in AlreadyIncluded
#include <sys/resource.h>     // For getrusage in ResourceStats

#include "engine/globals.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "utilities/strutils.h"
#include "utilities/random_base.h"
#include "utilities/acmrandom.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/code.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/frame.h"
#include "public/sawzall.h"
#include "engine/proc.h"
#include "engine/error.h"
#include "engine/histogram.h"
#include "engine/profile.h"
#include "engine/linecount.h"
#include "public/emitterinterface.h"
#include "engine/outputter.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/map.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/engine.h"
#include "engine/debugger.h"
#include "engine/compiler.h"

namespace sawzall {

ResourceStats::ResourceStats(Proc* proc)
  : proc_(proc) {
  Update();
}


void ResourceStats::Update() {
  available_mem_ = proc_->heap()->total_available();
  allocated_mem_ = proc_->heap()->total_allocated();
  struct rusage r;
  if (getrusage(RUSAGE_SELF, &r) == 0) {
    user_time_ = r.ru_utime.tv_sec * 1000000 + r.ru_utime.tv_usec;
    system_time_ = r.ru_stime.tv_sec * 1000000 + r.ru_stime.tv_usec;
  } else {
    user_time_ = 0;
    system_time_ = 0;
  }
}


Proc::Proc(int mode, ErrorHandler* error_handler) {
  mode_ = mode;
  name_ = "<no name>";
  executable_ = NULL;
  symbol_table_ = NULL;
  code_ = NULL;
  statics_size_ = 0;
  heap_ = new Memory(this);  // explicitly deallocated
  context_ = NULL;
  emitter_factory_ = NULL;
  histo_ = ((mode & kHistogram) != 0) ? Histogram::New(this) : NULL;
  profile_ = NULL;
  debugger_ = NULL;
  stack_size_ = MaxInt(FLAGS_stack_size * 1024, YELLOW_ZONE*2);
  stack_ = new char[stack_size_];  // explicitly deallocated
  var_trapinfo_count_ = 1;  // slot 0 is for return values
  var_trapinfo_ = NULL;
  clear_var_trapinfo_ = false;
  outputters_ = NULL;
  tuple_types_ = NULL;
  regexp_objects_ = NULL;
  rand_ = new SzlACMRandom(SzlACMRandom::GoodSeed());
  undef_cnt_index_ = 0;
  undef_details_index_ = 0;
  undef_cnt_ = 0;
  proto_bytes_read_ = 0;
  proto_bytes_skipped_ = 0;
  status_ = TERMINATED;
  linecount_ = new LineCount(this);
  initialized_ = false;
  state_.gp_ = NULL;
  state_.fp_ = NULL;
  state_.sp_ = initial_sp();
  state_.pc_ = NULL;
  state_.cc_ = false;
  native_.fp_ = NULL;
  native_.sp_ = NULL;
  native_.bottom_sp_ = NULL;
  start_call_.fp_ = NULL;
  start_call_.bp_ = NULL;
  start_call_.fun_decl_ = NULL;
  szl_file_inodes_ = NULL;
  sszl_file_inodes_ = NULL;
  calls_getresourcestats_ = false;
  initialized_stats_ = NULL;
  current_stats_ = NULL;
  error_messages_ =
    new char[kNumErrorMessageBuffers][kMaxErrorMessageLength + 1];
  error_message_index_ = 0;
  additional_input_ = new vector<AdditionalInput>;
  error_ = new Error(error_handler);
  trap_pc_ = NULL;
  stack_trace_printed_ = false;
  is_sawzall_job_being_parsed = false;
}


Proc::Proc() {
  mode_ = kPersistent | kInternal;  // internal Procs have persistent memory
  name_ = "initial proc";
  executable_ = NULL;
  symbol_table_ = NULL;
  code_ = NULL;
  statics_size_ = 0;
  heap_ = new Memory(this);  // explicitly deallocated
  context_ = NULL;
  emitter_factory_ = NULL;
  histo_ = NULL;
  profile_ = NULL;
  debugger_ = NULL;
  stack_size_ = 0;
  stack_ = NULL;
  var_trapinfo_count_ = 1;  // slot 0 is for return values
  var_trapinfo_ = NULL;
  clear_var_trapinfo_ = false;
  outputters_ = NULL;
  tuple_types_ = NULL;
  regexp_objects_ = NULL;
  rand_ = new SzlACMRandom(SzlACMRandom::GoodSeed());
  undef_cnt_index_ = 0;
  undef_details_index_ = 0;
  undef_cnt_ = 0;
  proto_bytes_read_ = 0;
  proto_bytes_skipped_ = 0;
  status_ = TERMINATED;
  linecount_ = new LineCount(this);
  initialized_ = false;
  state_.gp_ = NULL;
  state_.fp_ = NULL;
  state_.sp_ = NULL;
  state_.pc_ = NULL;
  state_.cc_ = false;
  native_.fp_ = NULL;
  native_.sp_ = NULL;
  native_.bottom_sp_ = NULL;
  start_call_.fp_ = NULL;
  start_call_.bp_ = NULL;
  start_call_.fun_decl_ = NULL;
  szl_file_inodes_ = NULL;
  sszl_file_inodes_ = NULL;
  calls_getresourcestats_ = false;
  initialized_stats_ = NULL;
  current_stats_ = NULL;
  error_messages_ =
    new char[kNumErrorMessageBuffers][kMaxErrorMessageLength + 1];
  error_message_index_ = 0;
  additional_input_ = new vector<AdditionalInput>;
  error_ = new Error(NULL);
  trap_pc_ = NULL;
  stack_trace_printed_ = false;
}


Proc::~Proc() {
  delete[] stack_;
  if (var_trapinfo_ != NULL) {
    // delete saved trap messages
    delete[] var_trapinfo_;
  }
  if (outputters_ != NULL) {
    // delete outputters
    for (int i = outputter_count_; i-- > 0; )
      delete outputters_[i];
    delete[] outputters_;
  }
  if (histo_ != NULL) {
    // print histogram before destruction
    F.print("Opcode histogram for process '%s':\n", name());
    histo_->Print(0.005);  // don't print opcodes with costs < 0.5%
  }
  if (profile_ != NULL) {
    // print profile before destruction
    F.print("Code profile (raw) for process '%s':\n", name());
    profile_->PrintRaw(0.005);  // don't print code segments with costs < 0.5%
    F.print("Function profile (aggregated) for process '%s':\n", name());
    profile_->PrintAggregated(0.005);  // don't print functions with costs < 0.5%
    delete profile_;
  }
  delete debugger_;
  if (regexp_objects_ != NULL) {
    for (int i = 0; i < regexp_objects_->length(); i++)
      FreeRegexp(regexp_objects_->at(i));
  }
  // do not cleanup code_ here, not owner
  delete heap_;
  delete rand_;
  delete linecount_;
  delete szl_file_inodes_;
  delete sszl_file_inodes_;
  delete initialized_stats_;
  delete current_stats_;
  delete [] error_messages_;
  delete additional_input_;
  delete error_;

  ClearCache();
}


// This macro is used to put some casting ugliness
// into a single place - it is only used by Fork()
// Note: If field == NULL then proc->field is already
// NULL, because the Proc was just created.
#define SET(proc, field, dist) \
  if (field != NULL) \
    proc->field = reinterpret_cast<Frame*>(reinterpret_cast<char*>(field) + dist);

// TODO: Fork() needs to be cleaned up: we don't really need full-blown
// general fork semantics, we just need to be able to create a proper
// starting Proc from the Executable, to be used by the Process.
Proc* Proc::Fork(int mode) const {
  // this should only be done with procs that have not
  // yet executed any code as otherwise copying the
  // stack may not work properly
  CHECK(! is_initialized() && status_ == TERMINATED);
  // create a new process
  Proc* p = new Proc(mode, NULL);  // explicitly deallocated by client
  p->calls_getresourcestats_ = calls_getresourcestats_;
  p->set_code(code_);
  p->statics_size_ = statics_size_;
  p->context_ = context_;
  // forked process has a histogram, if the original process has one
  if (histo() != NULL)
    p->histo_ = Histogram::New(p);
  // set the line counting information
  p->linecount_->AllocCounters(code_->LineNumInfo()->length());
  // note: for now we don't copy outputters_
  CHECK(outputters_ == NULL) << ": outputters_ = " << outputters_ << " != NULL";
  // copy state
  p->status_ = status_;
  // copy stack data (initial_sp to sp)
  int size = reinterpret_cast<char*>(initial_sp()) - reinterpret_cast<char*>(state_.sp_);
  memcpy(p->state_.sp_, state_.sp_, size);
  // move frame pointers
  int dist = reinterpret_cast<char*>(p->state_.sp_) - reinterpret_cast<char*>(state_.sp_);
  SET(p, state_.gp_, dist);
  SET(p, state_.fp_, dist);
  // pc remains the same
  p->state_.pc_ = state_.pc_;
  p->var_trapinfo_count_ = var_trapinfo_count_;
  p->var_trapinfo_ = new VarTrapinfo[var_trapinfo_count_];
  memset(p->var_trapinfo_, '\0', sizeof(VarTrapinfo)*var_trapinfo_count_);
  p->env_values_ = env_values_;
  // Note: szl_file_inodes_ does not need to be copied at
  // this point since Fork is only used by the Executable.
  return p;
}

#undef SET


void Proc::SetupInitialization() {
  CHECK(!is_initialized() && status_ == TERMINATED);

  // setup state
  trap_info_ = NULL;
  seen_undef_ = 0;

  state_.fp_ = NULL;
  assert(state_.sp_ == initial_sp());
  // set gp before starting initialization, so that its value can be used
  // while handling traps occurring during initialization
  state_.gp_ = reinterpret_cast<Frame*>(state_.sp_ - statics_size_ / sizeof(Val*));
  state_.pc_ = code_->init();

  // ready for execution
  status_ = SUSPENDED;
}


szl_fingerprint Proc::InitializationFingerprint() {
  assert(state_.gp_ != NULL);
  // container fingerprint of the values on the initialization stack frame
  szl_fingerprint print = kFingerSeed();
  Val** ptr = &state_.gp_->at(0);
  Val** end = initial_sp();
  while (ptr < end) {
    // careful, can have undefined values (output vars, NO_INDEX)
    if ((*ptr)->is_null())
      print = FingerprintCat(print, Fingerprint(implicit_cast<uint64>(0)));
    else
      print = FingerprintCat(print, (*ptr)->Fingerprint(this));
    ptr++;
  }
  return print;
}


void Proc::SetupRunOrCall(bool mark_heap) {
  CHECK(is_initialized() && status_ == TERMINATED);

  if (mark_heap) {
    // mark current heap position
    heap()->Mark();
  }

  // setup state
  trap_info_ = NULL;
  seen_undef_ = 0;

  // ready for execution
  status_ = SUSPENDED;

  ClearVarTrapinfo();
}


void Proc::SetupRun(const char* input_ptr, size_t input_size,
                    const char* key_ptr, size_t key_size) {
  SetupRunOrCall(!(mode_ & kDoCalls));

  assert(state_.gp_ != NULL);
  state_.fp_ = state_.gp_;
  state_.sp_ = state_.gp_->stack();
  state_.pc_ = code_->main();

  // push parameter for main_(input: string, key: string)
  // (arguments are pushed from right to left)
  { BytesVal* a = SymbolTable::bytes_form()->NewValInit(this, key_size, key_ptr);
    Engine::push(state_.sp_, a);
  }
  { BytesVal* a = SymbolTable::bytes_form()->NewValInit(this, input_size,
                                                        input_ptr);
    Engine::push(state_.sp_, a);
  }

  // clear any pointers from additional inputs
  ClearInputs();
}


Proc::Status Proc::Execute(int max_steps, int* num_steps) {
  CHECK(status_ == SUSPENDED);

  // execute time_slice instructions
  status_ = RUNNING;
  if ((mode_ & kNative) != 0) {
    // execute natively compiled code instead of calling the interpreter
    if (state_.pc_ == code_->init()) {
      // Call native init code, which will first allocate the static frame on
      // the interpreter stack (thereby modifying state_.sp_ and state_.fp_) and
      // which will then initialize the statics.
      typedef Proc::Status (*native_init)(Frame*, Proc*);
      // new-style casts not allowed between function pointers and objects
      status_ = (*(native_init)state_.pc_)(state_.gp_, this);

    } else {
      assert(state_.pc_ == code_->main());

      // pop the parameters for main from the interpreter stack and pass them
      // on the native stack
      Val* input = Engine::pop(state_.sp_);
      Val* key = Engine::pop(state_.sp_);

      // there should be no more arguments on the interpreter stack
      assert(state_.sp_ == state_.fp_->stack());

      // call main code, passing gp as static link pointing to statics
      typedef Proc::Status (*native_main)(Frame*, Proc*, Val*, Val*);
      // new-style casts not allowed between function pointers and objects
      status_ = (*(native_main)state_.pc_)(state_.gp_, this, input, key);
    }
  } else {
    status_ = Engine::Execute(this, max_steps, num_steps);
  }

  FinishExecuteOrCall(!(mode_ & kDoCalls), false);

  // done
  return status_;
}


void Proc::FinishExecuteOrCall(bool do_cleanup, bool traps_are_fatal) {
  // handle current status
  switch (status_) {
    case RUNNING:
      ShouldNotReachHere();
      break;

    case SUSPENDED:
      // nothing to do
      break;

    case TRAPPED:
      HandleTrap(0, 0, traps_are_fatal);
      if (status_ != FAILED)
        break;
      // else fall through

    case FAILED:
      // Is this a failed assertion?  Special case to print stack when it's
      // not a regular trap.
      if (trap_info_ != NULL &&
          strncmp(trap_info_, "assertion failed", 16) == 0) {
        PrintStackTrace(); // ensures that trap_pc_ is set
        // assert() is a function with an empty return type, so it
        // always appears as the sole expression in an ExprStat node.
        // Every ExprStat has a trap range (see CodeGen::DoExprStat),
        // so we are guaranteed to get a non-NULL trap description here.
        const TrapDesc* desc = code_->TrapForInstr(trap_pc_);
        trap_info_ = PrintError("%s at %s", trap_info_, desc->comment());
      }
      // fall through

    case TERMINATED:
      ClearInputs();
      ClearVarTrapinfo();
      // release any resources used by the run, if necessary
      if (do_cleanup && is_initialized()) {
        heap()->Release();
      }
      // set new state, if necessary
      if (status_ == TERMINATED && ! is_initialized()) {
        // code is initialized now
        assert(state_.fp_ != NULL);
        // current fp marks global frame
        // state_.gp_ should not have been modified by init code
        assert(state_.gp_ == state_.fp_);
        // mark the proc as initialized
        initialized_ = true;
        assert(is_initialized());
      }
      break;

    default:
      ShouldNotReachHere();
  }
}


const VarDecl* Proc::LookupFunction(const char* name) {
  Scope* outer_scope = executable_->compilation()->program()->scope();
  CHECK(outer_scope != NULL);
  Object* object = outer_scope->Lookup(name);
  if (object == NULL) {
    // Didn't find the function.
    trap_info_ = PrintError("%q undeclared", name);
    return NULL;
  }
  VarDecl* var_decl = object->AsVarDecl();
  if (var_decl == NULL) {
    // The name didn't name a variable declaration, so it can't be a function.
    trap_info_ = PrintError("%q is not a variable", name);
    return NULL;
  }
  if (!var_decl->type()->is_function()) {
    // The variable wasn't a function.
    trap_info_ = PrintError("%q is not a function", name);
    return NULL;
  }
  return var_decl;
}


void Proc::SetupCall() {
  CHECK(mode_ & kDoCalls)
      << "Must pass kDoCalls when creating the Executable "
      << "in order to support SetupCall";
  CHECK(!(mode_ & kNative))
      << "Sorry, native execution mode not yet supported for SetupCall";

  CHECK(is_initialized() && (status_ == TERMINATED || status_ == FAILED));
  if (status_ == FAILED) {
    status_ = TERMINATED;
    stack_trace_printed_ = false;
  }

  SetupRunOrCall(true);
}


Val* Proc::DoCall(const VarDecl* fun_decl, Val* args[], int num_args) {
  CHECK(mode_ & kDoCalls)
      << "Must pass kDoCalls when creating the Executable "
      << "in order to support DoCall";
  CHECK(!(mode_ & kNative))
      << "Sorry, native execution mode not yet supported for DoCall";

  CHECK(is_initialized() && status_ == SUSPENDED);

  assert(state_.gp_ != NULL);
  assert(state_.fp_ != NULL);
  assert(state_.fp_->static_link() == state_.gp_);
  assert(state_.fp_->dynamic_link() == state_.gp_);
  assert(state_.fp_->return_pc() == NULL);
  assert(state_.sp_ == state_.fp_->stack());

  FunctionType* fun_type = fun_decl->type()->as_function();
  CHECK(fun_type);
  int num_formals = fun_type->parameters()->length();
  if (num_formals != num_args) {
    // Passing the wrong number of arguments.
    trap_info_ = PrintError(
        "wrong number of arguments to %s: expected %d; passed %d",
        fun_decl->name(), num_formals, num_args);
    status_ = FAILED;
    return NULL;
  }

  // Save the current frame pointer, in case we need to restore it after a trap.
  saved_fp_ = state_.fp_;

  // Push the arguments, right-to-left.
  for (int i = num_args - 1; i >= 0; i--) {
    Val* v = args[i];
    CHECK(v) << "should not be passing in a NULL argument to DoCall()";
    v->inc_ref();
    Engine::push(state_.sp_, v);
  }

  // Get the function closure value.
  CHECK_LE(fun_decl->level(), 1) << "Can only invoke global functions";
  Frame* frame = fun_decl->is_static() ? state_.gp_ : state_.fp_;
  ClosureVal* c = frame->at(fun_decl->offset() / sizeof(Val*))->as_closure();
  Frame* bp = c->context();
  CHECK(bp == state_.gp_ || bp == state_.fp_)
      << "unexpected context for global function";
  // Set the interpreter to run at the function's entry point.
  state_.pc_ = c->entry();

  status_ = RUNNING;
  if ((status_ = Engine::Execute(this, kint32max, NULL, bp)) == RUNNING) {
    while ((status_ = Engine::Execute(this, kint32max, NULL)) == RUNNING) {
    }
  }

  // Extract the result (if there is one).
  Val* result;
  if (status_ == TERMINATED && fun_decl->type()->as_function()->has_result())
    result = Engine::pop(state_.sp_);
  else
    result = NULL;

  // Clean up the execution state.
  FinishExecuteOrCall(false, true);

  if (status_ == FAILED) {
    // The stack state is still in the function that had the error.
    // Reset the stack to the original frame to allow the client to
    // keep invoking DoCall().
    state_.fp_ = saved_fp_;
    state_.sp_ = saved_fp_->stack();
    state_.pc_ = NULL;
  }

  CHECK((status_ == TERMINATED && error_msg() == NULL) ||
        (status_ == FAILED && error_msg() != NULL))
      << "Unexpected status/error-message state";

  // Return the result (if any).
  return result;
}


void Proc::StartCall(const VarDecl* fun_decl, Val* args[], int num_args) {
  CHECK(mode_ & kDoCalls)
      << "Must pass kDoCalls when creating the Executable "
      << "in order to support StartCall";
  CHECK(!(mode_ & kNative))
      << "Sorry, native execution mode not yet supported for StartCall";

  CHECK(is_initialized() && status_ == SUSPENDED);

  assert(state_.gp_ != NULL);
  assert(state_.fp_ != NULL);
  assert(state_.fp_->static_link() == state_.gp_);
  assert(state_.fp_->dynamic_link() == state_.gp_);
  assert(state_.fp_->return_pc() == NULL);
  assert(state_.sp_ == state_.fp_->stack());

  FunctionType* fun_type = fun_decl->type()->as_function();
  CHECK(fun_type);
  int num_formals = fun_type->parameters()->length();
  if (num_formals != num_args) {
    // Passing the wrong number of arguments.
    trap_info_ = PrintError(
        "wrong number of arguments to %s: expected %d; passed %d",
        fun_decl->name(), num_formals, num_args);
    status_ = FAILED;
    return;
  }

  // Save the current frame pointer, in case we need to restore it after a trap.
  start_call_.fp_ = state_.fp_;
  start_call_.fun_decl_ = fun_decl;

  // Push the arguments, right-to-left.
  for (int i = num_args - 1; i >= 0; i--) {
    Val* v = args[i];
    CHECK(v) << "should not be passing in a NULL argument to DoCall()";
    v->inc_ref();
    Engine::push(state_.sp_, v);
  }

  // Get the function closure value.
  CHECK_LE(fun_decl->level(), 1) << "Can only invoke global functions";
  Frame* frame = fun_decl->is_static() ? state_.gp_ : state_.fp_;
  ClosureVal* c = frame->at(fun_decl->offset() / sizeof(Val*))->as_closure();
  start_call_.bp_ = c->context();
  CHECK(start_call_.bp_ == state_.gp_ || start_call_.bp_ == state_.fp_)
      << "unexpected context for global function";
  // Set the interpreter to run at the function's entry point.
  state_.pc_ = c->entry();
}


Val* Proc::ContinueCall(int max_steps, int* num_steps) {
  CHECK(mode_ & kDoCalls)
      << "Must pass kDoCalls when creating the Executable "
      << "in order to support StartCall";
  CHECK(!(mode_ & kNative))
      << "Sorry, native execution mode not yet supported for StartCall";

  Val* result = NULL;
  if (status_ == FAILED) {
    // Exit early if StartCall failed.
    return result;
  }

  status_ = RUNNING;
  status_ = Engine::Execute(this, max_steps, num_steps, start_call_.bp_);

  // Extract the result (if there is one).
  if (status_ == TERMINATED &&
      start_call_.fun_decl_->type()->as_function()->has_result())
    result = Engine::pop(state_.sp_);

  // Clean up the execution state.
  FinishExecuteOrCall(false, true);

  if (status_ == SUSPENDED) {
    return result;
  }

  if (status_ == FAILED) {
    // The stack state is still in the function that had the error.
    // Reset the stack to the original frame to allow the client to
    // keep invoking StartCall().
    state_.fp_ = start_call_.fp_;
    state_.sp_ = start_call_.fp_->stack();
    state_.pc_ = NULL;
  }

  CHECK((status_ == TERMINATED && error_msg() == NULL) ||
        (status_ == FAILED && error_msg() != NULL))
      << "Unexpected status/error-message state";

  return result;
}


void Proc::FinishCall() {
  CHECK(mode_ & kDoCalls)
      << "Must pass kDoCalls when creating the Executable "
      << "in order to support FinishCall";
  CHECK(!(mode_ & kNative))
      << "Sorry, native execution mode not yet supported for FinishCall";

  CHECK(is_initialized());
  CHECK(status_ == TERMINATED || status_ == FAILED || status_ == SUSPENDED);
  // We might have called SetupCall(), and then called FinishCall()
  // without an intervening DoCall() (e.g. if the client detected
  // errors).  In this case, the status will be SUSPENDED.  Clean it up.
  if (status_ == SUSPENDED) status_ = TERMINATED;

  heap()->Release();
}


char* Proc::PrintString(const char* fmt, ...) {
  Fmt::State f;
  va_list arg;
  F.fmtstrinit(&f);
  va_start(arg, fmt);
  F.fmtvprint(&f, fmt, &arg);
  va_end(arg);
  // allocate a new string on the proc's heap
  const int len = f.nfmt;  // number of chars generated
  char* s0 = F.fmtstrflush(&f);  // allocates a string
  char* s1 = ALLOC(this, char, len + 1);
  memmove(s1, s0, len + 1);
  free(s0);
  return s1;
}


char* Proc::CopyString(const char* s) {
  const int len = strlen(s);
  char* copy = ALLOC(this, char, len + 1);
  memmove(copy, s, len + 1);
  return copy;
}


char* Proc::PrintError(const char* fmt, ...) {
  // Find the next error message buffer.
  char* s = error_messages_[error_message_index_++];
  if (error_message_index_ == kNumErrorMessageBuffers)
    error_message_index_ = 0;

  // Format the error message there.
  va_list args;
  va_start(args, fmt);
  F.vsnprint(s, kMaxErrorMessageLength, fmt, &args);
  va_end(args);
  return s;
}


void Proc::set_code(Code* code) {
  code_ = code;
  if (code != NULL) {
    if ((mode_ & kProfile) != 0) {
      delete profile_;
      profile_ = new Profile(this);  // explicitly deallocated
    }
    if ((mode_ & kDebugger) != 0) {
      delete debugger_;
      debugger_ = new Debugger(this);  // explicitly deallocated
    }
  }
}


SymbolTable* Proc::symbol_table() {
  if (symbol_table_ == NULL && executable_ != NULL)
    return executable_->compilation()->symbol_table();
  else
    return symbol_table_;
}


void Proc::AllocateOutputters(OutputTables* tables) {
  assert(outputters_ == NULL);  // do initialization only once
  outputter_count_ = tables->length();
  outputters_ = new Outputter*[outputter_count_];
  for (int i = 0; i < outputter_count_; i++)
    outputters_[i] = new Outputter(this, tables->at(i));
}


List<Node*>* Proc::LineNumInfo() {
  return code_->LineNumInfo();
}


void Proc::RememberOutputter(const string& outputter_name, int var_index) {
  if (outputter_name == "_undef_cnt")
    undef_cnt_index_ = var_index;
  else if (outputter_name == "_undef_details")
    undef_details_index_ = var_index;
}


Proc* Proc::initial_proc() {
  static Proc p;
  return &p;
}


void Proc::HandleTrap(intptr_t sp_adjust,
                      int native_sp_adjust,
                      bool is_fatal) {
  // sp_adjust and native_sp_adjust are only meaningful in native mode and
  // specify the number of values that need to be popped from each stack before
  // continuing execution at the trap target. If sp_adjust is in the address
  // range of the interpreter stack, it is the new absolute value rather than an
  // small adjusment. See NCodeGen::Trap() in nativecodegen.cc.
  assert((sp_adjust >= 0 && sp_adjust <= stack_size_)  // relative
         || (reinterpret_cast<Val**>(sp_adjust) >= limit_sp() &&
             reinterpret_cast<Val**>(sp_adjust) <= initial_sp()));  // absolute
  assert(native_sp_adjust >= 0);
  clear_var_trapinfo_ = true;

  // the trap pc must be within the range of the opcode causing the
  // trap - since it may have advanced to the next instruction subtract
  // 1 to get it into the range (safe to do, because the pc is always
  // incremented by 1 in the switch)
  trap_pc_ = state_.pc_ - 1;  // safe for native code as well
  const int trap_offs = trap_pc_ - code_->base();
  const TrapDesc* desc = code_->TrapForInstr(trap_pc_);
  if (desc == NULL)
    // compiler bug => FatalError
    FatalError("no trap handler for pc = %p (%d)", trap_pc_, trap_offs);
  // set continuation pc
  state_.pc_ = code_->base() + desc->target();

  // undefine variable, if any
  int index = desc->var_index();
  int delta = desc->var_delta();
  if (index != NO_INDEX) {
    // undefine variable
    Val** v;
    if ((mode_ & kNative) == 0)
      v = &Engine::base(state_.fp_, delta)->at(index);
    else
      v = &NFrame::base(native_.fp_, delta)->at(index);
    (*v)->dec_ref();
    *v = NULL;
  }

  // adjust stack pointer and decrement ref counts of involved expression values
  Val** new_sp;  // new interpreter stack pointer after adjustment
  if ((mode_ & kNative) == 0) {
    new_sp = state_.fp_->stack() - desc->stack_height();
  } else {
    Val** cur_native_sp = native_.sp_;
    Val** new_native_sp = cur_native_sp + native_sp_adjust;
    assert(cur_native_sp <= new_native_sp);
    while (cur_native_sp < new_native_sp) {
      Val* val = Engine::pop(cur_native_sp);
      // skip call area header if marker found
      intptr_t header_size = reinterpret_cast<intptr_t>(val);
      if ((header_size % sizeof(Val*)) == 0 &&  // not an smi
          0 <= header_size && header_size <= NFrame::kMaxCallAreaHeaderSize)
        cur_native_sp += header_size / sizeof(Val*);
      else
        val->dec_ref();
    }
    assert(cur_native_sp == new_native_sp);
    native_.sp_ = new_native_sp;

    // The only possible values found on the interpreter stack during a trap in
    // native mode are the arguments to (possibly nested) intrinsic calls.
    // A small sp_adjust argument indicates how many such arguments need to be
    // popped before jumping to the continuation target, or it indicates the new
    // sp value at the continuation target.
    if (sp_adjust >= 0 && sp_adjust <= stack_size_)
      new_sp = state_.sp_ + sp_adjust;
    else
      new_sp = reinterpret_cast<Val**>(sp_adjust);

    assert(new_sp >= limit_sp() && new_sp <= initial_sp());
  }

  Val** cur_sp = state_.sp_;
  assert(cur_sp <= new_sp);
  while (cur_sp < new_sp)
    Engine::pop(cur_sp)->dec_ref();
  assert(cur_sp == new_sp);
  state_.sp_ = new_sp;

  // find the target variable trap entry, if any
  VarTrapinfo* target_var_trapinfo = NULL;
  if (desc->var() != NULL) {
    // associate error with target variable
    int index = desc->var()->trapinfo_index();
    assert(index > 0 && index < var_trapinfo_count_);
    target_var_trapinfo = &var_trapinfo_[index];
  } else if (desc->is_silent()) {
    // silent traps with no targets are return statements, use slot 0
    // (also uses slot 0 for def() traps, but this is harmless)
    target_var_trapinfo = &var_trapinfo_[0];
  }

  // find the referenced variable trap entry, if any
  VarTrapinfo* checked_var_trapinfo = NULL;
  VarDecl* checked_var = NULL;
  for (int i = 0; i < desc->var_traps()->length(); i++) {
    if (desc->var_traps()->at(i).code_offset == trap_offs) {
      checked_var = desc->var_traps()->at(i).var;
      int index = (checked_var != NULL) ? checked_var->trapinfo_index() : 0;
      assert(index >= 0 && index < var_trapinfo_count_);
      checked_var_trapinfo = &var_trapinfo_[index];
      break;
    }
  }

  // propagate trap info to the target variable
  // for unexplained undefined vars, record the VarDecl as the trap info
  if (checked_var_trapinfo != NULL) {
    // an undefined variable or undefined non-intrinsic function call result
    // get saved trap info, if any
    if (checked_var_trapinfo->message == NULL) {
      // no previous message; just an undefined variable
      // could be copied from another undefined variable, or never defined
      if (checked_var_trapinfo->var == NULL)
        checked_var_trapinfo->var = checked_var;  // never def, blame this one
    }
    // propagate to the target variable
    if (target_var_trapinfo != NULL) {
      if (target_var_trapinfo->message != NULL)
        target_var_trapinfo->message->dec_ref();
      if (checked_var_trapinfo->message != NULL)
        checked_var_trapinfo->message->inc_ref();
      target_var_trapinfo->message = checked_var_trapinfo->message;
      target_var_trapinfo->trap_desc = checked_var_trapinfo->trap_desc;
    }
  } else {
    assert(trap_info_ != NULL);
    // generated in this statement or an intrinsic.
    int length = strlen(trap_info_) + 1;
    if (target_var_trapinfo != NULL) {
      // if there is an existing BytesVal in "message" and it is sufficiently
      // large and unique, just reuse it; else allocate a new one
      if (target_var_trapinfo->message != NULL) {
        if (target_var_trapinfo->message->ref() != 1 ||
            target_var_trapinfo->message->length() < length) {
          // cannot reuse; discard existing message
          target_var_trapinfo->message->dec_ref();
          target_var_trapinfo->message = NULL;
        }
      }
      if (target_var_trapinfo->message == NULL)
        target_var_trapinfo->message = Factory::NewBytes(this, length);
      memcpy(target_var_trapinfo->message->base(), trap_info_, length);
      target_var_trapinfo->trap_desc = desc;
    }
  }

  // see if either (a) this trap isn't silent, or (b) we're stopping
  // with this error, but we don't yet have an error message
  if (! desc->is_silent() || (is_fatal && trap_info_ == NULL)) {
    // determine the error details
    // for undefined variables use the name of the variable
    // and the original error message, if any
    const char* info = trap_info_;
    if (trap_info_ == NULL) {
      assert(checked_var_trapinfo != NULL);
      if (checked_var_trapinfo->message == NULL) {
        // it was a variable that was never defined
        if (checked_var_trapinfo->var == checked_var) {
          // it was the variable we were checking
          info = PrintError("probably because %q had not been defined",
                            checked_var->name());
        } else {
          if (checked_var != NULL) {
            // it was copied from somewhere else to a variable we used
            info = PrintError("probably because %q was copied from %q "
                              "declared at %L which had not been defined",
                              checked_var->name(),
                              checked_var_trapinfo->var->name(),
                              checked_var_trapinfo->var->file_line());
          } else {
            // it was an undefined variable returned by a function
            info = PrintError("probably because a function result was copied "
                              "from %q declared at %L which had not been "
                              "defined",
                              checked_var_trapinfo->var->name(),
                              checked_var_trapinfo->var->file_line());
          }
        }
      } else {
        // it was an error in another statement
        if (checked_var != NULL) {
          // found because it left a variable undefined
          info = PrintError("probably because %q was undefined due to an "
                            "error at %s (%s)",
                            checked_var->name(),
                            checked_var_trapinfo->trap_desc->comment(),
                            checked_var_trapinfo->message->base());
        } else {
          // found because a function result was undefined
          info = PrintError("probably because a function result was undefined "
                            "due to an error at %s (%s)",
                            checked_var_trapinfo->trap_desc->comment(),
                            checked_var_trapinfo->message->base());
        }
      }
    }

    if (! seen_undef_) {
      undef_cnt_++;
      seen_undef_ = true;
      // outputter index is stored as a variable
      szl_int out_index = state_.gp_->at(undef_cnt_index_)->as_int()->val();
      if (undef_cnt_index_ > 0 && outputter(out_index)->emitter() != NULL) {
        Val** tmp = state_.sp_;  // for working safely
        Engine::push_szl_int(tmp, this, 1);
        // ignore emitter errors here!
        outputter(out_index)->Emit(tmp);
        // make sure we haven't screwed up somehow
        if ((mode_ & kNative) == 0)
          assert(state_.sp_ == state_.fp_->stack() - desc->stack_height());
        else
          // the interpreter stack may hold some arguments to intrinsics
          assert(state_.sp_ <= state_.gp_->stack());

        assert(state_.sp_ == tmp);
      }
    }
    // _undef_details can cause multiple emits per record
    if (undef_details_index_ > 0) {
      // outputter index is stored as a variable
      szl_int out_index = state_.gp_->at(undef_details_index_)->as_int()->val();
      if (outputter(out_index)->emitter() != NULL) {
        Val** tmp = state_.sp_;
        Engine::push_szl_int(tmp, this, 1);
        // create a maximally informative message, and convert it
        // to an Array as in CodeGen::DoStringLiteral
        int len = strlen(desc->comment()) + strlen(info) + 3;
        char* buf = new char[len+1];
        F.snprint(buf, len+1, "%s (%s)", desc->comment(), info);
        StringVal* s = Factory::NewStringC(this, buf);
        delete[] buf;
        // simulate push
        Engine::push(tmp, s);
        // ignore emitter errors here!
        outputter(out_index)->Emit(tmp);
        // make sure we haven't screwed up somehow
        if ((mode_ & kNative) == 0)
          assert(state_.sp_ == state_.fp_->stack() - desc->stack_height());
        else
          // the interpreter stack may hold some arguments to intrinsics
          assert(state_.sp_ <= state_.gp_->stack());

        assert(state_.sp_ == tmp);
      }
    }

    // terminate if trap is fatal
    // note: if the proc is not initialized, we must not ignore undefs
    bool ignore_undefs = is_initialized() && ((mode() & kIgnoreUndefs) != 0);
    if (! ignore_undefs) {
      PrintStackTrace();
      // terminate Engine::Execute w/ error message
      status_ = FAILED;
      trap_info_ =  PrintError("undefined value at %s (%s)", desc->comment(),
                               info);
      return;
    }
  }

  if (is_fatal) {
    PrintStackTrace();
    status_ = FAILED;
  } else {
    // reset trap info
    trap_info_ = NULL;
    // tracing
    if (FLAGS_trace_traps) {
      F.print("trap @ %p (%d): ", trap_pc_, trap_offs);
      desc->print();
    }

    // done
    status_ = SUSPENDED;
  }
}


// Print a stack trace, but only once.
void Proc::PrintStackTrace() {
  // We can get here twice if we get an assertion failure in native mode,
  // which unwinds the stack earlier and therefore must call PrintStackTrace
  // earlier; later, Execute will call it again during normal clean up.
  // This flag test prevents printing it twice.
  if (stack_trace_printed_)
    return;
  stack_trace_printed_ = true;
  if ((mode_ & kNative) == 0) {
    // the static frame is in the interpreter stack, even in native mode

    // trap_pc_ might not be set in nonative mode
    // See comment in Proc::HandleTrap for why the following is ok
    trap_pc_ = state_.pc_ - 1;
    FrameIterator::PrintStack(2, FLAGS_stacktrace_length, this,
                              state_.fp_, state_.sp_, trap_pc_);
  } else {
    FrameIterator::PrintStack(2, FLAGS_stacktrace_length, this,
                              native_.fp_, native_.sp_, trap_pc_);
  }
}


// This is really needed by the Scanner, but Proc provides the only
// global state with the right lifetime.
bool Proc::AlreadyIncluded(const char* file_name) {
  if (!FLAGS_ignore_multiple_inclusion)  // always include the file
    return false;
  struct stat stat_buf;
  char real_path[PATH_MAX];
  if (stat(file_name, &stat_buf) < 0 ||
      NULL == realpath(file_name, real_path))
    return false;  // Let error handling happen higher up

  vector<Inode>*& file_inodes =
      // is top level SuperSawzall code?
      (mode_ & kPipeline) != 0 && !is_sawzall_job_being_parsed ?
      sszl_file_inodes_ : szl_file_inodes_;

  // lazily allocate file inodes (not needed by Process)
  if (file_inodes == NULL)
    file_inodes = new vector<struct Inode>;

  // search existing files
  for (int i = 0; i < file_inodes->size(); i++) {
    Inode& node = file_inodes->at(i);
    if ((node.dev == stat_buf.st_dev && node.ino == stat_buf.st_ino) ||
        node.real_path == real_path) {
      if (!node.reported) {
        if (FLAGS_show_multiple_inclusion_warnings)
          F.fprint(2, "Warning: multiple inclusion of %s\n", file_name);
        node.reported = true;
      }
      return true;
    }
  }
  // add to list
  Inode inode;
  inode.dev = stat_buf.st_dev;
  inode.ino = stat_buf.st_ino;
  inode.real_path = real_path;
  inode.reported = false;
  file_inodes->push_back(inode);
  return false;
}


void Proc::RegisterTupleType(TupleType* t) {
  if (tuple_types_ == NULL)
    tuple_types_ = List<TupleType*>::New(this);
  tuple_types_->Append(t);
}


void Proc::ApplyToAllTupleTypes(void (TupleType::*f)(Proc* proc)) {
  if (tuple_types_ != NULL)
    for (int i = tuple_types_->length(); i-- > 0; )
      (tuple_types_->at(i)->*f)(this);
}


void Proc::RegisterRegexp(void* obj) {
  if (regexp_objects_ == NULL)
    regexp_objects_ = List<void*>::New(this);
  regexp_objects_->Append(obj);
}

void Proc::SetRandomSeed(int32 seed) {
  // From acmrandom.h:  "If 'seed' is not in [1, 2^31-2], the range of numbers
  // normally generated, it will be silently set to 1."
  rand_->Reset(seed & 0x7fffffff);
}

void Proc::AddInput(const char* key, const char* value, int value_size) {
  AddInput(key, Factory::NewBytesInit(this, value_size, value));
}

// Internal version of AddInput, for when we already have a BytesVal.
// This will decrement the reference count of value when it is no longer needed.
void Proc::AddInput(const char* key, BytesVal* value) {
  // If this key already exists, overwrite its value
  for (int i = 0; i < additional_input_->size(); ++i) {
    AdditionalInput* item = &additional_input_->at(i);
    if (item->key->length() == strlen(key) &&
        strncmp(item->key->base(), key, item->key->length()) == 0) {
      item->value->dec_ref();
      item->value = value;
      return;
    }
  }

  // Otherwise add a new pair
  AdditionalInput item;
  item.key = Factory::NewStringC(this, key);
  item.value = value;
  additional_input_->push_back(item);
}

void Proc::ClearInputs() {
  for (int i = 0; i < additional_input_->size(); ++i) {
    AdditionalInput* item = &additional_input_->at(i);
    item->key->dec_ref();
    item->value->dec_ref();
  }
  additional_input_->clear();
}

void Proc::ClearVarTrapinfo() {
  // Only clear trap info if some traps have occurred since last clearing.
  if (var_trapinfo_ != NULL && clear_var_trapinfo_) {
    for (int i = 0; i < var_trapinfo_count_; i++) {
      BytesVal* val = var_trapinfo_[i].message;
      if (val != NULL)
        val->dec_ref();
    }
    memset(var_trapinfo_, '\0', sizeof(VarTrapinfo)*var_trapinfo_count_);
    clear_var_trapinfo_ = false;
  }
}

BytesVal* Proc::GetInput(StringVal* key) {
  for (int i = 0; i < additional_input_->size(); ++i) {
    AdditionalInput* item = &additional_input_->at(i);
    if (SymbolTable::string_form()->IsEqual(item->key, key)) {
      item->value->inc_ref();
      return item->value;
    }
  }

  return NULL;
}

void Proc::NowParsingSawzallJob() {
  is_sawzall_job_being_parsed = true;
  assert(szl_file_inodes_ == NULL);
}

// Resets szl file inodes, so includes can be processed from scratch when
// the next job is parsed
void Proc::DoneParsingSawzallJob() {
  is_sawzall_job_being_parsed = false;
  delete szl_file_inodes_;
  szl_file_inodes_ = NULL;
}

void Proc::ClearCache() {
  for (int i = 0; i < intrinsic_cache_.size(); i++)
    delete intrinsic_cache_[i];
}

IntrinsicCache* Proc::Lookup(const char* name) {
  // No need for a lock, as we assume that a Proc object
  // will be accessed only by one thread at a time.
  for (int i = 0; i < intrinsic_slots_.size(); ++i)
    if (strcmp(intrinsic_slots_[i], name) == 0)
      return intrinsic_cache_[i];
  return NULL;
}

void Proc::Update(const char* name, IntrinsicCache* entry) {
  // No need for a lock, as we assume that a Proc object
  // will be accessed only by one thread at a time.
  for (int i = 0; i < intrinsic_slots_.size(); ++i)
    if (strcmp(intrinsic_slots_[i], name) == 0) {
      delete intrinsic_cache_[i];
      intrinsic_cache_[i] = entry;
      return;
    }
  intrinsic_slots_.push_back(name);
  intrinsic_cache_.push_back(entry);
}

void Proc::set_env_value(const string& name, const string& value) {
  for (int i = 0; i < env_values_.size(); ++i) {
    if (env_values_[i].name == name) {
      env_values_[i].value = value;
      return;
    }
  }
  env_values_.push_back(NameValuePair(name, value));
}

const char* Proc::env_value(const string& name) const {
  for (int i = 0; i < env_values_.size(); ++i) {
    if (env_values_[i].name == name)
      return env_values_[i].value.c_str();
  }
  return NULL;
}

void Proc::clear_env_values() {
  env_values_.clear();
}

}  // namespace sawzall
