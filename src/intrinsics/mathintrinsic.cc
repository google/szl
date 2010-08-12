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

#define _ISOC99_SOURCE 1  // for isnan, etc.
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <assert.h>

#include "engine/globals.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/frame.h"
#include "engine/proc.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/engine.h"

namespace sawzall {

static void call1(Proc* proc, Val**& sp, double (*fn)(double)) {
  szl_float x = Engine::pop_szl_float(sp);
  errno = 0;
  Engine::push_szl_float(sp, proc, fn(x));
}


static void call2(Proc* proc, Val**& sp, double (*fn)(double, double)) {
  szl_float x = Engine::pop_szl_float(sp);
  szl_float y = Engine::pop_szl_float(sp);
  errno = 0;
  Engine::push_szl_float(sp, proc, fn(x, y));
}


static const char szl_ln_doc[] =
  "The natural logarithm function.";

static void szl_ln(Proc* proc, Val**& sp) {
  call1(proc, sp, ::log);
}


static const char* szl_log10_doc =
  "The base 10 logarithm function";

static void szl_log10(Proc* proc, Val**& sp) {
  call1(proc, sp, ::log10);
}


static const char szl_exp_doc[] =
  "The base e exponential function.";

static void szl_exp(Proc* proc, Val**& sp) {
  call1(proc, sp, ::exp);
}


static const char szl_sqrt_doc[] =
  "The square root of function.";

static void szl_sqrt(Proc* proc, Val**& sp) {
  call1(proc, sp, ::sqrt);
}


static const char szl_pow_doc[] =
  "The exponential, base x, of y.";

static void szl_pow(Proc* proc, Val**& sp) {
  call2(proc, sp, ::pow);
}


static const char szl_sin_doc[] =
  "The sine function, argument in radians.";

static void szl_sin(Proc* proc, Val**& sp) {
  call1(proc, sp, ::sin);
}


static const char szl_cos_doc[] =
  "The cosine function, argument in radians.";

static void szl_cos(Proc* proc, Val**& sp) {
  call1(proc, sp, ::cos);
}


static const char szl_tan_doc[] =
  "The tangent function, argument in radians.";

static void szl_tan(Proc* proc, Val**& sp) {
  call1(proc, sp, ::tan);
}


static const char szl_asin_doc[] =
  "The arc sine function.";

static void szl_asin(Proc* proc, Val**& sp) {
  return call1(proc, sp, ::asin);
}


static const char szl_acos_doc[] =
  "The arc cosine function.";

static void szl_acos(Proc* proc, Val**& sp) {
  call1(proc, sp, ::acos);
}


static const char szl_atan_doc[] =
  "The arc tangent function.";

static void szl_atan(Proc* proc, Val**& sp) {
  call1(proc, sp, ::atan);
}


static const char* szl_atan2_doc =
  "The arc tangent of y/x.";

static void szl_atan2(Proc* proc, Val**& sp) {
  call2(proc, sp, ::atan2);
}


static const char szl_cosh_doc[] =
  "The hyperbolic cosine function.";

static void szl_cosh(Proc* proc, Val**& sp) {
  call1(proc, sp, ::cosh);
}


static const char szl_sinh_doc[] =
  "The hyperbolic sine function.";

static void szl_sinh(Proc* proc, Val**& sp) {
  call1(proc, sp, ::sinh);
}


static const char szl_tanh_doc[] =
  "The hyperbolic tangent function.";

static void szl_tanh(Proc* proc, Val**& sp) {
  call1(proc, sp, ::tanh);
}


static const char szl_acosh_doc[] =
  "The hyperbolic arc cosine function.";

static void szl_acosh(Proc* proc, Val**& sp) {
  call1(proc, sp, ::acosh);
}


static const char szl_asinh_doc[] =
  "The hyperbolic arc sine function.";

static void szl_asinh(Proc* proc, Val**& sp) {
  call1(proc, sp, ::asinh);
}


static const char szl_atanh_doc[] =
  "The hyperbolic arc tangent function.";

static void szl_atanh(Proc* proc, Val**& sp) {
  call1(proc, sp, ::atanh);
}


static const char szl_fabs_doc[] =
  "The absolute value function.";

static void szl_fabs(Proc* proc, Val**& sp) {
  call1(proc, sp, ::fabs);
}


static const char szl_ceil_doc[] =
  "Round up to the nearest integer.";

static void szl_ceil(Proc* proc, Val**& sp) {
  call1(proc, sp, ::ceil);
}


static const char szl_floor_doc[] =
  "Round down to the nearest integer.";

static void szl_floor(Proc* proc, Val**& sp) {
  call1(proc, sp, ::floor);
}


static const char szl_round_doc[] =
  "Round to the nearest integer, but round halfway cases away from zero.";

static void szl_round(Proc* proc, Val**& sp) {
  call1(proc, sp, ::round);
}


static const char szl_trunc_doc[] =
  "Round to the nearest integer not larger in absolute value.";

static void szl_trunc(Proc* proc, Val**& sp) {
  call1(proc, sp, ::trunc);
}


// IEEE special values

static const char szl_isnan_doc[] =
  "Tests if a float value is an IEEE NaN";

static void szl_isnan(Proc* proc, Val**& sp) {
  szl_float x = Engine::pop_szl_float(sp);
  Engine::push_szl_bool(sp, proc, isnan(x));
}


static const char szl_isinf_doc[] =
  "Tests if a float value is an IEEE Inf";

static void szl_isinf(Proc* proc, Val**& sp) {
  szl_float x = Engine::pop_szl_float(sp);
  Engine::push_szl_bool(sp, proc, isinf(x));
}


static const char szl_isfinite_doc[] =
  "Tests if a float value is not +-Inf or NaN";

static void szl_isfinite(Proc* proc, Val**& sp) {
  szl_float x = Engine::pop_szl_float(sp);
  Engine::push_szl_bool(sp, proc, isfinite(x));
}

static const char szl_isnormal_doc[] =
  "Tests if a float value is neither zero, subnormal, Inf, nor NaN";

static void szl_isnormal(Proc* proc, Val**& sp) {
  szl_float x = Engine::pop_szl_float(sp);
  Engine::push_szl_bool(sp, proc, isnormal(x));
}

static void Initialize() {
  // make sure the SymbolTable is initialized
  assert(SymbolTable::is_initialized());
  Proc* proc = Proc::initial_proc();

  // shortcuts for predefined types
  Type* bool_type = SymbolTable::bool_type();
  Type* float_type = SymbolTable::float_type();

  FunctionType* unary = FunctionType::New(proc)->
    par("x", float_type)->res(float_type);
  FunctionType* binary = FunctionType::New(proc)->
    par("x", float_type)->par("y", float_type)->res(float_type);
  FunctionType* predicate = FunctionType::New(proc)->
    par("x", float_type)->res(bool_type);

#define DEF(name, type, attribute) \
  SymbolTable::RegisterIntrinsic(#name, type, szl_##name, \
                                 szl_##name##_doc, attribute)

  DEF(ln, unary, Intrinsic::kCanFold);
  DEF(log10, unary, Intrinsic::kCanFold);
  DEF(exp, unary, Intrinsic::kCanFold);
  DEF(sqrt, unary, Intrinsic::kCanFold);
  DEF(pow, binary, Intrinsic::kCanFold);
  DEF(sin, unary, Intrinsic::kCanFold);
  DEF(cos, unary, Intrinsic::kCanFold);
  DEF(tan, unary, Intrinsic::kCanFold);
  DEF(asin, unary, Intrinsic::kCanFold);
  DEF(acos, unary, Intrinsic::kCanFold);
  DEF(atan, unary, Intrinsic::kCanFold);
  DEF(atan2, binary, Intrinsic::kCanFold);
  DEF(cosh, unary, Intrinsic::kCanFold);
  DEF(sinh, unary, Intrinsic::kCanFold);
  DEF(tanh, unary, Intrinsic::kCanFold);
  DEF(acosh, unary, Intrinsic::kCanFold);
  DEF(asinh, unary, Intrinsic::kCanFold);
  DEF(atanh, unary, Intrinsic::kCanFold);
  DEF(fabs, unary, Intrinsic::kCanFold);
  DEF(ceil, unary, Intrinsic::kCanFold);
  DEF(floor, unary, Intrinsic::kCanFold);
  DEF(round, unary, Intrinsic::kCanFold);
  DEF(trunc, unary, Intrinsic::kCanFold);

  DEF(isnan, predicate, Intrinsic::kCanFold);
  DEF(isinf, predicate, Intrinsic::kCanFold);
  DEF(isfinite, predicate, Intrinsic::kCanFold);
  DEF(isnormal, predicate, Intrinsic::kCanFold);
}

}  // namespace sawzall

// Note: This must happen outside the namespace
// to avoid linker/name mangling problems.
REGISTER_MODULE_INITIALIZER(
  MathIntrinsic, {
    REQUIRE_MODULE_INITIALIZED(Sawzall);
    sawzall::Initialize();
  }
);
