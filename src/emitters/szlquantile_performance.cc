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

// Analyze memory requirements of class SzlQuantile.
// We study different input sequences: sorted, identical, random,
// reverse_sorted.  For each sequence, we invoke
// SzlQuantileEntry::Flush() when the sequence terminates. Thereafter, we invoke
// SzlQuantileEntry::Merge() "n" times (on the same string that was just
// Flush()ed) and invoke a final SzlQuantileEntry::Flush().

#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <set>
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
#include "public/szlresults.h"
#include "public/szltabentry.h"

#include "emitters/szlquantile.h"


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


enum InsertionSequence {
  IS_SORTED,
  IS_IDENTICAL,
  IS_RANDOM,
  IS_REVERSESORTED,
  IS_NUM_INSERTION_SEQUENCES
};
string InsertionSequenceName[] = {
  "sorted",
  "identical",
  "random",
  "reverse_sorted",
  "no_name"
};

// Inserts 'num' strings into the entry pointed by quant.
// Strings are ordered and have the format "xx-%09lld" for i from 0 to num - 1.
// The insertion sequence is determined by "seq".
static int InsertElements(
    int64 num, SzlTabEntry* quant,
    const SzlACMRandom& random, enum InsertionSequence seq) {
  CHECK_GE(seq, 0);
  CHECK_LT(seq, IS_NUM_INSERTION_SEQUENCES);
  VLOG(1) << StringPrintf("Inserting %lld elements in sequence '%s'",
                          num, InsertionSequenceName[seq].c_str());
  int memory = quant->Memory();
  vector<string> vals;

  switch (seq) {
    case IS_RANDOM:
      for (int64 i = 0; i < num; ++i) {
        vals.push_back(StringPrintf("xx-%09lld", i));
      }
      random_shuffle(vals.begin(), vals.end());
      break;

    case IS_SORTED:
      for (int64 i = 0; i < num; ++i) {
        vals.push_back(StringPrintf("xx-%09lld", i));
      }
      break;

    case IS_REVERSESORTED:
      for (int64 i = num - 1; i >= 0; --i) {
        vals.push_back(StringPrintf("xx-%09lld", i));
      }
      break;

    case IS_IDENTICAL:
      for (int64 i = 0; i < num; ++i) {
        vals.push_back(StringPrintf("xx-%09lld", 0LL));
      }
      break;

    default: LOG(FATAL) << "Unsupported value of parameter 'seq' encountered";
  }

  for (int64 i = 0; i < num; ++i) {
    SzlEncoder enc;
    enc.PutString(vals[i].c_str());
    //memory += quant->AddElem(enc.data());
    quant->AddElem(enc.data());
  }
  return memory;
}

// For each insertion sequence "seq", we create "num_steps" SzlTabEntry's,
// each populated with "num_quantiles * scaling_factor" members.
// Then we invoke "SzlQuantileEntry::Merge()" on the "num_steps" entries,
// one by one. At each step, we measure and report the size of Flush()'d state.
static void Run(int num_quantiles, const SzlACMRandom& random,
                enum InsertionSequence seq, int num_steps, int scaling_factor) {
  // Create a table of type "quantile"
  SzlType t(SzlType::TABLE);
  t.set_table("quantile");
  SzlField tfld("", SzlType::kString);
  t.set_element(&tfld);
  t.set_param(num_quantiles);
  string error;
  CHECK(t.Valid(&error)) << ": " << error;

  SzlTabWriter* swr = SzlTabWriter::CreateSzlTabWriter(t, &error);
  CHECK(swr != NULL) << ": " << error;

  SzlResults* sres = SzlResults::CreateSzlResults(t, &error);
  CHECK(sres != NULL) << ": " << error;

  string* flush_state = new string[num_steps];
  SzlTabEntry** quant = new SzlTabEntry*[num_steps];

  for (int i = 0; i < num_steps; ++i) {
    quant[i] = swr->CreateEntry(StringPrintf("%d", i).c_str());
    const int memory = InsertElements(
        scaling_factor * num_quantiles, quant[i], random, seq);
    quant[i]->Flush(&flush_state[i]);
    CHECK_EQ(quant[i]->TotElems(), 0);
    VLOG(1) << StringPrintf("quant[%d] has memory=%d "
                            "flush_state=%zu\n",
                            i, memory, flush_state[i].size());

    CHECK(sres->ParseFromString(flush_state[i]));
    CHECK_EQ(sres->Results()->size(), max(2, num_quantiles));
  }

  fprintf(stdout, "\n\nAnalysis of insertion sequence '%s'\n",
          InsertionSequenceName[seq].c_str());
  SzlTabEntry *merged_quant = swr->CreateEntry("");
  for (int num_flushes = 1;
       num_flushes <= num_steps; ++num_flushes) {
    for (int i = 0; i < num_flushes; ++i) {
      CHECK_EQ(merged_quant->Merge(flush_state[i]), SzlTabEntry::MergeOk);
      CHECK_EQ(merged_quant->TotElems(),
               (i + 1) * scaling_factor * num_quantiles);
    }
    string merged_flush_state;
    merged_quant->Flush(&merged_flush_state);
    CHECK_EQ(merged_quant->TotElems(), 0);
    fprintf(stdout, "Flush_state after %d merges = %zu\n",
            num_flushes, merged_flush_state.size());
    CHECK(sres->ParseFromString(merged_flush_state));
    CHECK_EQ(sres->Results()->size(), max(2, num_quantiles));
  }
  delete merged_quant;

  for (int i = 0; i < num_steps; ++i) {
    delete quant[i];
  }
  delete[] quant;
  delete[] flush_state;
  delete sres;
  delete swr;
}


int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  const int num_quantiles = 100;
  SzlACMRandom random(GetTestRandomSeed());

  for (int x = 0; x < IS_NUM_INSERTION_SEQUENCES; ++x) {
    Run(num_quantiles, random,
        static_cast<enum InsertionSequence>(x), 20, 1000);
  }

  return 0;
}
