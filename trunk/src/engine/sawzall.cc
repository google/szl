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

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <vector>
#include <string>

#include "engine/globals.h"
#include "public/commandlineflags.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "utilities/strutils.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/code.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/backendtype.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/frame.h"
#include "public/sawzall.h"
#include "engine/tracer.h"
#include "engine/codegen.h"
#include "engine/compiler.h"
#include "public/emitterinterface.h"
#include "engine/outputter.h"
#include "engine/intrinsic.h"
#include "engine/help.h"
#include "engine/linecount.h"
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "public/value.h"
#include "engine/engine.h"
#include "engine/profile.h"
#include "engine/debugger.h"


DEFINE_bool(test_backend_type_conversion, false,
            "perform backend type conversions for output types"
            "as sawzall-to-backend-to-sawzall-to-backend type conversions "
            "for testing purposes");


namespace sawzall {


// -----------------------------------------------------------------------------
// Global Sawzall interface and data types

static void Initialize() {
  InstallFmts();
  SymbolTable::Initialize();
  Intrinsics::Initialize();
}


const char* Version() {
  return  "Sawzall 1.0 - ";
}


bool RegisterTableType(const char* name, bool has_param, bool has_weight) {
  return SymbolTable::RegisterTableType(name, has_param, has_weight) != NULL;
}


// Register standard table types.
void RegisterStandardTableTypes() {
  CHECK(sawzall::RegisterTableType("bootstrapsum", true, true));
  CHECK(sawzall::RegisterTableType("collection", false, false));
  CHECK(sawzall::RegisterTableType("distinctsample", true, true));
  CHECK(sawzall::RegisterTableType("inversehistogram", true, true));
  CHECK(sawzall::RegisterTableType("maximum", true, true));
  CHECK(sawzall::RegisterTableType("minimum", true, true));
  CHECK(sawzall::RegisterTableType("mrcounter", false, false));
  CHECK(sawzall::RegisterTableType("quantile", true, false));
  CHECK(sawzall::RegisterTableType("recordio", true, false));
  CHECK(sawzall::RegisterTableType("sample", true, false));
  CHECK(sawzall::RegisterTableType("set", true, false));
  CHECK(sawzall::RegisterTableType("sum", false, false));
  CHECK(sawzall::RegisterTableType("text", false, false));
  CHECK(sawzall::RegisterTableType("top", true, true));
  CHECK(sawzall::RegisterTableType("unique", true, false));
  CHECK(sawzall::RegisterTableType("weightedsample", true, true));
}


// Register emitters for all of exe's backend tables
// that can be evaluated at compile-time.
void RegisterEmitters(sawzall::Process* process) {
  const vector<sawzall::TableInfo*>* tables = process->exe()->tableinfo();
  for (int i = 0; i < tables->size(); i++) {
    sawzall::TableInfo* tabinfo = (*tables)[i];
    if (!tabinfo->is_evaluated())
      continue;  // delay emitter installation until run-time
    string error;
    sawzall::Emitter* emitter = process->emitter_factory()->
        NewEmitter(tabinfo, &error);
    if (emitter == NULL) {
      fprintf(stderr, "%s\n", error.c_str());
      exit(1);
    }
    process->RegisterEmitterOrDie(tabinfo->name(), emitter);
  }
}


void PrintUniverse() {
  Help::PrintUniverse();
}


bool Explain(const char* name) {
  return Help::Explain(name);
}


void PrintHtmlDocumentation() {
  Help::PrintHtmlDocumentation("Sawzall Predefined Identifiers");
}


// -----------------------------------------------------------------------------
// Implementation of TableInfo



TableInfo* TableInfo::New(Proc* proc, const char* name, OutputType* type) {
  assert(name != NULL);
  assert(type != NULL);
  TableInfo* t = NEW(proc, TableInfo);
  t->name_ = name;
  t->type_ = type;
  t->proc_ = proc;
  return t;
}


const string& TableInfo::type_string() {
  if (type_string_.empty()) {
    type_string_ = BackendTypeFor(type_);
    if (FLAGS_test_backend_type_conversion) {
      Type* test = TypeFor(proc_, SymbolTable::init_file_line(), type_string_);
      string test_backend_type = BackendTypeFor(test);
      CHECK(type_string_ == test_backend_type);
    }
  }
  assert(!type_string_.empty());
  return type_string_;
}


void TableInfo::Print() {
  // We could simply use the "%T" format and print type_ here;
  // but we are interested in seeing the decoded value of the
  // type_string_ (should be the same at "%T" of type_).
  const string s = TypeStringToTypeSpec(type_string());
  F.print("%s: %s;\n", name(), s.c_str());
}


bool TableInfo::is_evaluated() {
  return type_->is_evaluated_param();
}


// -----------------------------------------------------------------------------
// Implementation of ProfileInfo

int ProfileInfo::top_ticks_at(int pc_index) const {
  return profile_->ticks_at(pc_index)->top;
}


int ProfileInfo::all_ticks_at(int pc_index) const {
  return profile_->ticks_at(pc_index)->all;
}


int ProfileInfo::length() const {
  return profile_->length();
}


int ProfileInfo::FunctionIndex(int pc_index) const {
  const int offset = pc_index * CodeDesc::kAlignment;
  Instr* pc = code_->base() + offset;
  return code_->DescForInstr(pc)->index();
}


const char* ProfileInfo::FunctionName(int f_index) const {
  const char* fun = "INIT";
  CodeDesc* desc = code_->DescForIndex(f_index);
  if (desc != NULL && desc->function() != NULL)
    fun = desc->function()->name();
  return fun;
}


ProfileInfo* ProfileInfo::New(Proc* proc) {
  ProfileInfo* p = NEW(proc, ProfileInfo);
  p->profile_ = proc->profile();
  p->code_ = proc->code();
  assert(p->profile_ != NULL);
  assert(p->code_ != NULL);
  return p;
}


// -----------------------------------------------------------------------------
// Implementation of DebuggerAPI


DebuggerAPI::DebuggerAPI(Proc* proc) {
  debugger_ = proc->debugger();
}


void DebuggerAPI::Continue() {
  return debugger_->Continue();
}


bool DebuggerAPI::Step() {
  return debugger_->Step();
}


int DebuggerAPI::CurrentLineNumber() {
  return debugger_->CurrentLineNumber();
}


const char* DebuggerAPI::CurrentFileName() {
  return debugger_->CurrentFileName();
}


const char* DebuggerAPI::CurrentFunctionName() {
  return debugger_->CurrentFunctionName();
}


// -----------------------------------------------------------------------------
// Implementation of Executable

Executable::Executable(const char* file_name, const char* source, int mode,
                       ErrorHandler* error_handler) {
  if (FLAGS_restrict)
    mode |= kSecure;
  // Note: Procs used to create an Executable are persistent.
  // Proc is explicitly deallocated by destructor.
  proc_ = new Proc(mode | Proc::kPersistent, error_handler);
  proc_->set_name("Sawzall::Executable");
  proc_->set_executable(this);
  compilation_ = Compilation::New(proc_, (mode & kDebug) != 0);
  bool leave_main_unreturned = (mode & kDoCalls) != 0;
  fingerprint_ = kIllegalFprint;
  if (source != NULL) {
    // file_name[0] is comment only - compile the source
    compilation_->CompileStr(file_name, source, leave_main_unreturned);
  } else {
    // file_name may be a comma separated list.
    // Compile() takes an array of char* pointers, so we convert here.
    vector<const char*> files;
    char* file_name_copy = strdup(file_name);
    char* p = file_name_copy;
    while (true) {
      files.push_back(p);
      if ((p = strchr(p, ',')) == NULL)
        break;
      *p++ = '\0';
    }
    compilation_->Compile(&files[0], files.size(), leave_main_unreturned);
    free(file_name_copy);
  }
  proc_->set_code(compilation_->code());
  proc_->set_statics_size(compilation_->statics_size());
  MakeTables();
}

// Define pure virtual destructor for ErrorHandler to avoid linking issues.
ErrorHandler::~ErrorHandler() { }

Executable::~Executable() {
  // all objects associated with this executable were either
  // explicitly deleted before or have been allocated on the
  // proc_ heap and will be deleted when proc_ is deleted
  compilation_->Finalize();
  delete proc_;
  delete tableinfo_;
}


void Executable::PrintSource() {
  assert(is_executable());
  F.print("%N", compilation_->program());
}


const char* Executable::RawSource() {
  // always possible (even in the presence of compilation errors)
  return compilation_->source();
}


const char* Executable::Source() {
  assert(is_executable());
  return proc_->PrintString("%N", compilation_->program());
}


string Executable::InputProtoName() const {
  assert(is_executable());
  TupleType* input_proto = compilation_->symbol_table()->input_proto();
  if (input_proto == NULL)
    return "";
  else
    return input_proto->type_name()->name();
}


static void AddReferencedTupleFieldNames(TupleType* tuple,
                                         const string& prefix,
                                         vector<string>* field_names,
                                         bool internal_fields) {
  List<Field*>* fields = tuple->fields();
  for (int i = 0; i < fields->length(); i++) {
    Field* field = fields->at(i);
    if (field->read()) {
      const szl_string field_name =
          field->name() == NULL ? "<unnamed>" : field->name();
      const string prefixed_name = prefix + field_name;
      // For arrays use the array element for type checks but use the original
      // field name instead of the optional array field name.
      while (field->type()->is_array())
        field = field->type()->as_array()->elem();
      if (!field->recursive()) {
        // For tuples and arrays of tuples only, consider the tuple fields.
        // (Note that the array field name, if any, is irrelevant here.)
        TupleType* type = field->type()->as_tuple();
        if (type != NULL) {
          if (internal_fields)
            field_names->push_back(prefixed_name);
          AddReferencedTupleFieldNames(type, prefixed_name + ".", field_names,
                                       internal_fields);
        } else {
          field_names->push_back(prefixed_name);
        }
      }
    }
  }
}


void Executable::GetReferencedTupleFieldNames(const string& tuple_name,
                                              vector<string>* field_names,
                                              bool internal_fields) const {
  const List<TupleType*>* tuple_types = proc_->GetTupleTypes();
  if (tuple_types != NULL) {
    for (int i = tuple_types->length(); i-- > 0; ) {
      TupleType* t = tuple_types->at(i);
      if (t->type_name() != NULL && tuple_name == t->type_name()->name()) {
        AddReferencedTupleFieldNames(t, "", field_names, internal_fields);
        break;
      }
    }
  }
}


void Executable::PrintCode() {
  assert(is_executable());
  compilation_->code()->Disassemble();
}


void Executable::PrintTables() {
  assert(is_executable());
  for (int i = 0; i < tableinfo_->size(); i++)
    tableinfo_->at(i)->Print();
}


bool Executable::GenerateELF(const char* name, uintptr_t* map_beg,
                             uintptr_t* map_end, int* map_offset) {
  assert(is_executable());
  return compilation_->code()->GenerateELF(name, map_beg, map_end, map_offset);
}


void Executable::PrintInputProtoName() {
  assert(is_executable());
  string pb_name = InputProtoName();
  if (pb_name.empty())
    pb_name = "<none>";
  F.print("Protocol buffer type associated with input: %q\n", pb_name.c_str());
}


void Executable::PrintReferencedTupleFieldNames(const string& tuple_name,
                                                bool internal_fields) {
  assert(is_executable());
  string name = tuple_name;
  // "<input>" indicates the type to which "input" was converted.
  if (tuple_name == "<input>") {
    TupleType* input_proto = compilation_->symbol_table()->input_proto();
    if (input_proto == NULL)
      return;
    name = input_proto->type_name()->name();
  }

  // "<all>" indicates all named tuples; else match just one tuple name.
  const List<TupleType*>* tuple_types = proc_->GetTupleTypes();
  if (tuple_types != NULL) {
    bool match_all = (name == "<all>");
    for (int i = tuple_types->length(); i-- > 0; ) {
      TupleType* t = tuple_types->at(i);
      if (t->type_name() != NULL &&
          (name == t->type_name()->name() || match_all)) {
        vector<string> field_names;
        AddReferencedTupleFieldNames(t, "", &field_names, internal_fields);
        F.print("Fields referenced in tuple %q:\n", t->type_name()->name());
        for (int i = 0; i < field_names.size(); i++)
          F.print("  %q\n", field_names[i].c_str());
      }
    }
  }
}


const ProfileInfo* Executable::profile() const {
  assert(is_executable());
  ProfileInfo* profile = NULL;
  if (proc_->profile() != NULL)
    profile = ProfileInfo::New(proc_);  // explicitly deallocated by client
  return profile;
}


bool Executable::is_executable() const {
  return compilation_->error_count() == 0;
}


Fprint Executable::fingerprint() {
  assert(is_executable());
  if (fingerprint_ == kIllegalFprint) {
    const char* source = Source();
    fingerprint_ = FingerprintString(source, strlen(source));
  }
  return fingerprint_;
}


void Executable::MakeTables() {
  // We only collect the tables that use an emitter
  // because they are the only ones for which the
  // client needs to install an emitter.
  // Also, for historical reasons, the interface uses
  // C++ STL vectors, while the Sawzall implementation
  // is using its own lists.
  tableinfo_ = new vector<TableInfo*>;  // explicitly deallocated by destructor
  OutputTables* tables = compilation_->tables();
  for (int i = 0; i < tables->length(); i++) {
    TableInfo* t = tables->at(i);
    if (t->type()->uses_emitter())
      tableinfo_->push_back(t);
  }
}


// -----------------------------------------------------------------------------
// Implementation of Process

Process::Process(Executable* exe, void* context) {
  CHECK(exe != NULL && exe->is_executable());
  // Note: Procs used to create a Process are not persistent.
  proc_ = exe->proc_->Fork(exe->proc_->mode() & ~Proc::kPersistent);
  proc_->set_name("Sawzall::Process");
  proc_->set_context(context);
  proc_->set_executable(exe);
  proc_->AllocateOutputters(exe->compilation_->tables());
  exe_ = exe;
  do_call_state_ = UNINITIALIZED;
}


// Obsolete version pending fixing clients.
Process::Process(Executable* exe, bool ignore_undefs, void* context) {
  CHECK(exe != NULL && exe->is_executable());
  CHECK_EQ(ignore_undefs, (exe->proc_->mode() & Proc::kIgnoreUndefs) != 0);
  // Note: Procs used to create a Process are not persistent.
  proc_ = exe->proc_->Fork(exe->proc_->mode() & ~Proc::kPersistent);
  proc_->set_name("Sawzall::Process");
  proc_->set_context(context);
  proc_->set_executable(exe);
  proc_->AllocateOutputters(exe->compilation_->tables());
  exe_ = exe;
  do_call_state_ = UNINITIALIZED;
}


Process::~Process() {
  delete proc_;
}


void Process::Epilog(bool source) {
  // emit counts using the saved Emitter (the stack is long gone)
  if (source) {
    proc_->linecount()->Emit(exe_->compilation_->source());
  } else {
    proc_->linecount()->Emit(NULL);
  }
  proc_->linecount()->ResetCounters();
}


const ProfileInfo* Process::profile() const {
  ProfileInfo* profile = NULL;
  if (proc_->profile() != NULL)
    profile = ProfileInfo::New(proc_);
  return profile;
}


DebuggerAPI* Process::debugger() {
  DebuggerAPI* debugger = NULL;
  if (proc_->debugger() != NULL) {
    debugger = new DebuggerAPI(proc_);  // explicitly deallocated by client
  }
  return debugger;
}


void* Process::context() const {
  return proc_->context();
}


void Process::set_memory_limit(int64 memory_limit) {
  proc_->set_memory_limit(memory_limit);
}


void Process::set_emitter_factory(EmitterFactory* emitter_factory) {
  proc_->set_emitter_factory(emitter_factory);
}


EmitterFactory* Process::emitter_factory() const {
  return proc_->emitter_factory();
}


void Process::DieIfFalse(bool b) {
  if (!b) {
    fprintf(stderr, "szl: fatal: %s\n", error_msg());
    exit(1);
  }
}


// register an emitter for a table for the process.
bool Process::RegisterEmitter(const char* name, Emitter* emitter) {
  OutputTables* tables = exe_->compilation_->tables();
  for (int i = 0; i < tables->length(); i++) {
    TableInfo* t = tables->at(i);
    if (strcmp(t->name(), name) == 0 && t->type()->uses_emitter()) {
      assert(strcmp(proc_->outputter(i)->name(), name) == 0);
      proc_->outputter(i)->set_emitter(emitter);
      if (strcmp(name, "_line_counts") == 0)  // need to remember this emitter
        proc_->linecount()->set_emitter(emitter);
      return true;
    }
  }
  // TODO: For now we don't provide an error message here - need to
  // clean up error handling in Proc, then this could be done cleanly.
  //
  // proc_->PrintString("registering non-existent table '%s'", name)
  return false;
}


void Process::RegisterEmitterOrDie(const char* name, Emitter* emitter) {
  DieIfFalse(RegisterEmitter(name, emitter));
}


void Process::SetRandomSeed(int32 seed) {
  proc_->SetRandomSeed(seed);
}


void Process::SetupInitialization() {
  CHECK(do_call_state_ == UNINITIALIZED || do_call_state_ == ILLEGAL)
      << "cannot perform non-DoCalls() operations "
      << "after calling InitializeDoCalls()";
  do_call_state_ = ILLEGAL;
  proc_->SetupInitialization();
}


void Process::SetupRun(const char* input_ptr, size_t input_size,
                       const char* key_ptr, size_t key_size) {
  proc_->SetupRun(input_ptr, input_size, key_ptr, key_size);
}


bool Process::Execute(int max_steps, int* num_steps) {
  return proc_->Execute(max_steps, num_steps) >= Proc::TERMINATED;
}


bool Process::Initialize() {
  SetupInitialization();
  while (! Execute(kint32max, NULL))
    ;
  // Record the current resource statistics as a reference point
  // for the initialization baseline.
  proc_->set_initialized_stats();
  proc_->set_current_stats();  // baseline for first record
  proc_->heap()->ResetCounters();
  return proc_->status() == Proc::TERMINATED;
}


void Process::InitializeOrDie() {
  DieIfFalse(Initialize());
}


uint64 Process::InitializationFingerprint() const {
  // caller cannot use szl_fingerprint; convert, but verify no bits will be lost
  COMPILE_ASSERT(sizeof(szl_fingerprint) == sizeof(uint64),
                 fingerprint_size_mismatch);
  return implicit_cast<uint64>(proc_->InitializationFingerprint());
}


bool Process::Run(const char* input_ptr, size_t input_size,
                  const char* key_ptr, size_t key_size) {
  SetupRun(input_ptr, input_size, key_ptr, key_size);
  while (! Execute(kint32max, NULL))
    ;
  // Update the current resource statistics.
  proc_->set_current_stats();
  proc_->heap()->ResetCounters();
  return proc_->status() == Proc::TERMINATED;
}


bool Process::RunAlreadySetup() {
  while (! Execute(kint32max, NULL))
    ;
  // Update the current resource statistics.
  proc_->set_current_stats();
  proc_->heap()->ResetCounters();
  return proc_->status() == Proc::TERMINATED;
}

void Process::RunOrDie(const char* input_ptr, size_t input_size,
                       const char* key_ptr, size_t key_size) {
  DieIfFalse(Run(input_ptr, input_size, key_ptr, key_size));
}


bool Process::InitializeDoCalls() {
  CHECK(do_call_state_ == UNINITIALIZED)
      << "calling InitializeDoCalls() after non-DoCalls() initialization";
  if (!Initialize() || !Run()) return false;
  do_call_state_ = INITIALIZED;
  return true;
}


const FunctionDecl* Process::LookupFunction(const char* function_name) {
  CHECK(do_call_state_ >= INITIALIZED)
      << "calling LookupFunction() before InitializeDoCalls() has been invoked";
  const VarDecl* fun_decl = proc()->LookupFunction(function_name);
  if (fun_decl == NULL)
    return NULL;
  else
    return FunctionDecl::New(fun_decl);
}


CallContext* Process::SetupCall() {
  CHECK(do_call_state_ != UNINITIALIZED && do_call_state_ != ILLEGAL)
      << "calling SetupCall() before InitializeDoCalls() has been invoked";
  CHECK(do_call_state_ != SETUP && do_call_state_ != CALLED &&
        do_call_state_ != STARTED && do_call_state_ != CONTINUED)
      << "re-calling SetupCall() without first calling FinishCall()";
  proc()->SetupCall();
  do_call_state_ = SETUP;
  CallContext* context = new CallContext(proc());
  return context;
}


const Value* Process::DoCall(CallContext* context,
                             const FunctionDecl* fun_decl,
                             const Value* const* args,
                             int num_args) {
  CHECK(do_call_state_ == SETUP)
      << "calling DoCall() without first calling SetupCall()";
  Val* result = proc()->DoCall(fun_decl->fun_decl(),
                               Value::val_array(args),
                               num_args);
  context->Record(result);
  do_call_state_ = CALLED;
  return Value::New(result);
}


void Process::StartCall(CallContext* context,
                        const FunctionDecl* fun_decl,
                        const Value* const* args,
                        int num_args) {
  CHECK(do_call_state_ != STARTED)
      << "calling StartCall() twice before calling FinishCall()";
  CHECK(do_call_state_ == SETUP)
      << "calling StartCall() without first calling SetupCall()";
  proc()->StartCall(fun_decl->fun_decl(), Value::val_array(args), num_args);
  do_call_state_ = STARTED;
}


bool Process::ContinueCall(CallContext* context,
                           int max_steps,
                           int* num_steps,
                           const Value** result) {
  CHECK(do_call_state_ == STARTED || do_call_state_ == CONTINUED)
      << "calling ContinueCall() without first calling StartCall()";
  Val* val = proc()->ContinueCall(max_steps, num_steps);
  Proc::Status status = proc()->status();
  bool finished = (status == Proc::TERMINATED || status == Proc::FAILED);
  if (finished) {
    context->Record(val);
    *result = Value::New(val);
  }
  do_call_state_ = CONTINUED;
  return finished;
}


void Process::FinishCall(CallContext* context) {
  CHECK(do_call_state_ == CALLED || do_call_state_ == SETUP ||
        do_call_state_ == STARTED || do_call_state_ == CONTINUED)
      << "calling FinishCall() without first calling SetupCall()";
  delete context;  // Decrement ref counts before cleaning up the Proc state.
  proc()->FinishCall();
  do_call_state_ = FINISHED;
}


const char* Process::error_msg() const  {
  return proc_->error_msg();
}


// Proc is an opaque (incomplete) type in sawzall.h, hence this
uint64 Process::ProcUndefCnt() {
  return proc_->UndefCnt();
}


uint64 Process::ProcProtoBytesRead() {
  return proc_->proto_bytes_read();
}


uint64 Process::ProcProtoBytesSkipped() {
  return proc_->proto_bytes_skipped();
}


void Process::ProcClearProtoBytesRead() {
  proc_->clear_proto_bytes_read();
}


void Process::ProcClearProtoBytesSkipped() {
  proc_->clear_proto_bytes_skipped();
}

void Process::SetDisallowedReadPaths(const vector<string>& disallowed) {
  proc_->set_disallowed_read_paths(disallowed);
}

void Process::set_env_value(const string& name, const string& value) {
  proc_->set_env_value(name, value);
}

const char* Process::env_value(const string& name) const {
  return proc_->env_value(name);
}

void Process::clear_env_values() {
  proc_->clear_env_values();
}


}  // namespace sawzall


// Note: This must happen outside the namespace
// to avoid linker/name mangling problems.
REGISTER_MODULE_INITIALIZER(
  Sawzall,
  { sawzall::Initialize();
  }
);
