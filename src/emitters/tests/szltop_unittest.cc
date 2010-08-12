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

// We need PRId64, which is only defined if we explicitly ask for it.
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <stdio.h>
#include <math.h>                   // For sqrt.
#include <errno.h>

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


static const int kDefaultRandomSeed = 301;

// Extract default flag values from environment.
static int GetTestRandomSeed() {
  int32 random_seed = kDefaultRandomSeed;

  // Flag value specified in environment?
  const char * value = getenv("TEST_RANDOM_SEED");
  if (value && value[0]) {
    char *endptr = NULL;
    errno = 0;
    long int_value = strtol(value, &endptr, 10);
    if ((endptr == value + strlen(value)) && !errno) {
      random_seed = int_value;
    }
  }

  return random_seed;
}


// For sorting.
class IndirectOrder {
 public:
  bool operator()(const int& i1, const int& i2) const {
    return (*base_)[i1] > (*base_)[i2];
  }
  explicit IndirectOrder(vector<int64>* vec)
      : base_(vec) {
  }

 protected:
  const vector<int64>* base_;
};


class SzlTopTest {
 public:
  void SetUp();
  SzlTopTest();

  // Tests
  void EmptyMerge();
  void RandomTest();
  void RandomTestPaired();
  void TupleCountTest();

  void RunTest(void (SzlTopTest::*pmf)()) {
    SetUp();
    (this->*pmf)();
    TearDown();
  }

  void TearDown() {
    delete tab1_;
    delete tab2_;
    delete twr_;
  }

 private:
  // Retrieve the results from a saw tabentry.
  // Check if top table tab has a reasonably good (or exact) answer.
  // The actual summed weights are in vals.
  void CheckTopMatch(const SzlType& type, SzlTabEntry* tab,
                     vector<int64>* vals, vector<int64>* vals2,
                     bool exact, bool pair);
  // Set v1 to a random value with a Zipfian distribution.
  // Set v2 to a fraction of v1 if not in pair mode.
  static void GenerateTestValues(
      SzlACMRandom* random, int64* v1, int64* v2, bool pair);

  // Put an "elem" with a weight crafted from w1 and w2 to "tab".
  static void TestPutWeightedElem(const SzlOps& ops, SzlTabEntry* tab,
                                  const string& elem, int64 w1, int64 w2);
  void TestTop(SzlACMRandom* random, bool pair);

  void TupleCountTest(SzlACMRandom* random, bool paired);

  // Multipliers against nelem_ to overload tables.
  static const int kBigMult = 50;
  static const int kSmallMult = 2;

  static const int kDispInterval = 7;  // Interval between FlushForDisplay.

  const int nelem_;
  const SzlField telem_;
  SzlType type_;                  // Table type; const out of SetUp().
  SzlType wt_;                    // Weight type; const out of SetUp().
  SzlField tweight_;
  SzlTabWriter* twr_;
  SzlTabEntry* tab1_;
  SzlTabEntry* tab2_;
};


SzlTopTest::SzlTopTest()
    : nelem_(16),
      telem_("", SzlType::kString),
      type_(SzlType::TABLE),
      wt_(SzlType::TUPLE) {
  wt_.AddField("", SzlType::kInt);
  wt_.AddField("", SzlType::kInt);
  tweight_ = SzlField("", wt_);
  type_.set_table("top");
  type_.set_element(&telem_);
  type_.set_param(nelem_);
  type_.set_weight(&tweight_);
}


void SzlTopTest::TestPutWeightedElem(const SzlOps& ops, SzlTabEntry* tab,
                                     const string& elem, int64 w1, int64 w2) {
  SzlValue sv;
  SzlEncoder wenc;
  wenc.PutInt(w1);
  wenc.PutInt(w2);
  string wencstr = wenc.data();
  ops.ParseFromArray(wencstr.data(), wencstr.size(), &sv);
  tab->AddWeightedElem(elem, sv);
  ops.Clear(&sv);
}


void SzlTopTest::GenerateTestValues(
    SzlACMRandom* random, int64* v1, int64* v2, bool pair) {
  *v1 = static_cast<int64>(1. / sqrt(random->RndFloat()));
  if (pair) {
    *v2 = *v1;
  } else {
    *v2 = static_cast<int64>(*v1 * random->RndFloat() + .5);
  }
}


void SzlTopTest::SetUp() {
  string error;
  twr_= SzlTabWriter::CreateSzlTabWriter(type_, &error);
  CHECK(twr_ != NULL) << ": " << error;
  tab1_ = twr_->CreateEntry("");
  tab2_ = twr_->CreateEntry("");
  CHECK(tab1_ != NULL);
  CHECK(tab2_ != NULL);
}


// Retrieve the results from a saw tabentry.
// Check if top table tab got a reasonably good (or exact) answer.
// The actual summed weights are in vals.
void SzlTopTest::CheckTopMatch(const SzlType& type, SzlTabEntry* tab,
                               vector<int64>* vals, vector<int64>* vals2,
                               bool exact, bool paired) {
  CHECK_EQ(vals->size(), vals2->size());
  // Sort the jiggled elements.
  IndirectOrder indorder(vals);
  vector<int> order(vals->size());
  for (int i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  sort(order.begin(), order.end(), indorder);
  vector<int> invorder(order.size());
  for (int i = 0; i < order.size(); ++i) {
    invorder[order[i]] = i;
  }
  // Check the results.
  vector<string> results;
  tab->FlushForDisplay(&results);
  int nelem = type.param();
  CHECK_EQ(nelem, results.size());
  for (int i = 0; i < nelem; ++i) {
    SzlDecoder dec(results[i].data(), results[i].size());
    string estr;
    int64 w, w2;
    double stddev, stddev2;
    CHECK(dec.GetString(&estr));
    CHECK(dec.GetInt(&w));
    CHECK(dec.GetInt(&w2));
    CHECK(dec.GetFloat(&stddev));
    CHECK(dec.GetFloat(&stddev2));
    CHECK(dec.done());

    if (paired) {
      CHECK_EQ(w, w2);
    }

    // Break apart the elem string to find out its original position.
    // Then check how close we came.
    int pos;
    bool match;
    CHECK(match = (sscanf(estr.c_str(), "xx-%d", &pos) == 1));
    if (match) {
      if (exact) {
        CHECK_EQ(w, (*vals)[pos]);
        CHECK_EQ(w2, (*vals2)[pos]);
      } else {
        // For now, report but don't check approximate matches.
        fprintf(stderr, "top[%d]=(%"PRId64", %"PRId64"), actually "
                "%d=(%"PRId64", %"PRId64") (%g%%, %g%%) std.dev.=(%g, %g)\n",
                i, w, w2, invorder[pos], (*vals)[pos], (*vals2)[pos],
                (w - (*vals)[pos]) * 100. / (*vals)[pos],
                (w2 - (*vals2)[pos]) * 100. / (*vals2)[pos],
                stddev, stddev2);

        // We've rigged the second result to always be no larger than the
        // first, so the estimated stddev should be no larger.
        CHECK_LE(stddev2, stddev * 1.01);
      }
    }
  }
}


// Add a bunch of random elements and update their weights several times
// for all the tables.
void SzlTopTest::TestTop(SzlACMRandom* random, bool paired) {
  const SzlOps wops(wt_);

  SzlTabEntry* tab1a = twr_->CreateEntry("");
  CHECK(tab1a != NULL);
  SzlTabEntry* tab2a = twr_->CreateEntry("");
  CHECK(tab2a != NULL);
  vector<int64> vals1(kBigMult * nelem_);
  vector<int64> vals12(kBigMult * nelem_);
  vector<int64> vals1a(kBigMult * nelem_);
  vector<int64> vals1a2(kBigMult * nelem_);
  vector<int64> vals2(kSmallMult * nelem_);
  vector<int64> vals22(kSmallMult * nelem_);
  vector<int64> vals2a(kSmallMult * nelem_);
  vector<int64> vals2a2(kSmallMult * nelem_);
  for (int adds = 0; adds < 2; ++adds) {
    for (int i = 0; i < vals1.size(); ++i) {
      string elem = StringPrintf("xx-%d", i);
      SzlEncoder enc;
      enc.PutString(elem.c_str());

      int64 v, v2;
      GenerateTestValues(random, &v, &v2, paired);
      TestPutWeightedElem(wops, tab1_, enc.data(), v, v2);
      vals1[i] += v;
      vals12[i] += v2;
      GenerateTestValues(random, &v, &v2, paired);
      TestPutWeightedElem(wops, tab1a , enc.data(), v, v2);
      vals1a[i] += v;
      vals1a2[i] += v2;

      // Expected to get exactly right answer with only a few unique elems.
      if (i < vals2.size()) {
        GenerateTestValues(random, &v, &v2, paired);
        TestPutWeightedElem(wops, tab2_, enc.data(), v, v2);
        vals2[i] += v;
        vals22[i] += v2;
        GenerateTestValues(random, &v, &v2, paired);
        TestPutWeightedElem(wops, tab2a, enc.data(), v, v2);
        vals2a[i] += v;
        vals2a2[i] += v2;
      }
      // To verify there is no side effect.
      if (0 == (i % kDispInterval)) {
        vector<string> dummy;
        tab1_->FlushForDisplay(&dummy);
        tab1a->FlushForDisplay(&dummy);
        tab2_->FlushForDisplay(&dummy);
        tab2a->FlushForDisplay(&dummy);
      }
    }
  }
  CheckTopMatch(type_, tab1_, &vals1, &vals12, false, paired);
  CheckTopMatch(type_, tab1a, &vals1a, &vals1a2, false, paired);
  CheckTopMatch(type_, tab2_, &vals2, &vals22, true, paired);
  CheckTopMatch(type_, tab2a, &vals2a, &vals2a2, true, paired);

  // Try the following different merges:
  // no sketch->no sketch, no sketch->sketch, sketch->no sketch, sketch->sketch.
  string state2a;
  tab2a->Flush(&state2a);
  tab2a->Merge(state2a);
  CheckTopMatch(type_, tab2a, &vals2a, &vals2a2, true, paired);

  string state1a;
  tab1a->Flush(&state1a);
  tab1a->Merge(state1a);
  CheckTopMatch(type_, tab1a, &vals1a, &vals1a2, false, paired);

  CHECK_EQ(SzlTabEntry::MergeOk, tab2_->Merge(state2a));
  for (int i = 0; i < vals2.size(); ++i) {
    vals2[i] += vals2a[i];
    vals22[i] += vals2a2[i];
  }
  CheckTopMatch(type_, tab2_, &vals2, &vals22, true, paired);
  CHECK_EQ(SzlTabEntry::MergeOk, tab1_->Merge(state2a));
  CHECK_EQ(SzlTabEntry::MergeOk, tab2_->Merge(state1a));
  CHECK_EQ(SzlTabEntry::MergeOk, tab1_->Merge(state1a));

  delete tab1a;
  delete tab2a;
}


void SzlTopTest::TupleCountTest(SzlACMRandom* random, bool paired) {
  const SzlOps wops(wt_);
  vector<int64> vals1(kBigMult * nelem_);

  for (int i = 0; i < vals1.size(); ++i) {
    string elem = StringPrintf("xx-%d", i);
    SzlEncoder enc;
    enc.PutString(elem.c_str());

    int64 v, v2;
    GenerateTestValues(random, &v, &v2, paired);
    TestPutWeightedElem(wops, tab1_, enc.data(), v, v2);

    int expectedTuples = i < nelem_ ? i + 1 : nelem_;
    CHECK_EQ(tab1_->TupleCount(), expectedTuples);
  }
}


void SzlTopTest::EmptyMerge() {
  CHECK_EQ(0, tab1_->TotElems());
  CHECK_EQ(0, tab2_->TotElems());
  string s1;
  tab1_->Flush(&s1);
  string s2;
  tab2_->Flush(&s2);
  CHECK_EQ(s1, s2);
  CHECK_EQ(SzlTabEntry::MergeOk, tab1_->Merge(s2));
  string s3;
  tab1_->Flush(&s3);
  CHECK_EQ(s1, s3);
}


void SzlTopTest::RandomTest() {
  SzlACMRandom random(GetTestRandomSeed());
  TestTop(&random, false);
}


void SzlTopTest::RandomTestPaired() {
  SzlACMRandom random(GetTestRandomSeed());
  TestTop(&random, true);
}


void SzlTopTest::TupleCountTest() {
  SzlACMRandom random(GetTestRandomSeed());
  TupleCountTest(&random, false);
}


}  // namespace sawzall

int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  typedef sawzall::SzlTopTest Test;
  Test test;
  test.RunTest(&Test::EmptyMerge);
  test.RunTest(&Test::RandomTest);
  test.RunTest(&Test::RandomTestPaired);
  test.RunTest(&Test::TupleCountTest);

  puts("PASS");
  return 0;
}
