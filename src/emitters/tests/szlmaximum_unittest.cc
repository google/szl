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

#include <stdio.h>

#include <string>
#include <vector>
#include <functional>
#include <algorithm>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/strutils.h"
#include "utilities/random_base.h"
#include "utilities/acmrandom.h"

#include "public/szldecoder.h"
#include "public/szlencoder.h"
#include "public/szlvalue.h"
#include "public/szltabentry.h"


namespace sawzall {

class SzlMaximumTest {
 public:
  SzlMaximumTest();
  void SetUp();

  void RunTest(void (SzlMaximumTest::*pmf)(int), int value) {
    SetUp();
    (this->*pmf)(value);
    TearDown();
  }

  void RunTest(void (SzlMaximumTest::*pmf)(const char*, int),
               const char* str, int value) {
    SetUp();
    (this->*pmf)(str, value);
    TearDown();
  }

  void SetUpParam(const char *kind, int nelem);
  void TestEmptyMerge(int nelem);
  void TestMaximum(const char* kind, int nelem);
  void TestTuple(const char* kind, int nelem);

  void TearDown() {
    delete tab1_;
    delete tab2_;
    delete mwr_;
  }

 private:
  SzlType type_;
  SzlField telem_;
  SzlField tweight_;
  SzlTabWriter* mwr_;
  SzlTabEntry* tab1_;
  SzlTabEntry* tab2_;
  static const int kMult = 5;     // Multiplier to overload tables.
  static const int kDispInterval = 7;  // Interval between FlushForDisplay.
};


SzlMaximumTest::SzlMaximumTest()
    : type_(SzlType::TABLE),
      telem_("", SzlType::kString),
      tweight_("", SzlType::kInt) {
}


void SzlMaximumTest::SetUp() {
  type_.set_element(&telem_);
  type_.set_weight(&tweight_);
}



// SetUp objects with test dependency in their parameters.
void SzlMaximumTest::SetUpParam(const char *kind, int nelem) {
  type_.set_param(nelem);
  type_.set_table(kind);
  string error;
  CHECK(type_.Valid(&error)) << ": " << error;

  mwr_ = SzlTabWriter::CreateSzlTabWriter(type_, &error);
  CHECK(mwr_ != NULL) << ": " << error;
  tab1_ = mwr_->CreateEntry("");
  CHECK(tab1_ != NULL);
  tab2_ = mwr_->CreateEntry("");
  CHECK(tab2_ != NULL);
}


// Check initial conditions and empty merge.
void SzlMaximumTest::TestEmptyMerge(int nelem) {
  SetUpParam("maximum", nelem);
  CHECK_EQ(0, tab1_->TotElems());
  vector<string> value;
  tab1_->FlushForDisplay(&value);
  CHECK_EQ(1, value.size());
  CHECK_EQ(0, value[0].size());
  CHECK_EQ(0, tab2_->TotElems());
  tab2_->FlushForDisplay(&value);
  CHECK_EQ(1, value.size());
  CHECK_EQ(0, value[0].size());
  string s1;
  tab1_->Flush(&s1);
  string s2;
  tab2_->Flush(&s2);
  CHECK_EQ(0, s1.size());
  CHECK_EQ(0, s2.size());
  CHECK_EQ(s1, s2);
  CHECK_EQ(tab1_->Merge(s2), SzlTabEntry::MergeOk);
  string s3;
  tab1_->Flush(&s3);
  CHECK_EQ(s1, s3);
}


// Test a simple maximum table.
// Since we use SzlValues, we don't need to test all weight types.
void SzlMaximumTest::TestMaximum(const char* kind, int nelem) {
  // Make testing type: maximum or minimum(nelem) of string weight int
  SetUpParam(kind, nelem);

  // Add a bunch of random elements.
  SzlACMRandom random(SzlACMRandom::DeterministicSeed());
  vector<int> vals;
  for (int i = 0; i < kMult * nelem; i++) {
    int64 v = random.Next();
    SzlValue w(v);
    string elem = StringPrintf("xx-%lld", v);
    SzlEncoder enc;
    enc.PutString(elem.c_str());
    tab1_->AddWeightedElem(enc.data(), w);
    vals.push_back(v);

    // Do FlushForDisplay to verify no side-effect.
    if ((i % kDispInterval) == 0) {
      vector<string> dummy;
      tab1_->FlushForDisplay(&dummy);
    }
  }
  for (int i = 0; i < kMult * nelem; i++) {
    int64 v = random.Next();
    SzlValue w(v);
    string elem = StringPrintf("xx-%lld", v);
    SzlEncoder enc;
    enc.PutString(elem.c_str());
    tab2_->AddWeightedElem(enc.data(), w);
    vals.push_back(v);
  }

  // Test a merge.
  string state2;
  tab2_->Flush(&state2);
  CHECK_EQ(tab1_->Merge(state2), SzlTabEntry::MergeOk);

  // Prepare expected results.
  if (strcmp(kind, "maximum") == 0) {
    sort(vals.begin(), vals.end(), greater<int>());
  } else {
    sort(vals.begin(), vals.end());
  }

  // Verify we kept the best ones.
  vector<string> results;
  tab1_->FlushForDisplay(&results);
  CHECK_EQ(results.size(), nelem);
  for (int i = 0; i < nelem; ++i) {
    SzlDecoder dec(results[i].data(), results[i].size());
    string estr;
    int64 w;
    CHECK(dec.GetString(&estr));
    CHECK(dec.GetInt(&w));
    CHECK(dec.done());
    CHECK_EQ(vals[i], w);
    string elem = StringPrintf("xx-%d", vals[i]);
    CHECK_EQ(estr, elem);
  }
}


void SzlMaximumTest::TestTuple(const char* kind, int nelem) {
  // Make testing type: maximum or minimum(nelem) of string weight int
  SetUpParam(kind, nelem);
  CHECK_EQ(tab1_->TupleCount(), 0);

  // Add a bunch of random elements.
  SzlACMRandom random(SzlACMRandom::DeterministicSeed());
  vector<int> vals;
  vector<string> state2;
  for (int i = 0; i < kMult * nelem; i++) {
    int64 v = random.Next();
    SzlValue w(v);
    string elem = StringPrintf("xx-%lld", v);
    SzlEncoder enc;
    enc.PutString(elem.c_str());
    tab1_->AddWeightedElem(enc.data(), w);
    vals.push_back(v);
    int expectedTupleCnt = (i < nelem) ? i + 1 : nelem;
    CHECK_EQ(tab1_->TupleCount(), expectedTupleCnt);
  }
}


}  // namespace sawzall


int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  typedef sawzall::SzlMaximumTest Test;
  Test test;
  test.RunTest(&Test::TestEmptyMerge, 1);
  test.RunTest(&Test::TestEmptyMerge, 16);
  test.RunTest(&Test::TestEmptyMerge, 23);
  test.RunTest(&Test::TestMaximum, "maximum", 1);
  test.RunTest(&Test::TestMaximum, "minimum", 16);
  test.RunTest(&Test::TestMaximum, "maximum", 23);
  test.RunTest(&Test::TestTuple, "maximum", 100);
  test.RunTest(&Test::TestTuple, "minimum", 100);

  puts("PASS");
  return 0;
}
