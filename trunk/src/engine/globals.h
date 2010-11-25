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

#include <string.h>   // for strcmp()
#include <inttypes.h>  // for uintptr_t
#include <stdarg.h>  // for va_list

#include "public/porting.h"
#include "public/commandlineflags.h"

#include "fmt/fmt.h"

// Global Sawzall definitions

// -----------------------------------------------------------------------------
// Component global flags
// These flags are valid for all instances Sawzall Executables
// and Processes - this shouldn't matter because these flags
// are used mainly for debugging. Process-specific flags can
// be set for Processes only (see sawzall::Process). The flags
// are declared outside the Sawzall namespace to make sure
// we see clashes with similarly named flags elsewhere (other-
// wise it is unclear which flags are set).

DECLARE_bool(trace_code);  // trace instructions during execution
DECLARE_bool(trace_traps);  // trace trap resolution (debugging)
DECLARE_int32(heap_chunk_size);  // heap chunk size in KB
DECLARE_int32(stack_size);  // stack size in KB
DECLARE_int32(stacktrace_length);  // maximum stacktrace length
DECLARE_bool(debug_whens); // print when statements before and after rewriting
DECLARE_bool(restrict);  // restrict access to insecure features
DECLARE_bool(ignore_multiple_inclusion);  // ignored 2nd and subsequent
DECLARE_bool(show_multiple_inclusion_warnings);  // show include warnings
DECLARE_bool(trace_refs);  // trace reference counts; used in dbg mode only

// allocate default values for proto buffer TupleTypes
DECLARE_bool(preallocate_default_proto);


namespace sawzall {

// -----------------------------------------------------------------------------
// Node hierarchy
// (ordered alphabetically within subclasses)

class Node;
  class Expr;
    class BadExpr;
    class Binary;
    class Call;
    class Composite;
    class Conversion;
    class Dollar;
    class Function;
    class RuntimeGuard;
    class Index;
    class New;
    class Regex;
    class Saw;
    class Selector;
    class Slice;
    class StatExpr;
    class Object;
      class Field;
      class Intrinsic;
      class Literal;
      class TypeName;
      class Variable;
        class TempVariable;
  class Statement;
    class Assignment;
    class Block;
    class Break;
    class Continue;
    class Decl;
      class TypeDecl;
      class VarDecl;
        class QuantVarDecl;
    class Emit;
    class Empty;
    class ExprStat;
    class If;
    class Increment;
    class Proto;
    class Result;
    class Return;
    class When;
    class BreakableStatement;
      class Loop;
      class Switch;


// -----------------------------------------------------------------------------
// Type hierarchy
// (ordered alphabetically within subclasses)

class Type;
  class ArrayType;
  class BadType;
  class BasicType;
  class FunctionType;
  class IncompleteType;
  class MapType;
  class OutputType;
  class TupleType;


// -----------------------------------------------------------------------------
// Val hierarchy
// (ordered alphabetically within subclasses)

class Val;
  class IndexableVal;
    class ArrayVal;
    class BytesVal;
    class StringVal;
  class BoolVal;
  class BytesVal;
  class FingerprintVal;
  class FloatVal;
  class IntVal;
  class UIntVal;
  class TimeVal;
  class FunctionVal;
  class MapVal;
  class TupleVal;
  class ClosureVal;


// -----------------------------------------------------------------------------
// Form hierarchy
// (ordered alphabetically within subclasses)

class Form;
  class ArrayForm;
  class BoolForm;
  class BytesForm;
  class FingerprintForm;
  class FloatForm;
  class IntForm;
  class UIntForm;
  class StringForm;
  class TimeForm;
  class MapForm;
  class TupleForm;
  class ClosureForm;


// -----------------------------------------------------------------------------
// Frequently used classes
// (ordered alphabetically)

class Closure;
class Code;
class CodeDesc;
class Compilation;
class Debugger;
class Emitter;
class EmitterFactory;
class Engine;
class Error;
class Executable;
class Frame;
class Histogram;
class LineCount;
class Memory;
class NFrame;
class OutputTable;
class Outputter;
class Proc;
class Profile;
class Scope;
class SourceFile;
class TableInfo;


// Instr* points to an Opcode (we don't use Opcode*
// because we must make sure that sizeof(Instr) == 1!)
typedef unsigned char Instr;


// -----------------------------------------------------------------------------
// Error handling

void InstallFmts();  // call before any fmt printing
void FatalError(const char*fmt, ...);


// -----------------------------------------------------------------------------
// Defensive programming support

#define Untested() \
  F.print("%s:%d: untested\n", __FILE__, __LINE__)


#define Unimplemented() \
  FatalError("%s:%d: unimplemented\n", __FILE__, __LINE__)


#define ShouldNotReachHere() \
  FatalError("%s:%d: should not reach here", __FILE__, __LINE__)


// -----------------------------------------------------------------------------
// Sawzall basic types

typedef intptr_t smi;  // sizeof(smi) == sizeof(void*)
typedef int64 szl_int;  // sizeof(szl_int) >= sizeof(smi)
typedef uint64 szl_uint;
typedef double szl_float;
typedef const char* szl_string;
typedef uint64 szl_time;
typedef uint64 szl_fingerprint;


// -----------------------------------------------------------------------------
// Allocate doesn't require knowledge of the Proc data structure
// for allocation because its implementation is hidden. Should be
// used when proc.h and/or memory.h cannot be included (e.g.
// because of cycles).
void* Allocate(Proc* proc, size_t size);
// Same for Deallocate.
void Deallocate(Proc* proc, void* p);


// -----------------------------------------------------------------------------
// Some general definitions

extern Fmt::Formatter F;

// Make TRACE_REF a macro so it vanishes completely in `opt' mode.
// str must be a literal string.
#ifdef NDEBUG
  #define TRACE_REF(str, val)
#else
  #define TRACE_REF(str, val) \
    if (FLAGS_trace_refs) \
      F.print(str ": %p has ref %d\n", (val), (val)->ref());
#endif

const int kMaxFormatLen = 32 ;  // Plenty big enough to store %23.2


// String generated when formatting a bad time value.
extern const char kStringForInvalidTime[];

// Canonical format strings for Sawzall datatypes
#define SZL_FINGERPRINT_FMT "0x%.16llxP"
#define SZL_TIME_FMT "%lluT"
#define SZL_UINT_FMT "%lluU"

}  // namespace sawzall
