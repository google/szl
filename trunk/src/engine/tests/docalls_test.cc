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

// Unit tests for the Process::DoCall() functionality.

#include <stdio.h>
#include <string>

#include "public/porting.h"
#include "public/logging.h"

#include "public/sawzall.h"
#include "public/value.h"


DECLARE_string(szl_includepath);
DECLARE_string(test_srcdir);

// Source code embedded in the test, at the end to make code more readable.
extern const char* test_funs_szl;
extern const char* test_table_error_szl;


namespace sawzall {

// A singleton helper class shared by all the tests in this file.
class TestProcess {
 public:
  // Sets up for running tests.
  static void Init() {
    executable_ = new Executable("<test_funs.szl>", test_funs_szl, kDoCalls);
    CHECK(executable_->is_executable());

    process_ = new Process(executable_, NULL);

    CHECK(process_->InitializeDoCalls());
  }

  static void Destroy() {
    delete process_;
    delete executable_;
  }

  static Process* process() { return process_; }

 private:
  static Executable* executable_;
  static Process* process_;
};

Executable* TestProcess::executable_;
Process* TestProcess::process_;


class DoCallTest {
 public:
  DoCallTest() {
    process_ = TestProcess::process();
    CHECK(process_);
  }

  void SetUp() {
    context_ = process_->SetupCall();
    CHECK_NE(reinterpret_cast<CallContext*>(NULL), context_);
    CHECK_EQ(NULL, process_->error_msg());
  }

  void TearDown() {
    process_->FinishCall(context_);
  }

  void RunTest(void (DoCallTest::*pmf)()) {
    SetUp();
    (this->*pmf)();
    TearDown();
  }

  // Tests
  void TestCallUndeclared();
  void TestCallNonVar();
  void TestCallNonFun();
  void TestNoCall();
  void TestCallWrongArgCount();
  void TestBoundedCallWrongArgCount();
  void TestNoOpCall();
  void TestNoOpBoundedCall();
  void TestCallWithResult();
  void TestBoundedCallWithResult();
  void TestCallWithArg();
  void TestBoundedCallWithArg();
  void TestCallWithArgAndResult();
  void TestBoundedCallWithArgAndResult();
  void TestCallWithArgAndResult2();
  void TestBoundedCallWithArgAndResult2();
  void TestCallWithMultipleArgs();
  void TestBoundedCallWithMultipleArgs();
  void TestCallWithBasicTypeArgsAndBoolResult();
  void TestCallWithBasicTypeArgsAndIntResult();
  void TestCallWithBasicTypeArgsAndFloatResult();
  void TestCallWithBasicTypeArgsAndUIntResult();
  void TestCallWithBasicTypeArgsAndTimeResult();
  void TestCallWithBasicTypeArgsAndFingerprintResult();
  void TestCallWithBasicTypeArgsAndStringResult();
  void TestCallWithBasicTypeArgsAndBytesResult();
  void TestBoundedCallWithBasicTypeArgsAndBoolResult();
  void TestBoundedCallWithBasicTypeArgsAndIntResult();
  void TestBoundedCallWithBasicTypeArgsAndFloatResult();
  void TestBoundedCallWithBasicTypeArgsAndUIntResult();
  void TestBoundedCallWithBasicTypeArgsAndTimeResult();
  void TestBoundedCallWithBasicTypeArgsAndFingerprintResult();
  void TestBoundedCallWithBasicTypeArgsAndStringResult();
  void TestBoundedCallWithBasicTypeArgsAndBytesResult();
  void TestNestedCalls();
  void TestBoundedNestedCalls();
  void TestGetGlobal();
  void TestBoundedGetGlobal();
  void TestSetGlobal();
  void TestBoundedSetGlobal();
  void TestTrappingCall();
  void TestBoundedTrappingCall();
  void TestUndefGlobalTrappingCall();
  void TestBoundedUndefGlobalTrappingCall();
  void TestUndefLocalTrappingCall();
  void TestBoundedUndefLocalTrappingCall();
  void TestAssertTrappingCall();
  void TestBoundedAssertTrappingCall();
  void TestAnotherTrappingCall();
  void TestAnotherBoundedTrappingCall();
  void TestCallStatic();
  void TestBoundedCallStatic();
  void TestGetStatic();
  void TestBoundedGetStatic();
  void TestGetStaticWrapper();
  void TestBoundedGetStaticWrapper();
  void TestMultipleBoundedCalls();
  void TestEmitError();
  void TestTopLevelEmitError();

 private:
  const FunctionDecl* TestLookupFunction(const char* function_name,
                                         const char* expected_error_msg) {
    const FunctionDecl* function_decl = process_->LookupFunction(function_name);
    const char* error_msg = process_->error_msg();
    if (expected_error_msg == NULL) {
      // Not expecting an error.
      CHECK_NE(reinterpret_cast<const FunctionDecl*>(NULL), function_decl);
      CHECK_EQ(NULL, error_msg)
          << "didn't expect non-null function lookup error: " << error_msg;
    } else {
      // Expecting an error.
      CHECK_EQ(NULL, function_decl);
      CHECK(strcmp(error_msg, expected_error_msg) == 0);
    }
    return function_decl;
  }

  void TestCall(const char* function_name,
                const Value* const* args,
                int sizeof_args,
                const Value* expected_result,
                const char* expected_error_msg) {
    int num_args = sizeof_args / sizeof(args[0]);

    const FunctionDecl* function_decl =
        TestLookupFunction(function_name,
                           NULL /* expected_error_msg */);

    const Value* result =
        process_->DoCall(context_, function_decl, args, num_args);
    const char* error_msg = process_->error_msg();

    if (expected_error_msg == NULL) {
      // Not expecting an error.
      CHECK_EQ(NULL, error_msg)
          << "didn't expect non-null call error: " << error_msg;
    } else {
      // Expecting an error.
      CHECK_EQ(NULL, result);
      CHECK(strcmp(error_msg, expected_error_msg)  == 0);
    }
    if (expected_result == NULL) {
      // No result expected.  (Error or void.)
      CHECK_EQ(NULL, result);
    } else {
      // Result expected.
      CHECK_EQ(NULL, error_msg)
          << "didn't expect non-null call error: " << error_msg;
      CHECK(result->IsEqual(expected_result))
          << "unexpected result";
    }
  }

  void TestBoundedCall(const char* function_name,
                       const Value* const* args,
                       int sizeof_args,
                       const Value* expected_result,
                       const char* expected_error_msg,
                       const int max_steps,
                       const bool should_finish) {
    int num_args = sizeof_args / sizeof(args[0]);

    const FunctionDecl* function_decl =
        TestLookupFunction(function_name,
                           NULL /* expected_error_msg */);

    const Value* result = NULL;
    int num_steps = 0;
    process_->StartCall(context_, function_decl, args, num_args);
    bool finished = process_->ContinueCall(context_, max_steps, &num_steps,
                                           &result);
    const char* error_msg = process_->error_msg();

    CHECK_EQ(finished, should_finish);

    if (expected_error_msg == NULL) {
      // Not expecting an error.
      CHECK_EQ(NULL, error_msg)
          << "didn't expect non-null call error: " << error_msg;
    } else {
      // Expecting an error.
      CHECK_EQ(NULL, result);
      CHECK(strcmp(error_msg, expected_error_msg) == 0);
    }
    if (expected_result == NULL) {
      // No result expected.  (Error or void.)
      CHECK_EQ(NULL, result);
    } else {
      // Result expected.
      CHECK_EQ(NULL, error_msg)
          << "didn't expect non-null call error: " << error_msg;
      CHECK(result->IsEqual(expected_result))
          << "unexpected result";
    }
  }

  enum ResultType {
    BOOL, INT, FLOAT, UINT, TIME, FINGERPRINT, STRING, BYTES
  };
  void TestCallWithBasicTypeArgs(ResultType result_type);
  void TestBoundedCallWithBasicTypeArgs(ResultType result_type);

  Process* process_;
  CallContext* context_;
};

void DoCallTest::TestCallUndeclared() {
  const char* function_name = "UndeclaredFunction";
  const char* expected_error_msg = "\"UndeclaredFunction\" undeclared";

  TestLookupFunction(function_name, expected_error_msg);
}

void DoCallTest::TestCallNonVar() {
  const char* function_name = "TypeDecl";
  const char* expected_error_msg = "\"TypeDecl\" is not a variable";

  TestLookupFunction(function_name, expected_error_msg);
}

void DoCallTest::TestCallNonFun() {
  const char* function_name = "a_string";
  const char* expected_error_msg = "\"a_string\" is not a function";

  TestLookupFunction(function_name, expected_error_msg);
}

void DoCallTest::TestNoCall() {
  // This just does SetupCall() and FinishCall(), without any DoCall()
  // in the middle.
}

void DoCallTest::TestCallWrongArgCount() {
  const char* function_name = "NoOp";
  const Value* args[] = {
    IntValue::New(context_, 5),
    IntValue::New(context_, 3),
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg =
      "wrong number of arguments to NoOp: expected 0; passed 2";

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedCallWrongArgCount() {
  const char* function_name = "NoOp";
  const Value* args[] = {
    IntValue::New(context_, 5),
    IntValue::New(context_, 3),
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg =
      "wrong number of arguments to NoOp: expected 0; passed 2";

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestNoOpCall() {
  const char* function_name = "NoOp";
  const Value* args[] = {
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg = NULL;

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestNoOpBoundedCall() {
  const char* function_name = "NoOp";
  const Value* args[] = {
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg = NULL;

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestCallWithResult() {
  const char* function_name = "TheAnswer";
  const Value* args[] = {
  };
  const Value* expected_result = IntValue::New(context_, 42);
  const char* expected_error_msg = NULL;

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedCallWithResult() {
  const char* function_name = "TheAnswer";
  const Value* args[] = {
  };
  const Value* expected_result = IntValue::New(context_, 42);
  const char* expected_error_msg = NULL;

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestCallWithArg() {
  const char* function_name = "DevNull";
  const Value* args[] = {
    StringValue::New(context_, "howdy"),
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg = NULL;

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedCallWithArg() {
  const char* function_name = "DevNull";
  const Value* args[] = {
    StringValue::New(context_, "howdy"),
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg = NULL;

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestCallWithArgAndResult() {
  const char* function_name = "Strlen";
  const Value* args[] = {
    StringValue::New(context_, "joe billy bob"),
  };
  const Value* expected_result = IntValue::New(context_, 13);
  const char* expected_error_msg = NULL;

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedCallWithArgAndResult() {
  const char* function_name = "Strlen";
  const Value* args[] = {
    StringValue::New(context_, "joe billy bob"),
  };
  const Value* expected_result = IntValue::New(context_, 13);
  const char* expected_error_msg = NULL;

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestCallWithArgAndResult2() {
  const char* function_name = "Identity";
  const Value* args[] = {
    IntValue::New(context_, 17),
  };
  const Value* expected_result = IntValue::New(context_, 17);
  const char* expected_error_msg = NULL;

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedCallWithArgAndResult2() {
  const char* function_name = "Identity";
  const Value* args[] = {
    IntValue::New(context_, 17),
  };
  const Value* expected_result = IntValue::New(context_, 17);
  const char* expected_error_msg = NULL;

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestCallWithMultipleArgs() {
  const char* function_name = "Subtract";
  const Value* args[] = {
    IntValue::New(context_, 22),
    IntValue::New(context_, 5),
  };
  const Value* expected_result = IntValue::New(context_, 17);
  const char* expected_error_msg = NULL;

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedCallWithMultipleArgs() {
  const char* function_name = "Subtract";
  const Value* args[] = {
    IntValue::New(context_, 22),
    IntValue::New(context_, 5),
  };
  const Value* expected_result = IntValue::New(context_, 17);
  const char* expected_error_msg = NULL;

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestCallWithBasicTypeArgs(ResultType result_type) {
  const Value* bool_val = BoolValue::New(context_, true);
  const Value* int_val = IntValue::New(context_, 99);
  const Value* float_val = FloatValue::New(context_, 3.14159);
  const Value* uint_val = UIntValue::New(context_, 99999999LL);
  const Value* time_val = TimeValue::New(context_, 12345678912345LL);
  const Value* fingerprint_val =
      FingerprintValue::New(context_, 0xdeadbeefL);
  const Value* string_val = StringValue::New(context_, "howdy");
  char bytes[] = "some \0encoded\0 bytes";
  const Value* bytes_val = BytesValue::New(context_, sizeof(bytes) - 1, bytes);

  const Value* args[] = {
    bool_val,
    int_val,
    float_val,
    uint_val,
    time_val,
    fingerprint_val,
    string_val,
    bytes_val,
  };

  const char* function_name;
  const Value* expected_result;
  switch (result_type) {
    case BOOL:
      function_name = "SelectBool";
      expected_result = bool_val;
      break;
    case INT:
      function_name = "SelectInt";
      expected_result = int_val;
      break;
    case FLOAT:
      function_name = "SelectFloat";
      expected_result = float_val;
      break;
    case UINT:
      function_name = "SelectUint";
      expected_result = uint_val;
      break;
    case TIME:
      function_name = "SelectTime";
      expected_result = time_val;
      break;
    case FINGERPRINT:
      function_name = "SelectFingerprint";
      expected_result = fingerprint_val;
      break;
    case STRING:
      function_name = "SelectString";
      expected_result = string_val;
      break;
    case BYTES:
      function_name = "SelectBytes";
      expected_result = bytes_val;
      break;
    default:
      LOG(FATAL) << "unexpected result_type";
  }

  const char* expected_error_msg = NULL;

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestCallWithBasicTypeArgsAndBoolResult() {
  TestCallWithBasicTypeArgs(BOOL);
}

void DoCallTest::TestCallWithBasicTypeArgsAndIntResult() {
  TestCallWithBasicTypeArgs(INT);
}

void DoCallTest::TestCallWithBasicTypeArgsAndFloatResult() {
  TestCallWithBasicTypeArgs(FLOAT);
}

void DoCallTest::TestCallWithBasicTypeArgsAndUIntResult() {
  TestCallWithBasicTypeArgs(UINT);
}

void DoCallTest::TestCallWithBasicTypeArgsAndTimeResult() {
  TestCallWithBasicTypeArgs(TIME);
}

void DoCallTest::TestCallWithBasicTypeArgsAndFingerprintResult() {
  TestCallWithBasicTypeArgs(FINGERPRINT);
}

void DoCallTest::TestCallWithBasicTypeArgsAndStringResult() {
  TestCallWithBasicTypeArgs(STRING);
}

void DoCallTest::TestCallWithBasicTypeArgsAndBytesResult() {
  TestCallWithBasicTypeArgs(BYTES);
}

void DoCallTest::TestBoundedCallWithBasicTypeArgs(ResultType result_type) {
  const Value* bool_val = BoolValue::New(context_, true);
  const Value* int_val = IntValue::New(context_, 99);
  const Value* float_val = FloatValue::New(context_, 3.14159);
  const Value* uint_val = UIntValue::New(context_, 99999999LL);
  const Value* time_val = TimeValue::New(context_, 12345678912345LL);
  const Value* fingerprint_val =
      FingerprintValue::New(context_, 0xdeadbeefL);
  const Value* string_val = StringValue::New(context_, "howdy");
  char bytes[] = "some \0encoded\0 bytes";
  const Value* bytes_val = BytesValue::New(context_, sizeof(bytes) - 1, bytes);

  const Value* args[] = {
    bool_val,
    int_val,
    float_val,
    uint_val,
    time_val,
    fingerprint_val,
    string_val,
    bytes_val,
  };

  const char* function_name;
  const Value* expected_result;
  switch (result_type) {
    case BOOL:
      function_name = "SelectBool";
      expected_result = bool_val;
      break;
    case INT:
      function_name = "SelectInt";
      expected_result = int_val;
      break;
    case FLOAT:
      function_name = "SelectFloat";
      expected_result = float_val;
      break;
    case UINT:
      function_name = "SelectUint";
      expected_result = uint_val;
      break;
    case TIME:
      function_name = "SelectTime";
      expected_result = time_val;
      break;
    case FINGERPRINT:
      function_name = "SelectFingerprint";
      expected_result = fingerprint_val;
      break;
    case STRING:
      function_name = "SelectString";
      expected_result = string_val;
      break;
    case BYTES:
      function_name = "SelectBytes";
      expected_result = bytes_val;
      break;
    default:
      LOG(FATAL) << "unexpected result_type";
  }

  const char* expected_error_msg = NULL;

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestBoundedCallWithBasicTypeArgsAndBoolResult() {
  TestBoundedCallWithBasicTypeArgs(BOOL);
}

void DoCallTest::TestBoundedCallWithBasicTypeArgsAndIntResult() {
  TestBoundedCallWithBasicTypeArgs(INT);
}

void DoCallTest::TestBoundedCallWithBasicTypeArgsAndFloatResult() {
  TestBoundedCallWithBasicTypeArgs(FLOAT);
}

void DoCallTest::TestBoundedCallWithBasicTypeArgsAndUIntResult() {
  TestBoundedCallWithBasicTypeArgs(UINT);
}

void DoCallTest::TestBoundedCallWithBasicTypeArgsAndTimeResult() {
  TestBoundedCallWithBasicTypeArgs(TIME);
}

void DoCallTest::TestBoundedCallWithBasicTypeArgsAndFingerprintResult() {
  TestBoundedCallWithBasicTypeArgs(FINGERPRINT);
}

void DoCallTest::TestBoundedCallWithBasicTypeArgsAndStringResult() {
  TestBoundedCallWithBasicTypeArgs(STRING);
}

void DoCallTest::TestBoundedCallWithBasicTypeArgsAndBytesResult() {
  TestBoundedCallWithBasicTypeArgs(BYTES);
}

void DoCallTest::TestNestedCalls() {
  const char* function_name = "Factorial";
  const Value* args[] = {
    IntValue::New(context_, 15),
  };
  const Value* expected_result = IntValue::New(context_, 1307674368000LL);
  const char* expected_error_msg = NULL;

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedNestedCalls() {
  const char* function_name = "Factorial";
  const Value* args[] = {
    IntValue::New(context_, 15),
  };
  const Value* expected_result = IntValue::New(context_, 1307674368000LL);
  const char* expected_error_msg = NULL;

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestGetGlobal() {
  const char* function_name = "GetGlobal";
  const Value* args[] = {
  };
  const Value* expected_result = IntValue::New(context_, 5040);
  const char* expected_error_msg = NULL;

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedGetGlobal() {
  const char* function_name = "GetGlobal";
  const Value* args[] = {
  };
  const Value* expected_result = IntValue::New(context_, 5040);
  const char* expected_error_msg = NULL;

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestSetGlobal() {
  {
    // SetupCall() was already invoked by the test fixture.

    const char* function_name = "SetGlobal";
    const Value* args[] = {
      IntValue::New(context_, 33),
    };
    const Value* expected_result = NULL;
    const char* expected_error_msg = NULL;

    TestCall(function_name, args, sizeof(args),
             expected_result, expected_error_msg);

    process_->FinishCall(context_);
  }

  {
    context_ = process_->SetupCall();

    const char* function_name = "GetGlobal";
    const Value* args[] = {
    };
    const Value* expected_result = IntValue::New(context_, 33);
    const char* expected_error_msg = NULL;

    TestCall(function_name, args, sizeof(args),
             expected_result, expected_error_msg);

    // FinishCall() will be invoked by the test fixture.
  }
}

void DoCallTest::TestBoundedSetGlobal() {
  {
    // SetupCall() was already invoked by the test fixture.

    const char* function_name = "SetGlobal";
    const Value* args[] = {
      IntValue::New(context_, 33),
    };
    const Value* expected_result = NULL;
    const char* expected_error_msg = NULL;

    TestCall(function_name, args, sizeof(args),
             expected_result, expected_error_msg);

    process_->FinishCall(context_);
  }

  {
    context_ = process_->SetupCall();

    const char* function_name = "GetGlobal";
    const Value* args[] = {
    };
    const Value* expected_result = IntValue::New(context_, 33);
    const char* expected_error_msg = NULL;

    TestBoundedCall(function_name, args, sizeof(args),
                    expected_result, expected_error_msg,
                    kint32max, true);

    // FinishCall() will be invoked by the test fixture.
  }
}

void DoCallTest::TestTrappingCall() {
  const char* function_name = "DivideByZero";
  const Value* args[] = {
    IntValue::New(context_, 34),
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg = "divide by zero error: 34 / 0";

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedTrappingCall() {
  const char* function_name = "DivideByZero";
  const Value* args[] = {
    IntValue::New(context_, 34),
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg = "divide by zero error: 34 / 0";

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestUndefGlobalTrappingCall() {
  const char* function_name = "ReturnGlobalUndef";
  const Value* args[] = {
    BoolValue::New(context_, true),
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg =
      "undefined value at <test_funs.szl>:130: a_string "
      "(probably because \"a_string\" had not been defined)";

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedUndefGlobalTrappingCall() {
  const char* function_name = "ReturnGlobalUndef";
  const Value* args[] = {
    BoolValue::New(context_, true),
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg =
      "undefined value at <test_funs.szl>:130: a_string "
      "(probably because \"a_string\" had not been defined)";

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestUndefLocalTrappingCall() {
  const char* function_name = "ReturnLocalUndef";
  const Value* args[] = {
    BoolValue::New(context_, true),
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg =
      "undefined value at <test_funs.szl>:140: f * f "
      "(probably because \"f\" had not been defined)";

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedUndefLocalTrappingCall() {
  const char* function_name = "ReturnLocalUndef";
  const Value* args[] = {
    BoolValue::New(context_, true),
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg =
      "undefined value at <test_funs.szl>:140: f * f "
      "(probably because \"f\" had not been defined)";

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestAssertTrappingCall() {
  const char* function_name = "DoAssert";
  const Value* args[] = {
    BoolValue::New(context_, true),
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg =
      "assertion failed at <test_funs.szl>:145: assert(!b)";

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedAssertTrappingCall() {
  const char* function_name = "DoAssert";
  const Value* args[] = {
    BoolValue::New(context_, true),
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg =
      "assertion failed at <test_funs.szl>:145: assert(!b)";

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestAnotherTrappingCall() {
  const char* function_name = "Die";
  const Value* args[] = {
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg =
      "undefined value at <test_funs.szl>:151: 1 / 0 "
      "(divide by zero error: 1 / 0)";

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestAnotherBoundedTrappingCall() {
  const char* function_name = "Die";
  const Value* args[] = {
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg =
      "undefined value at <test_funs.szl>:151: 1 / 0 "
      "(divide by zero error: 1 / 0)";

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestCallStatic() {
  const char* function_name = "Fib";
  const Value* args[] = {
    IntValue::New(context_, 7),
  };
  const Value* expected_result = IntValue::New(context_, 13);
  const char* expected_error_msg = NULL;

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedCallStatic() {
  const char* function_name = "Fib";
  const Value* args[] = {
    IntValue::New(context_, 7),
  };
  const Value* expected_result = IntValue::New(context_, 13);
  const char* expected_error_msg = NULL;

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestGetStatic() {
  const char* function_name = "GetStatic";
  const Value* args[] = {
  };
  const Value* expected_result = IntValue::New(context_, 21);
  const char* expected_error_msg = NULL;

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedGetStatic() {
  const char* function_name = "GetStatic";
  const Value* args[] = {
  };
  const Value* expected_result = IntValue::New(context_, 21);
  const char* expected_error_msg = NULL;

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestGetStaticWrapper() {
  const char* function_name = "GetStaticWrapper";
  const Value* args[] = {
  };
  const Value* expected_result = IntValue::New(context_, 21);
  const char* expected_error_msg = NULL;

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}

void DoCallTest::TestBoundedGetStaticWrapper() {
  const char* function_name = "GetStaticWrapper";
  const Value* args[] = {
  };
  const Value* expected_result = IntValue::New(context_, 21);
  const char* expected_error_msg = NULL;

  TestBoundedCall(function_name, args, sizeof(args),
                  expected_result, expected_error_msg,
                  kint32max, true);
}

void DoCallTest::TestMultipleBoundedCalls() {
  const FunctionDecl* factorial = TestLookupFunction("Factorial", NULL);
  const Value* args[] = {
    IntValue::New(context_, 15),
  };
  const Value* expected_result = IntValue::New(context_, 1307674368000LL);

  process_->StartCall(context_, factorial, args, sizeof(args)/sizeof(args[0]));
  const Value* result = NULL;
  int num_steps = 0;
  bool finished = process_->ContinueCall(context_, 10, &num_steps, &result);

  CHECK_EQ(false, finished);
  CHECK_LE(10, num_steps);
  CHECK_EQ(NULL, process_->error_msg());
  CHECK_EQ(NULL, result);

  finished = process_->ContinueCall(context_, 10, &num_steps, &result);

  CHECK_EQ(false, finished);
  CHECK_LE(10, num_steps);
  CHECK_EQ(NULL, process_->error_msg());
  CHECK_EQ(NULL, result);

  finished = process_->ContinueCall(context_, kint32max, &num_steps, &result);

  CHECK_EQ(true, finished);
  CHECK_EQ(NULL, process_->error_msg());
  CHECK_EQ(expected_result->as_int()->value(), result->as_int()->value());
}

void DoCallTest::TestEmitError() {
  const char* function_name = "Emit";
  const Value* args[] = {
    StringValue::New(context_, "hi"),
  };
  const Value* expected_result = NULL;
  const char* expected_error_msg =
      "no emitter installed for table t; cannot emit";

  TestCall(function_name, args, sizeof(args),
           expected_result, expected_error_msg);
}


void DoCallTest::TestTopLevelEmitError() {
  Executable executable("<test_table_error.szl>",
                        test_table_error_szl, kDoCalls);
  CHECK(executable.is_executable());

  Process process(&executable, NULL);

  CHECK(!process.InitializeDoCalls());
  const char* error_msg = process.error_msg();
  const char* expected_error_msg =
      "no emitter installed for table t; cannot emit";
  CHECK(strcmp(error_msg, expected_error_msg) == 0);
}

}  // namespace sawzall

int main(int argc, char* argv[]) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();
  sawzall::TestProcess::Init();

  typedef sawzall::DoCallTest Test;
  Test test;
  test.RunTest(&Test::TestCallUndeclared);
  test.RunTest(&Test::TestCallNonVar);
  test.RunTest(&Test::TestCallNonFun);
  test.RunTest(&Test::TestNoCall);
  test.RunTest(&Test::TestCallWrongArgCount);
  test.RunTest(&Test::TestBoundedCallWrongArgCount);
  test.RunTest(&Test::TestNoOpCall);
  test.RunTest(&Test::TestNoOpBoundedCall);
  test.RunTest(&Test::TestCallWithResult);
  test.RunTest(&Test::TestBoundedCallWithResult);
  test.RunTest(&Test::TestCallWithArg);
  test.RunTest(&Test::TestBoundedCallWithArg);
  test.RunTest(&Test::TestCallWithArgAndResult);
  test.RunTest(&Test::TestBoundedCallWithArgAndResult);
  test.RunTest(&Test::TestCallWithArgAndResult2);
  test.RunTest(&Test::TestBoundedCallWithArgAndResult2);
  test.RunTest(&Test::TestCallWithMultipleArgs);
  test.RunTest(&Test::TestBoundedCallWithMultipleArgs);
  test.RunTest(&Test::TestCallWithBasicTypeArgsAndBoolResult);
  test.RunTest(&Test::TestCallWithBasicTypeArgsAndIntResult);
  test.RunTest(&Test::TestCallWithBasicTypeArgsAndFloatResult);
  test.RunTest(&Test::TestCallWithBasicTypeArgsAndUIntResult);
  test.RunTest(&Test::TestCallWithBasicTypeArgsAndTimeResult);
  test.RunTest(&Test::TestCallWithBasicTypeArgsAndFingerprintResult);
  test.RunTest(&Test::TestCallWithBasicTypeArgsAndStringResult);
  test.RunTest(&Test::TestCallWithBasicTypeArgsAndBytesResult);
  test.RunTest(&Test::TestBoundedCallWithBasicTypeArgsAndBoolResult);
  test.RunTest(&Test::TestBoundedCallWithBasicTypeArgsAndIntResult);
  test.RunTest(&Test::TestBoundedCallWithBasicTypeArgsAndFloatResult);
  test.RunTest(&Test::TestBoundedCallWithBasicTypeArgsAndUIntResult);
  test.RunTest(&Test::TestBoundedCallWithBasicTypeArgsAndTimeResult);
  test.RunTest(&Test::TestBoundedCallWithBasicTypeArgsAndFingerprintResult);
  test.RunTest(&Test::TestBoundedCallWithBasicTypeArgsAndStringResult);
  test.RunTest(&Test::TestBoundedCallWithBasicTypeArgsAndBytesResult);
  test.RunTest(&Test::TestNestedCalls);
  test.RunTest(&Test::TestBoundedNestedCalls);
  test.RunTest(&Test::TestGetGlobal);
  test.RunTest(&Test::TestBoundedGetGlobal);
  test.RunTest(&Test::TestSetGlobal);
  test.RunTest(&Test::TestBoundedSetGlobal);
  test.RunTest(&Test::TestTrappingCall);
  test.RunTest(&Test::TestBoundedTrappingCall);
  test.RunTest(&Test::TestUndefGlobalTrappingCall);
  test.RunTest(&Test::TestBoundedUndefGlobalTrappingCall);
  test.RunTest(&Test::TestUndefLocalTrappingCall);
  test.RunTest(&Test::TestBoundedUndefLocalTrappingCall);
  test.RunTest(&Test::TestAssertTrappingCall);
  test.RunTest(&Test::TestBoundedAssertTrappingCall);
  test.RunTest(&Test::TestAnotherTrappingCall);
  test.RunTest(&Test::TestAnotherBoundedTrappingCall);
  test.RunTest(&Test::TestCallStatic);
  test.RunTest(&Test::TestBoundedCallStatic);
  test.RunTest(&Test::TestGetStatic);
  test.RunTest(&Test::TestBoundedGetStatic);
  test.RunTest(&Test::TestGetStaticWrapper);
  test.RunTest(&Test::TestBoundedGetStaticWrapper);
  test.RunTest(&Test::TestMultipleBoundedCalls);
  test.RunTest(&Test::TestEmitError);
  test.RunTest(&Test::TestTopLevelEmitError);

  sawzall::TestProcess::Destroy();

  puts("PASS");
  return 0;
}


// Source code

// ============================================================================

const char* test_funs_szl =
  "# This is a file of Sawzall test functions.\n"
  "\n"
  "type TypeDecl = int;\n"
  "a_string: string;\n"
  "\n"
  "NoOp: function() {\n"
  "};\n"
  "\n"
  "TheAnswer: function(): int {\n"
  "  return 42;\n"
  "};\n"
  "\n"
  "DevNull: function(s: string) {\n"
  "};\n"
  "\n"
  "Strlen: function(s: string): int {\n"
  "  return len(s);\n"
  "};\n"
  "\n"
  "Identity: function(i: int): int {\n"
  "  return i;\n"
  "};\n"
  "\n"
  "Subtract: function(i1: int, i2: int): int {\n"
  "  return i1 - i2;\n"
  "};\n"
  "\n"
  "SelectBool: function(b: bool, i: int, f: float,\n"
  "                     ui: uint, t: time, fp: fingerprint,\n"
  "                     s: string, bs: bytes\n"
  "                    ): bool {\n"
  "  return b;\n"
  "};\n"
  "\n"
  "SelectInt: function(b: bool, i: int, f: float,\n"
  "                    ui: uint, t: time, fp: fingerprint,\n"
  "                    s: string, bs: bytes\n"
  "                   ): int {\n"
  "  return i;\n"
  "};\n"
  "\n"
  "SelectFloat: function(b: bool, i: int, f: float,\n"
  "                      ui: uint, t: time, fp: fingerprint,\n"
  "                      s: string, bs: bytes\n"
  "                     ): float {\n"
  "  return f;\n"
  "};\n"
  "\n"
  "SelectUint: function(b: bool, i: int, f: float,\n"
  "                     ui: uint, t: time, fp: fingerprint,\n"
  "                     s: string, bs: bytes\n"
  "                    ): uint {\n"
  "  return ui;\n"
  "};\n"
  "\n"
  "SelectTime: function(b: bool, i: int, f: float,\n"
  "                     ui: uint, t: time, fp: fingerprint,\n"
  "                     s: string, bs: bytes\n"
  "                    ): time {\n"
  "  return t;\n"
  "};\n"
  "\n"
  "SelectFingerprint: function(b: bool, i: int, f: float,\n"
  "                            ui: uint, t: time, fp: fingerprint,\n"
  "                            s: string, bs: bytes\n"
  "                           ): fingerprint {\n"
  "  return fp;\n"
  "};\n"
  "\n"
  "SelectString: function(b: bool, i: int, f: float,\n"
  "                       ui: uint, t: time, fp: fingerprint,\n"
  "                       s: string, bs: bytes\n"
  "                      ): string {\n"
  "  return s;\n"
  "};\n"
  "\n"
  "SelectBytes: function(b: bool, i: int, f: float,\n"
  "                      ui: uint, t: time, fp: fingerprint,\n"
  "                      s: string, bs: bytes\n"
  "                     ): bytes {\n"
  "  return bs;\n"
  "};\n"
  "\n"
  "Factorial: function(n: int): int {\n"
  "  if (n <= 1) {\n"
  "    return 1;\n"
  "  } else {\n"
  "    return n * Factorial(n - 1);\n"
  "  }\n"
  "};\n"
  "\n"
  "global: int = Factorial(7);\n"
  "\n"
  "GetGlobal: function(): int {\n"
  "  return global;\n"
  "};\n"
  "\n"
  "SetGlobal: function(i: int) {\n"
  "  global = i;\n"
  "};\n"
  "\n"
  "# Introduce extra helper function and local variables in order to\n"
  "# exercise error propagation and memory cleanup across multiple stack frames.\n"
  "# Also include computations that do memory allocation, i.e.,\n"
  "# conditional string concatenation, that are\n"
  "# unlikely to be elimintated by the compiler.\n"
  "DivideByZeroHelper: function(i: int, a: string): string {\n"
  "  b: string = a + \"test data\" + a;\n"
  "  j := i / 0;\n"
  "  return b + b;\n"
  "};\n"
  "\n"
  "DivideByZero: function(i: int): string {\n"
  "  s: string = \"test\";\n"
  "  if (i > 0) {\n"
  "    s = \"more \" + s;\n"
  "  } else {\n"
  "    s = \"still more \" + s;\n"
  "  }\n"
  "  t: string = DivideByZeroHelper(i, s);\n"
  "  return t + s;\n"
  "};\n"
  "\n"
  "ReturnGlobalUndef: function(b: bool): string {\n"
  "  # Include a conditional assignment to defeat the compiler's static\n"
  "  # undefined-variable checking.\n"
  "  if (!b) {\n"
  "    a_string = \"hi\";\n"
  "  }\n"
  "  return a_string;\n"
  "};\n"
  "\n"
  "ReturnLocalUndef: function(b: bool): float {\n"
  "  f: float;\n"
  "  # Include a conditional assignment to defeat the compiler's static\n"
  "  # undefined-variable checking.\n"
  "  if (!b) {\n"
  "    f = 3.14;\n"
  "  }\n"
  "  g: float = f * f;\n"
  "  return g;\n"
  "};\n"
  "\n"
  "DoAssert: function(b: bool) {\n"
  "  assert(!b);\n"
  "};\n"
  "\n"
  "Die: function() {\n"
  "  i := 0;\n"
  "  j := 0;\n"
  "  1 / 0;\n"
  "};\n"
  "\n"
  "static Fib: function(n: int): int {\n"
  "  if (n <= 1) {\n"
  "    return n;\n"
  "  } else {\n"
  "    return Fib(n - 1) + Fib(n - 2);\n"
  "  }\n"
  "};\n"
  "\n"
  "static kStaticGlobal: int = Fib(8);\n"
  "\n"
  "GetStatic: function(): int {\n"
  "  return kStaticGlobal;\n"
  "};\n"
  "\n"
  "# Test that a function can call another function which in turn\n"
  "# accesses a static variable, since that exercises subtleties of\n"
  "# setting up static and dynamic linkages properly.\n"
  "GetStaticWrapper: function(): int {\n"
  "  return GetStatic();\n"
  "};\n"
  "\n"
  "t: table collection of string;\n"
  "\n"
  "Emit: function(s: string) {\n"
  "  emit t <- s;\n"
  "};\n"
  "\n"
  "Weird: function(s: string, f: float): bool {\n"
  "  return convert(float, len(s)) > f;\n"
  "};\n"
;

// ============================================================================

const char* test_table_error_szl =
  "# This file attempts to output to a table, which is illegal when used\n"
  "# in DoCall() mode (and no emitters have been registered).\n"
  "\n"
  "t: table collection of string;\n"
  "\n"
  "emit t <- \"hi\";\n"
;

// ============================================================================
