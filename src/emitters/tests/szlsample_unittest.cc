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

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/strutils.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlvalue.h"
#include "public/szltabentry.h"


namespace sawzall {


class SzlSampleTest {
 public:
  SzlSampleTest();
  void SetUp();
  void SetUpParam(int nelem);

  void RunTest(void (SzlSampleTest::*pmf)(int), int value) {
    SetUp();
    (this->*pmf)(value);
    TearDown();
  }

  // Test functions.
  void TestSample(int nsamples);
  void TestEmptyMerge(int nsamples);
  void TestUniqueAdd(int nsamples);
  void TestMerge(int nsamples);

  void TearDown() {
    delete tab1_;
    delete tab2_;
    delete mwr_;
  }

 private:
  static bool SameContents(const vector<string>* v1, const vector<string>* v2);

  SzlType type_;
  SzlField telem_;
  SzlTabWriter* mwr_;
  SzlTabEntry* tab1_;
  SzlTabEntry* tab2_;
};


SzlSampleTest::SzlSampleTest()
    : type_(SzlType::TABLE),
      telem_("", SzlType::kString) {
}


void SzlSampleTest::SetUp() {
  type_.set_table("sample");
  type_.set_element(&telem_);
}


void SzlSampleTest::SetUpParam(int nelem) {
  type_.set_param(nelem);
  string error;
  CHECK(type_.Valid(&error)) << ": " << error;
  mwr_ = SzlTabWriter::CreateSzlTabWriter(type_, &error);
  CHECK(mwr_ != NULL) << ": " << error;
  tab1_ = mwr_->CreateEntry("");
  CHECK(tab1_ != NULL);
  tab2_ = mwr_->CreateEntry("");
  CHECK(tab2_ != NULL);
}


// Do v1 and v2 contain the same set of strings?
bool SzlSampleTest::SameContents(const vector<string>* v1,
                                 const vector<string>* v2) {
  if (v1->size() != v2->size())
    return false;

  for (int i = 0; i < v1->size(); ++i) {
    int j;
    for (j = 0; j < v2->size(); ++j) {
      if ((*v1)[i] == (*v2)[j])
        break;
    }
    if (j >= v2->size())
      return false;
  }
  return true;
}


// Check basic sampling.
void SzlSampleTest::TestSample(int nsamples) {
  SetUpParam(nsamples);
  // Try just filling up the samples, they should all be there.
  for (int i = 0; i < nsamples; ++i) {
    string s = StringPrintf("%d", i);
    tab1_->AddElem(s);
    tab2_->AddElem(s);
    CHECK_EQ(i + 1, tab1_->TotElems());
  }

  CHECK_EQ(nsamples, tab1_->TotElems());

  vector<string> v1;
  tab1_->FlushForDisplay(&v1);
  vector<string> v2;
  tab2_->FlushForDisplay(&v2);
  CHECK_EQ(v1.size(), v2.size());
  CHECK(SameContents(&v1, &v2));

  // Add a bunch more; some should get dropped,
  // and the two tables should be at least somewhat different.
  for (int i = 0; i < nsamples; ++i) {
    string s = StringPrintf("xx-%d", i);
    tab1_->AddElem(s);
    tab2_->AddElem(s);
    CHECK_EQ(nsamples + i + 1, tab1_->TotElems());
  }

  tab1_->FlushForDisplay(&v1);
  tab2_->FlushForDisplay(&v2);

  CHECK_EQ(v1.size(), v2.size());

  // This is not always true wih very small nsamples.
  CHECK(!SameContents(&v1, &v2));
}


// Check initial conditions and empty merge.
void SzlSampleTest::TestEmptyMerge(int nsamples) {
  SetUpParam(nsamples);
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


// Check that unique strings aren't eliminated.
void SzlSampleTest::TestUniqueAdd(int nsamples) {
  SetUpParam(nsamples);
  for (int i = 0; i < nsamples; i++) {
    tab1_->AddElem("hello");
    CHECK_EQ(tab1_->TotElems(), i + 1);
  }
  CHECK_EQ(nsamples, tab1_->TotElems());
}


// Check merge works.
void SzlSampleTest::TestMerge(int nsamples) {
  SetUpParam(nsamples);
  CHECK_EQ(0, tab1_->TotElems());
  for (int i = 0; i < nsamples; i++) {
    tab1_->AddElem("hello");
    CHECK_EQ(tab1_->TotElems(), i + 1);
  }
  vector<string> value;
  tab1_->FlushForDisplay(&value);
  CHECK_EQ(nsamples, value.size());
  CHECK_EQ(0, tab2_->TotElems());
  tab2_->FlushForDisplay(&value);
  CHECK_EQ(1, value.size());
  string s1;
  tab1_->Flush(&s1);
  CHECK_EQ(tab2_->Merge(s1), SzlTabEntry::MergeOk);
  CHECK_EQ(nsamples, tab2_->TotElems());
  string s2;
  tab2_->Flush(&s2);
  CHECK_EQ(s1, s2);
}


}  // namespace sawzall


int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  typedef sawzall::SzlSampleTest Test;
  Test test;
  test.RunTest(&Test::TestEmptyMerge, 10);
  test.RunTest(&Test::TestEmptyMerge, 128);
  test.RunTest(&Test::TestEmptyMerge, 1000);
  test.RunTest(&Test::TestUniqueAdd, 10);
  test.RunTest(&Test::TestUniqueAdd, 128);
  test.RunTest(&Test::TestUniqueAdd, 1000);
  test.RunTest(&Test::TestSample, 10);
  test.RunTest(&Test::TestSample, 128);
  test.RunTest(&Test::TestSample, 1000);
  test.RunTest(&Test::TestMerge, 10);
  test.RunTest(&Test::TestMerge, 128);
  test.RunTest(&Test::TestMerge, 1000);

  puts("PASS");
  return 0;
}
