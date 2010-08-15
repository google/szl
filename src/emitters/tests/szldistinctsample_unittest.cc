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
#include <time.h>
#include <math.h>
#include <string>
#include <vector>
#include <map>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/strutils.h"
#include "utilities/random_base.h"
#include "utilities/acmrandom.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlnamedtype.h"
#include "public/szlresults.h"
#include "public/szltabentry.h"


namespace sawzall {

// Retrieve the results from a szl tabentry.
static void GetResults(const SzlType& type, SzlTabEntry* tab,
                        vector<string>* ret) {
  string error;
  SzlResults* saw_result = SzlResults::CreateSzlResults(type, &error);
  CHECK(NULL != saw_result) << ": " << error;
  string tmp;
  int64 total_elems = tab->TotElems();
  tab->Flush(&tmp);

  CHECK_EQ(SzlTabEntry::MergeOk, tab->Merge(tmp));
  CHECK(saw_result->ParseFromString(tmp));
  CHECK_EQ(total_elems, tab->TotElems());
  CHECK_EQ(total_elems, saw_result->TotElems());

  const vector<string>* res = saw_result->Results();
  CHECK(NULL != res);
  *ret = *res;

  delete saw_result;
}

static void TestDistinctSample(int sample_size, int nelem) {
  // make testing type: minhash(sample_size) of string weight int
  SzlType t = SzlNamedTable("distinctsample")
      .Param(sample_size).Of(SzlNamedString()).Weight(SzlNamedInt()).type();
  string error;
  CHECK(t.Valid(&error)) << ": " << error;

  SzlTabWriter* writer = SzlTabWriter::CreateSzlTabWriter(t, &error);
  CHECK(NULL != writer);
  SzlTabEntry* tab1 = writer->CreateEntry("");
  CHECK(NULL != tab1);
  SzlTabEntry* tab2 = writer->CreateEntry("");
  CHECK(NULL != tab2);

  // first check initial conditions and empty merge
  CHECK_EQ(0, tab1->TotElems());
  vector<string> tab1_results;
  GetResults(t, tab1, &tab1_results);
  CHECK_EQ(0, tab1_results.size());
  vector<string> tab2_results;
  GetResults(t, tab2, &tab2_results);
  CHECK_EQ(0, tab2_results.size());
  string s1;
  tab1->Flush(&s1);
  string s2;
  tab2->Flush(&s2);
  CHECK_EQ(s1, s2);
  CHECK_EQ(SzlTabEntry::MergeOk, tab1->Merge(s2));
  string s3;
  tab1->Flush(&s3);
  CHECK_EQ(s1, s3);

  // Add a bunch of random elements. This is supposed to
  // simulate a Zipfian distribution.
  SzlACMRandom random(SzlACMRandom::DeterministicSeed());

  vector<int64> vals;

  // Generate first table
  for (int i = 0; i < nelem; i++) {
    SzlValue w(static_cast<int64>(1));
    int64 v = random.Next() % 1000000;
    if (i > 0 && random.Next() % 100 < 90) {
      int ii = random.Next() % i;
      v = vals[ii];
    }
    string elem = StringPrintf("A-%09lld", v);
    SzlEncoder enc;
    enc.PutString(elem.c_str());
    tab1->AddWeightedElem(enc.data(), w);
    vals.push_back(v);
  }

  // Generate second table
  for (int i = 0; i < nelem; i++) {
    int64 v = random.Next() % 1000000;
    SzlValue w(static_cast<int64>(1));
    if (i > 0 && random.Next() % 100 < 70) {
      int ii = random.Next() % i;
      v = vals[ii + nelem];
    }
    string elem = StringPrintf("B-%09lld", v);
    SzlEncoder enc;
    enc.PutString(elem.c_str());
    tab2->AddWeightedElem(enc.data(), w);
    vals.push_back(v);
  }

  // Try merging two tables
  string state2;
  tab2->Flush(&state2);
  delete tab2;
  tab2 = NULL;
  CHECK_EQ(SzlTabEntry::MergeOk, tab1->Merge(state2));
  vector<string> results;
  GetResults(t, tab1, &results);

  // Compute true frequencies
  map<int64, int64> freq;
  for (int i = 0; i < 2 * nelem; i++) {
    freq[vals[i] + 10000000 * (i / nelem)]++;
  }

  int64 nUnique = freq.size();

  // Checking the distinctsample table
  // Check that each sampled element has correct frequency count
  for (int i = 0; i < results.size(); i++) {
    SzlDecoder dec(results[i].data(), results[i].size());
    string estr;
    int64 w;
    CHECK(dec.GetString(&estr));
    CHECK(dec.GetInt(&w));
    CHECK(dec.done());

    // check frequency count
    int64 v;
    int which = estr.c_str()[0] - 'A';
    sscanf(estr.c_str()+2, "%"PRId64, &v);
    CHECK_EQ(w, freq[v + 10000000 * which]) << estr;
  }

  // Checking the inversehistogram output table
  string s;
  tab1->Flush(&s);

  t = SzlNamedTable("inversehistogram")
      .Param(sample_size)
      .Of(SzlNamedInt())
      .Weight(SzlNamedInt()).type();
  CHECK(t.Valid(&error)) << ": " << error;

  SzlResults* sawres = SzlResults::CreateSzlResults(t, &error);
  CHECK(NULL != sawres) << ": " << error;
  CHECK(sawres->ParseFromString(s));

  const vector<string>* res = sawres->Results();
  results = *res;

  map<int64, double> ihist;
  vector<int64> ihist_true(nelem * 2);

  // Compute the true inverse distribution
  for (map<int64, int64>::iterator ii = freq.begin(); ii != freq.end(); ii++) {
    ihist_true[ii->second]++;
  }

  // Check each line of the output against true inverse distribution
  for (int i = 0; i < results.size(); i++) {
    SzlDecoder dec(results[i].data(), results[i].size());
    int64 w;
    double f;
    CHECK(dec.GetInt(&w));
    CHECK(dec.GetFloat(&f));
    CHECK(dec.done());
    ihist[w] = f;
    // no value should occur with frequency higher than # elems
    CHECK_LT(w, 2 * nelem);
    double f_true = (ihist_true[w] + 0.0) / nUnique;
    double diff = f - f_true;
    if (w != 0) {
      CHECK_LT(diff, 1 / sqrt(sample_size) + f / 10);
    }
  }

  {
    double dd = (nUnique - ihist[0]) / nUnique;
    printf("nUnique = %"PRId64", ihist[0] = %f dd = %f%%\n",
           nUnique, ihist[0], dd*100);
    CHECK_LT(fabs(dd), 0.05);  // Scream if estimated unique count not precise
  }

  delete sawres;
  delete tab1;
  delete writer;
}

void RunTest() {
  SzlType t = SzlNamedTable("distinctsample")
      .Param(10)
      .Of(SzlNamedString()).Weight(SzlNamedTuple().Field(SzlNamedInt())).type();
  string error;
  CHECK(t.Valid(&error)) << ": " << error;

  SzlTabWriter* writer = SzlTabWriter::CreateSzlTabWriter(t, &error);
  CHECK(NULL != writer);
  SzlTabEntry* tab1 = writer->CreateEntry("");
  CHECK(NULL != tab1);

  // Generate the table
  for (int i = 0; i < 20; i++) {
    SzlOps ops(SzlNamedTuple().Field(SzlNamedInt()).type());
    SzlEncoder value_enc;
    value_enc.PutInt(1);
    SzlValue w;
    CHECK(ops.ParseFromArray(value_enc.data().data(),
                                   value_enc.data().size(), &w));

    SzlEncoder enc;
    enc.PutString(StringPrintf("alabala %d", i).c_str());
    tab1->AddWeightedElem(enc.data(), w);
    ops.Clear(&w);
  }

  string state1;
  tab1->Flush(&state1);

  SzlTabEntry* tab2 = writer->CreateEntry("");
  CHECK_EQ(SzlTabEntry::MergeOk, tab2->Merge(state1));

  string state2;
  tab2->Flush(&state2);
  CHECK_EQ(state1, state2);
  // szl merges the state back after a flush. Make sure it doesn't
  // leak. http://b/issue?id=2183784
  CHECK_EQ(SzlTabEntry::MergeOk, tab2->Merge(state1));

  delete tab1;
  delete tab2;
  delete writer;
}

}  // namespace sawzall

int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  sawzall::TestDistinctSample(5, 1);
  sawzall::TestDistinctSample(10, 30);
  sawzall::TestDistinctSample(150, 5000);
  sawzall::TestDistinctSample(5000, 1000000);
  sawzall::RunTest();

  puts("PASS");
  return 0;
}
