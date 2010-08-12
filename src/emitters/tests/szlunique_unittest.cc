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

// We need PRId32 and PRId64, which are only defined if we explicitly ask for it
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <stdio.h>
#include <math.h>                   // For sqrt.

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/strutils.h"
#include "public/szldecoder.h"
#include "public/szlencoder.h"
#include "public/szltabentry.h"


namespace sawzall {


class SzlUniqueTest  {
 public:
  void SetUp() {
    // Make testing type: unique(10) of string.
    uwr_type_ = new SzlType(SzlType::TABLE);
    uwr_type_->set_table("unique");
    SzlField telem("", SzlType::kString);
    uwr_type_->set_element(&telem);
    uwr_type_->set_param(10);
    string error;
    CHECK(uwr_type_->Valid(&error)) << ": " << error;

    uwr_ = SzlTabWriter::CreateSzlTabWriter(*uwr_type_, &error);
    CHECK(uwr_ != NULL) << ": " << error;
  }

  void RunTest(void (SzlUniqueTest::*pmf)()) {
    SetUp();
    (this->*pmf)();
    TearDown();
  }

  void TearDown() {
    delete uwr_;
    delete uwr_type_;
  }

  // Tests
  void UniqueRedundant();
  void TestMerge();
  void EstimateAccuracy();
  void TupleCountTest();

 private:
  // Exract the estimate from EncodedDispValue.
  static int64 Estimate(const SzlType& type, SzlTabEntry* u) {
    vector<string> encoded;
    u->FlushForDisplay(&encoded);
    CHECK_EQ(encoded.size(), 1);
    int64 es = u->TotElems();
    if (es == 0) {
      CHECK(encoded[0].empty());
      return 0;
    }
    SzlDecoder dec;
    dec.Init(encoded[0].data(), encoded[0].size());
    int64 result;
    CHECK(dec.GetInt(&result));
    CHECK(dec.done());
    return result;
  }


  // Verify that u1 and u2 are the same.
  static void Same(const SzlType& type,
                  SzlTabEntry* u1, SzlTabEntry* u2, const char* name) {
    CHECK(u1->TotElems() == u2->TotElems()) << ": " << name << " failed";
    CHECK(Estimate(type, u1) == Estimate(type, u2))
        << ": " << name << " failed";
  }


  bool IsAccurateEnough(int elems, int64 est, int64 actual) {
    int64 delta = est - actual;
    if (delta < 0)
      delta = -delta;
    double err = 100. * delta / actual;
    double allowed = 100. * 2. / sqrt(elems);
    printf("Unique elements: actual=%"PRId64" est=%"PRId64
           " delta=%"PRId64" err=%.2f%% allowed=%.2f%%\n",
           actual, est, delta, err, allowed);
    return (err < allowed);
  }


  void TestEstimate(int elems, int actual) {
    // Make testing type: unique(elems) of string.
    SzlType t(SzlType::TABLE);
    t.set_table("unique");
    SzlField telem("", SzlType::kString);
    t.set_element(&telem);
    t.set_param(elems);
    string error;
    CHECK(t.Valid(&error)) << ": " << error;

    // Using local variable table writer.
    SzlTabWriter* uwr = SzlTabWriter::CreateSzlTabWriter(t, &error);
    CHECK(uwr != NULL)<< ": " << error;

    SzlTabEntry* u = uwr->CreateEntry("");
    CHECK(u != NULL);

    for (int i = 0; i < actual; ++i) {
      u->AddElem(StringPrintf("est-%d", i));
      CHECK(u->TotElems() == i + 1);
      if ((i % kDispInterval) == 0) {
        vector<string> dummy;
        u->FlushForDisplay(&dummy);
      }
    }

    CHECK(u->TotElems() == actual);

    int64 est = Estimate(t, u);

    if (elems > actual) {
      CHECK(est == actual);
    } else {
      CHECK(IsAccurateEnough(elems, est, actual));
    }
    delete u;
    delete uwr;
  }

  static const int kDispInterval = 7;  // Interval between FlushForDisplay.
  SzlTabWriter* uwr_;
  SzlType* uwr_type_;
};


// Test that each element only counts once towards unique values.
// This knows that a heap is used, makes sure to test both when
// the redundant element is and is not the head.
void SzlUniqueTest::UniqueRedundant() {
  SzlTabEntry*  uref = uwr_->CreateEntry("");
  CHECK(uref != NULL);
  SzlTabEntry* u = uwr_->CreateEntry("");
  CHECK(u != NULL);

  static const int kHashes = 4;

  // Add a set of elements to two tables.
  for (int i = 0; i < kHashes; ++i) {
    string s = StringPrintf("%d", i);
    u->AddElem(s);
    uref->AddElem(s);
    CHECK(u->TotElems() == i + 1);
    CHECK(uref->TotElems() == i + 1);
  }

  // Add them again to one of the tables.
  for (int i = 0; i < kHashes; ++i) {
    string s = StringPrintf("%d", i);
    u->AddElem(s);
    CHECK(u->TotElems() == kHashes + i + 1);
  }

  // They should be the same, modulo number of emits.
  CHECK_EQ(Estimate(*uwr_type_, uref),
            Estimate(*uwr_type_, u))
                << ": Redundant merge failed";
  CHECK(uref->TotElems() < u->TotElems())
      << ": Redundant merge failed";

  // Add a single element to the other table until the emit count matches.
  for (int i = 0; i < kHashes; ++i) {
    string s = StringPrintf("%d", 0);
    uref->AddElem(s);
    CHECK(uref->TotElems() == kHashes + i + 1);
  }

  // They should have identical intermediate states.
  string s, sref;
  u->Flush(&s);
  uref->Flush(&sref);
  CHECK(s == sref) << ": Redundant failed";
  delete uref;
  delete u;
}


void SzlUniqueTest::TestMerge() {
  SzlTabEntry* u1 = uwr_->CreateEntry("");
  CHECK(u1 != NULL);
  SzlTabEntry* u2 = uwr_->CreateEntry("");
  CHECK(u2 != NULL);
  SzlTabEntry* u12 = uwr_->CreateEntry("");
  CHECK(u12 != NULL);
  SzlTabEntry* uboth = uwr_->CreateEntry("");
  CHECK(uboth != NULL);

  // First, test an empty merge.
  string s1;
  u1->Flush(&s1);
  CHECK_EQ(u1->Merge(s1), SzlTabEntry::MergeOk);
  CHECK_EQ(u1->TotElems(), 0);
  CHECK_EQ(Estimate(*uwr_type_, u1), 0);

  // Make tables u1 and u2, which has their own elements
  // and shares "dup-" elements.
  // "uboth" table has all of those.
  static const int kAdded = 256;
  for (int i = 0; i < kAdded; ++i) {
    string s = StringPrintf("%d", i);
    u1->AddElem(s);
    uboth->AddElem(s);
    CHECK(u1->TotElems() == i + 1);
  }

  for (int i = 0; i < kAdded; ++i) {
    string s = StringPrintf("another-%d", i + 512);
    u2->AddElem(s);
    uboth->AddElem(s);
    CHECK(u2->TotElems() == i + 1);
  }

  for (int i = 0; i < kAdded; ++i) {
    string s = StringPrintf("dup-%d", i);
    u1->AddElem(s);
    u2->AddElem(s);
    uboth->AddElem(s);
    uboth->AddElem(s);
    CHECK(u1->TotElems() == kAdded + i + 1);
  }
  string s2;
  u1->Flush(&s1);
  u2->Flush(&s2);
  CHECK(u12->Merge(s1) == SzlTabEntry::MergeOk);
  CHECK(u12->Merge(s2) == SzlTabEntry::MergeOk);

  Same(*uwr_type_, u12, uboth, "Merge");

  delete u1;
  delete u2;
  delete u12;
  delete uboth;
}


// A crude test that estimate works reasonably well.
// This does not test skew.
void SzlUniqueTest::EstimateAccuracy() {
  TestEstimate(100, 100 * 100);
  TestEstimate(1000, 1000 * 100);
  TestEstimate(2048, 2048 * 100);
  TestEstimate(10000, 9500);
  TestEstimate(10, 9500);
  TestEstimate(20, 9500);
  TestEstimate(30, 9500);
  TestEstimate(50, 9500);
  TestEstimate(100, 9500);
}


void SzlUniqueTest::TupleCountTest() {
  SzlTabEntry* u = uwr_->CreateEntry("");
  string error;

  for (int i = 0; i < 100; ++i) {
    u->AddElem(StringPrintf("est-%d", i));
    CHECK_EQ(u->TupleCount(), 1);
  }
  delete u;
}


}  // namespace sawzall


int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  typedef sawzall::SzlUniqueTest Test;
  Test test;
  test.RunTest(&Test::UniqueRedundant);
  test.RunTest(&Test::TestMerge);
  test.RunTest(&Test::EstimateAccuracy);
  test.RunTest(&Test::TupleCountTest);

  puts("PASS");
  return 0;
}
