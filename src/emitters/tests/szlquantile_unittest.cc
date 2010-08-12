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

// This unittest tests Sawzall's "quantile" table functionality.
// Since the table is currently implemented through SzlComplexTables,
// this also serves as unit test for SzlComplexTables.

#include <stdio.h>
#include <math.h>
#include <string>
#include <vector>
#include <algorithm>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/strutils.h"
#include "utilities/random_base.h"
#include "utilities/acmrandom.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlvalue.h"
#include "public/szltabentry.h"


namespace sawzall {


class SzlQuantileTest {
 public:
  SzlQuantileTest();
  void SetUp();

  void RunTest(void (SzlQuantileTest::*pmf)(int), int value) {
    SetUp();
    (this->*pmf)(value);
  }

  // Test functions.
  void TestEmptyMerge(int nparam);
  void TestPermutedInsertion(int nparam);

  void TearDown() {
    delete tab1_;
    delete tab2_;
    delete mwr_;
  }


 private:
  void SetUpParam(int nelem);

  // Test errors are within expected margin.
  static bool CheckCorrectness(int64 num_inserts, int param,
                               const vector<string> &res);

  // Inserts 'num' strings into the entry pointed by tab.
  void InsertElements(int64 num, SzlTabEntry* tab);

  // Similar to the last function, except instead of inserting
  // all entries into one entry quant, inserts half of them
  // into tab1 and the other half into tab2
  void MixedInsertElements(int64 num, SzlTabEntry* tab1,
                           SzlTabEntry* tab2);

  SzlACMRandom random_;

  SzlType type_;
  SzlField telem_;
  SzlTabWriter* mwr_;
  SzlTabEntry* tab1_;
  SzlTabEntry* tab2_;
};


SzlQuantileTest::SzlQuantileTest()
    : random_(SzlACMRandom::DeterministicSeed()),
      type_(SzlType::TABLE),
      telem_("", SzlType::kString) {
}


void SzlQuantileTest::SetUp() {
  type_.set_table("quantile");
  type_.set_element(&telem_);
}


void SzlQuantileTest::SetUpParam(int nelem) {
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


void SzlQuantileTest::TestEmptyMerge(int num_quantiles) {
  SetUpParam(num_quantiles);
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
  CHECK_EQ(s1, s2);
  CHECK_EQ(tab1_->Merge(s2), SzlTabEntry::MergeOk);
  string s3;
  tab1_->Flush(&s3);
  CHECK_EQ(s1, s3);
}


// Insert num_quantiles (= param) elements and check for correctness.
void SzlQuantileTest::TestPermutedInsertion(int num_quantiles) {
  SetUpParam(num_quantiles);
  string encoded;
  vector<string> result;
  InsertElements(num_quantiles, tab1_);
  tab1_->FlushForDisplay(&result);
  tab1_->Flush(&encoded);
  tab1_->Merge(encoded);
  CHECK_EQ(SzlTabEntry::MergeOk, tab2_->Merge(encoded));
  CHECK_EQ(num_quantiles, tab2_->TotElems());
  CHECK_EQ(num_quantiles, tab1_->TotElems());

  tab1_->Clear();
  CHECK_EQ(0, tab1_->TotElems());

  // Check correctness of results.
  CHECK_EQ(MaxInt(2, num_quantiles), result.size());
  CHECK(CheckCorrectness(num_quantiles, num_quantiles, result));

  // Repeat above for  100 * num_quantiles elements
  InsertElements(100 * num_quantiles, tab1_);
  tab1_->FlushForDisplay(&result);
  tab1_->Flush(&encoded);
  tab1_->Merge(encoded);
  CHECK_EQ(tab2_->Merge(encoded), SzlTabEntry::MergeOk);

  // Again flush into tab2_ and see if the numbers add up.
  CHECK_EQ(101 * num_quantiles, tab2_->TotElems());
  CHECK_EQ(100 * num_quantiles, tab1_->TotElems());

  CHECK_EQ(MaxInt(2, num_quantiles), result.size());
  CHECK(CheckCorrectness(100 * num_quantiles, num_quantiles, result));

  // Now, check if the merging went well.

  // Clear both entries.
  tab1_->Clear();
  tab2_->Clear();
  CHECK_EQ(0, tab1_->TotElems());
  CHECK_EQ(0, tab2_->TotElems());

  // Insert into both the entries.
  MixedInsertElements(100 * num_quantiles, tab1_, tab2_);
  CHECK_EQ(50 * num_quantiles, tab1_->TotElems());
  tab1_->Flush(&encoded);

  // Contents of tab1_ Merged into tab2_.
  CHECK_EQ(SzlTabEntry::MergeOk, tab2_->Merge(encoded));
  CHECK_EQ(100 * num_quantiles, tab2_->TotElems());

  // Now, check contents of tab2_.
  tab2_->FlushForDisplay(&result);
  CHECK_EQ(MaxInt(2, num_quantiles), result.size());

  // Check the results obtained from result for correctness.
  CHECK(CheckCorrectness(100 * num_quantiles, num_quantiles, result));
}


// Test errors are within expected margin.
bool SzlQuantileTest::CheckCorrectness(int64 num_inserts, int param,
                                       const vector<string> &rvec) {
  // Relative error we are ready to tolerate in terms
  // of parameter.
  const double eps = 1.0 / (MaxInt(1, param - 1));

  // Max absolute error we can tolerate.
  const int64 error = static_cast<int64>(ceil(eps * num_inserts));

  // Size of results vector.
  const int size = rvec.size();

  int index;

  // We know that the inserted elements are strings
  // that have index embedded in them. See the Insert
  // elements functions below.

  // Check the max and min elements.
  SzlDecoder dec(rvec[0].data(), rvec[0].size());
  string vstr;
  dec.GetString(&vstr);
  // Obtain index and check it is zero.
  CHECK_EQ(sscanf(vstr.c_str(), "xx-%d", &index), 1);
  CHECK_EQ(0, index) << " : lowest value not equal to min inserted";

  dec.Init(rvec[size - 1].data(), rvec[size - 1].size());
  dec.GetString(&vstr);
  // Obtain index and check it is equal to max (num_inserts -1).
  CHECK_EQ(sscanf(vstr.c_str(), "xx-%d", &index), 1);
  CHECK_EQ(num_inserts - 1, index) << " : highest value not equal"
                                  << " to max  inserted";

  // For each result obtain index and see if it is
  // within the tolerable range. The ideal index
  // we are expecting is calculated as next_rank.
  for (int i = 1; i < size - 1; ++i) {
    int64 next_rank = num_inserts * i / (size - 1);
    dec.Init(rvec[i].data(), rvec[i].size());
    dec.GetString(&vstr);
    CHECK_EQ(sscanf(vstr.c_str(), "xx-%d", &index), 1);
    CHECK_GE(error, abs(index - next_rank))
                                    << " : one of middle entries"
                                    << " not within bounds";
  }
  return true;
}


void SzlQuantileTest::InsertElements(int64 num, SzlTabEntry* tab) {
  vector<string> vals;
  for (int64 i = 0; i < num; ++i) {
    vals.push_back(StringPrintf("xx-%09lld", i));
  }

  // Randomly permute data before inserting.
  random_shuffle(vals.begin(), vals.end(), random_);
  for (int64 i = 0; i < num; ++i) {
    // Encode as SzlValue and insert into tab.
    SzlEncoder enc;
    enc.PutString(vals[i].c_str());
    tab->AddElem(enc.data());
  }
}


void SzlQuantileTest::MixedInsertElements(int64 num, SzlTabEntry* tab1,
                                          SzlTabEntry* tab2) {
  vector<string> vals;
  for (int64 i = 0; i < num; ++i) {
    vals.push_back(StringPrintf("xx-%09lld", i));
  }

  // Randomly permute data before inserting.
  random_shuffle(vals.begin(), vals.end(), random_);

  // First (permuted) half goes into tab1.
  for (int64 i = 0; i < num / 2; ++i) {
    SzlEncoder enc;
    enc.PutString(vals[i].c_str());
    tab1->AddElem(enc.data());
  }

  // Second half goes into tab2.
  for (int64 i = num / 2; i < num; ++i) {
    SzlEncoder enc;
    enc.PutString(vals[i].c_str());
    tab2->AddElem(enc.data());
  }
}


}  // namespace sawzall


int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  typedef sawzall::SzlQuantileTest Test;
  Test test;
  test.RunTest(&Test::TestEmptyMerge, 1);
  test.RunTest(&Test::TestEmptyMerge, 10);
  test.RunTest(&Test::TestEmptyMerge, 100);
  test.RunTest(&Test::TestPermutedInsertion, 1);
  test.RunTest(&Test::TestPermutedInsertion, 10);
  test.RunTest(&Test::TestPermutedInsertion, 100);

  puts("PASS");
  return 0;
}
