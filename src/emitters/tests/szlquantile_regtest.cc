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

// Unittest for quantile table implementation (sawquantile.cc) in szl.

#include <stdio.h>
#include <math.h>
#include <set>
#include <vector>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/strutils.h"
#include "utilities/random_base.h"
#include "utilities/acmrandom.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlvalue.h"
#include "public/szlresults.h"
#include "public/szltabentry.h"


static void TestQuantiles(const int N, const int num_quantiles) {
  CHECK_GE(N, 1);
  CHECK_GE(num_quantiles, 2);
  //VLOG(1) << StringPrintf("TestQuantiles(%d, %d) in progress ...",
  //                        N, num_quantiles);
  VLOG(1) << "TestQuantiles(" << N << ", " << num_quantiles << ") in progress.";

  SzlType t(SzlType::TABLE);
  t.set_table("quantile");
  SzlField tfld("", SzlType::kString);
  t.set_element(&tfld);
  t.set_param(num_quantiles);
  string error;
  CHECK(t.Valid(&error)) << ": " << error;

  SzlTabWriter* swr = SzlTabWriter::CreateSzlTabWriter(t, &error);
  CHECK(swr != NULL) << ": " << error;
  // Create szl tab entries by the table swr.
  SzlTabEntry* quant = swr->CreateEntry("");
  CHECK(quant != NULL);

  // Insert N elements into the table.
  for (int64 i = N;   i >= 1; i -= 2) {
    SzlEncoder enc;
    enc.PutString(StringPrintf("xx-%09lld", i).c_str());
    quant->AddElem(enc.data());
  }
  for (int64 i = N-1; i >= 1; i -= 2) {
    SzlEncoder enc;
    enc.PutString(StringPrintf("xx-%09lld", i).c_str());
    quant->AddElem(enc.data());
  }

  string result;
  quant->Flush(&result);
  CHECK_EQ(quant->TotElems(), 0);
  SzlResults* sres = SzlResults::CreateSzlResults(t, &error);
  CHECK(sres != NULL) << ": " << error;
  CHECK(sres->ParseFromString(result));
  CHECK_EQ(sres->Results()->size(), num_quantiles);

  vector<string> const *rvec = sres->Results();

  SzlDecoder dec(rvec->front().data(), rvec->front().size());
  {  // Check the max and min elements.
    int index;
    string vstr;
    dec.GetString(&vstr);
    CHECK_EQ(sscanf(vstr.c_str(), "xx-%d", &index), 1);
    CHECK(index == 1) << " : lowest value not equal to min inserted";

    dec.Init(rvec->back().data(), rvec->back().size());
    dec.GetString(&vstr);
    CHECK_EQ(sscanf(vstr.c_str(), "xx-%d", &index), 1);
    CHECK(index == N) << " : highest value not equal to max inserted";
  }

  const double epsilon = 1.0 / (num_quantiles - 1.0);
  for (int i = 1; i < sres->Results()->size() - 1; ++i) {
    dec.Init(rvec->at(i).data(), rvec->at(i).size());
    string vstr;
    dec.GetString(&vstr);
    int index;
    CHECK_EQ(sscanf(vstr.c_str(), "xx-%d", &index), 1);
    CHECK_LE(index, static_cast<int>(
                 ceil(i*N/(num_quantiles - 1.0)) + epsilon*N));
    CHECK_GE(index, static_cast<int>(
                 ceil(i*N/(num_quantiles - 1.0)) - epsilon*N));
  }

  delete quant;
  delete swr;
  delete sres;
}

static void TestForNValues(const int N, const int num_quantiles) {
  // This regtest takes over 30 minutes as of 2006/03. With SzlACMRandom::OneIn(5),
  // the #test cases will reduce to one-fifth. SzlACMRandom::GoodSeed() ensures
  // that we choose a different seed everytime the regtest runs.
  SzlACMRandom rnd;
  rnd.Reset(SzlACMRandom::GoodSeed());
  if (rnd.OneIn(5)) {
    TestQuantiles(N, num_quantiles);
  }
  if (rnd.OneIn(5)) {
    TestQuantiles(N, num_quantiles);
  }
}

static void Test(const int num_quantiles) {
  TestForNValues(1, num_quantiles);
  TestForNValues(2, num_quantiles);
  TestForNValues(5, num_quantiles);
  TestForNValues(10, num_quantiles);
  TestForNValues(15, num_quantiles);
  TestForNValues(19, num_quantiles);
  TestForNValues(269, num_quantiles);
  TestForNValues(4423, num_quantiles);
  TestForNValues(80897, num_quantiles);
  TestForNValues(120897, num_quantiles);
  TestForNValues(1000000, num_quantiles);
}

int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  Test(2);
  Test(3);
  Test(9);
  Test(65);
  Test(100);
  Test(233);
  Test(1345);

  puts("PASS");
  return 0;
}
