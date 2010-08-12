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


class SzlSetTest {
 public:
  SzlSetTest();
  void SetUp();

  void RunTest(void (SzlSetTest::*pmf)(int), int value) {
    SetUp();
    (this->*pmf)(value);
    TearDown();
  }

  // Test functions.
  void TestEmptyMerge(int nsamples);
  void TestUniqueAdd(int nsamples);
  void TestOverflow(int nsamples);
  void TestSetness(int nsamples);
  void TestTupleCount(int nsamples);

  void TearDown() {
    delete tab1_;
    delete tab2_;
    delete mwr_;
  }

 private:
  void SetUpParam(int nelem);

  SzlType type_;
  SzlField telem_;
  SzlTabWriter* mwr_;
  SzlTabEntry* tab1_;
  SzlTabEntry* tab2_;
};


SzlSetTest::SzlSetTest()
    : type_(SzlType::TABLE),
      telem_("", SzlType::kString) {
  type_.set_table("set");
  type_.set_element(&telem_);
}


void SzlSetTest::SetUp() {
  type_.set_table("set");
  type_.set_element(&telem_);
}


void SzlSetTest::SetUpParam(int nelem) {
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


void SzlSetTest::TestEmptyMerge(int setsize) {
  SetUpParam(setsize);
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


void SzlSetTest::TestUniqueAdd(int setsize) {
  SetUpParam(setsize);
  // Fill with unique elements.
  vector<string> vals;  // Save for comparison.
  for (int i = 0; i < setsize; ++i) {
    string x = StringPrintf("xx-%09lld", static_cast<int64>(i));
    SzlEncoder enc;
    enc.PutString(x.c_str());
    tab1_->AddElem(enc.data());
    vals.push_back(x);
  }

  // Verify contents of tab1.
  vector<string> disp1;
  tab1_->FlushForDisplay(&disp1);
  string enc1;
  tab1_->Flush(&enc1);
  CHECK_EQ(setsize, disp1.size());
  for (int i = 0; i < disp1.size(); ++i) {
    SzlDecoder dec(disp1[i].data(), disp1[i].size());
    string vstr;
    dec.GetString(&vstr);
    CHECK_EQ(vals[i], vstr);
  }

  // Merge tab1 to tab2.
  vector<string> disp2;
  CHECK_EQ(SzlTabEntry::MergeOk, tab2_->Merge(enc1));
  tab2_->FlushForDisplay(&disp2);
  CHECK_EQ(setsize, disp2.size());
  CHECK_EQ(setsize, tab2_->TotElems());

  // Again merge tab1 to tab2.
  CHECK_EQ(SzlTabEntry::MergeOk, tab2_->Merge(enc1));
  tab2_->FlushForDisplay(&disp2);
  CHECK_EQ(setsize, disp2.size());
  CHECK_EQ(setsize * 2, tab2_->TotElems());

  // Verify contents of tab2.
  for (int i = 0; i < disp2.size(); ++i) {
    SzlDecoder dec(disp2[i].data(), disp2[i].size());
    string vstr;
    dec.GetString(&vstr);
    CHECK_EQ(vals[i], vstr);
  }

  // Clear tab1.
  tab1_->Clear();
  tab1_->FlushForDisplay(&disp1);
  CHECK_EQ(1, disp1.size());
  if (disp1.size() > 0) {
    CHECK_EQ(0, disp1[0].size());
  }
}


void SzlSetTest::TestOverflow(int setsize) {
  SetUpParam(setsize);
  // Overfill with unique elements.
  vector<string> vals;  // Save for comparison.
  for (int i = 0; i < setsize * 2; ++i) {
    string x = StringPrintf("xx-%09lld", static_cast<int64>(i));
    SzlEncoder enc;
    enc.PutString(x.c_str());
    tab1_->AddElem(enc.data());
    vals.push_back(x);
  }

  // Overfull sets are ignored, so the result should be an empty vector.
  vector<string> disp1;
  tab1_->FlushForDisplay(&disp1);
  CHECK_EQ(0, disp1.size());

  string output1;
  tab1_->Flush(&output1);
  CHECK_EQ(0, output1.size());
}


void SzlSetTest::TestSetness(int setsize) {
  SetUpParam(setsize);
  const int target = (setsize + 1)/2;

  // Add same thing twice.
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < target; ++j) {
      string x = StringPrintf("xx-%09lld", static_cast<int64>(j));
      SzlEncoder enc;
      enc.PutString(x.c_str());
      tab1_->AddElem(enc.data());
    }
  }
  vector<string> res1;
  tab1_->FlushForDisplay(&res1);
  CHECK_EQ(target, res1.size());
}


void SzlSetTest::TestTupleCount(int setsize) {
  SetUpParam(setsize);
  CHECK_EQ(tab1_->TupleCount(), 0);
  // Fill with unique elements.
  for (int i = 0; i < setsize; ++i) {
    string x = StringPrintf("xx-%09lld", static_cast<int64>(i));
    SzlEncoder enc;
    enc.PutString(x.c_str());
    tab1_->AddElem(enc.data());
    CHECK_EQ(tab1_->TupleCount(), i+1);
  }
}


}  // namespace sawzall


int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  typedef sawzall::SzlSetTest Test;
  Test test;
  test.RunTest(&Test::TestEmptyMerge, 1);
  test.RunTest(&Test::TestEmptyMerge, 18);
  test.RunTest(&Test::TestEmptyMerge, 73000);
  test.RunTest(&Test::TestUniqueAdd, 1);
  test.RunTest(&Test::TestUniqueAdd, 18);
  test.RunTest(&Test::TestUniqueAdd, 73000);
  test.RunTest(&Test::TestOverflow, 1);
  test.RunTest(&Test::TestOverflow, 18);
  test.RunTest(&Test::TestOverflow, 73000);
  test.RunTest(&Test::TestSetness, 1);
  test.RunTest(&Test::TestSetness, 18);
  test.RunTest(&Test::TestSetness, 73000);
  test.RunTest(&Test::TestTupleCount, 500);

  puts("PASS");
  return 0;
}
