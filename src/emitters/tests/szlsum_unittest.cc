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
#include <errno.h>
#include <map>
#include <string>
#include <vector>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/random_base.h"
#include "utilities/acmrandom.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlnamedtype.h"
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


class SzlSumTest {
 public:
  void RunTest(void (SzlSumTest::*pmf)()) {
    (this->*pmf)();
    TearDown();
  }

  void TearDown() {
    delete sum1_;
    delete sum2_;
    delete sumwr_;
  }

  // Tests.
  void CreateWriterAndEntries();
  void PerformsEmptyMerge();
  void PerformsSummingAndMerging();
  void PerformTupleSummingAndMerging();
  void CheckTupleAndMemory();
  void PerformMapSummingAndMerging();

  // Initialization of writers and entries to use a single int type.
  void InitializeSingle() {
    string error;
    SzlType sumt(SzlType::TABLE);
    sumt.set_table("sum");
    SzlField sumte("", SzlType::kInt);
    sumt.set_element(&sumte);
    sumwr_ = SzlTabWriter::CreateSzlTabWriter(sumt, &error);
    sum1_ = sumwr_->CreateEntry("");
    sum2_ = sumwr_->CreateEntry("");
  }

  SzlTabWriter* sumwr_;
  SzlTabEntry* sum1_;
  SzlTabEntry* sum2_;
};


// Tests that the writer and entry creation functions work correctly.
void SzlSumTest::CreateWriterAndEntries() {
  SzlType sumt(SzlType::TABLE);
  sumt.set_table("sum");
  SzlField sumte("", SzlType::kInt);
  sumt.set_element(&sumte);
  string error;
  CHECK(sumt.Valid(&error)) << ": " << error;
  sumwr_ = SzlTabWriter::CreateSzlTabWriter(sumt, &error);
  CHECK_NE(sumwr_, static_cast<SzlTabWriter*>(NULL))
      << "failed to create writer: " << error;
  sum1_ = sumwr_->CreateEntry("");
  CHECK_NE(sum1_, static_cast<SzlTabEntry*>(NULL))
      << "failed to create entry";
  sum2_ = sumwr_->CreateEntry("");
  CHECK_NE(sum2_, static_cast<SzlTabEntry*>(NULL))
      << "failed to create entry";
}


// Tests that the SzlSum types perform an empty merge operation correctly.
void SzlSumTest::PerformsEmptyMerge() {
  InitializeSingle();

  // Perform empty merges.
  CHECK_EQ(sum1_->TotElems(), 0) << "initial number count incorrect";
  CHECK_EQ(sum2_->TotElems(), 0) << "initial number count incorrect";
  string s1;
  sum1_->Flush(&s1);
  string s2;
  sum2_->Flush(&s2);
  CHECK_EQ(s1, s2)
      << "encoding of empty strings does not produce identical results";
  CHECK_EQ(sum1_->Merge(s2), SzlTabEntry::MergeOk)
      << "merge returned error code";
  string s3;
  sum1_->Flush(&s3);
  CHECK_EQ(s1, s3) << "mpty merge did not result in an empty value";
}


// Tests that SzlSum writers and entries perform summing correctly.
void SzlSumTest::PerformsSummingAndMerging() {
  InitializeSingle();

  int64 i = 0;

  // Test a few simple adds.
  int64 tot = 0;
  int64 elements = 0;
  static const int64 kAdds = 1000;

  // Sum by adding together all the numbers from 0 to 999 in sequence. Verify
  // that the element count and sum are correct at each iteration.
  for (i = 0; i < kAdds; i++) {
    tot += i;
    elements++;
    SzlEncoder enc;
    enc.Reset();
    enc.PutInt(i);
    sum1_->AddElem(enc.data());
    CHECK_EQ(sum1_->TotElems(), elements)
        << "did not increment element count correctly";

    // Encode the expected number of elements and expected total. This is
    // necessary as the encoded value consists of a pair of the number of
    // elements and the sum. This sequence generates an encoded string
    // consisting of the actual sum concatenated onto the number of elements.
    enc.Reset();
    enc.PutInt(elements);
    enc.PutInt(tot);

    // Check the number of elements and the total to make sure they are
    // correct.
    string sum1_result;
    sum1_->Flush(&sum1_result);
    sum1_->Merge(sum1_result);
    CHECK_EQ(sum1_result, enc.data())
        << "did not sum correctly, mismatch with actual value";
  }

  string sum1_result;
  string sum2_result;
  SzlEncoder enc2;
  enc2.Reset();
  enc2.PutInt(5);
  sum2_->AddElem(enc2.data());
  CHECK_EQ(sum2_->TotElems(), 1) << "element count incremented incorrectly";
  enc2.Reset();
  enc2.PutInt(12);
  sum2_->AddElem(enc2.data());
  CHECK_EQ(sum2_->TotElems(), 2) << "element count incremented incorrectly";

  // Merge the sums, and confirm the sum value.
  sum2_->Flush(&sum2_result);
  CHECK_EQ(sum1_->Merge(sum2_result), SzlTabEntry::MergeOk)
      << "merge returned failure";
  sum1_->Flush(&sum1_result);

  enc2.Reset();
  enc2.PutInt(elements + 2);
  enc2.PutInt(tot + 5 + 12);
  CHECK_EQ(enc2.data(), sum1_result);
}


void SzlSumTest::PerformTupleSummingAndMerging() {
  // Our testing type: sum of {int, float}.
  SzlType sumt(SzlType::TABLE);
  sumt.set_table("sum");
  SzlType tuplet(SzlType::TUPLE);
  tuplet.AddField("", SzlType::kInt);
  tuplet.AddField("", SzlType::kFloat);
  SzlField sumte("", tuplet);
  sumt.set_element(&sumte);

  // Create a couple of tables.
  string error;
  sumwr_ = SzlTabWriter::CreateSzlTabWriter(sumt, &error);
  sum1_ = sumwr_->CreateEntry("");
  sum2_ = sumwr_->CreateEntry("");

  // First check initial conditions and empty merge.
  CHECK_EQ(sum1_->TotElems(), 0);
  CHECK_EQ(sum2_->TotElems(), 0);
  string s1;
  sum1_->Flush(&s1);
  string s2;
  sum2_->Flush(&s2);
  CHECK_EQ(s1, s2);
  CHECK_EQ(sum1_->Merge(s2), SzlTabEntry::MergeOk);
  string s3;
  sum1_->Flush(&s3);
  CHECK_EQ(s1, s3);

  // Test a few simple adds.
  int64 tot = 0;
  double totf = 0;
  int elements = 0;
  static const int kAdds = 1000;

  // Sum 1000 integer and float values together.
  for (int i = 0; i < kAdds; i++) {
    SzlEncoder enc;
    tot += i;
    totf += 3.14;
    elements++;

    enc.Reset();
    enc.PutInt(i);
    enc.PutFloat(3.14);
    sum1_->AddElem(enc.data());
    CHECK_EQ(sum1_->TotElems(), elements)
        << "did not increment element count correctly";

    // Encode the expected number of elements and expected total. This is
    // necessary as the encoded value consists of a pair of the number of
    // elements and the sum.
    enc.Reset();
    enc.PutInt(elements);
    enc.PutInt(tot);
    enc.PutFloat(totf);

    string sum1_result;
    sum1_->Flush(&sum1_result);
    sum1_->Merge(sum1_result);
    CHECK_EQ(sum1_result, enc.data())
        << "did not sum correctly, mismatch with actual value";
  }
  string sum1_result;
  string sum2_result;
  SzlEncoder enc2;

  // Put a couple of known values into another sum.
  enc2.Reset();
  enc2.PutInt(5);
  enc2.PutFloat(2.71828);
  sum2_->AddElem(enc2.data());
  CHECK_EQ(sum2_->TotElems(), 1);
  enc2.Reset();
  enc2.PutInt(12);
  enc2.PutFloat(17);
  sum2_->AddElem(enc2.data());
  CHECK_EQ(sum2_->TotElems(), 2);

  // Merge the sums, and confirm the sum value.
  sum2_->Flush(&sum2_result);
  CHECK(sum1_->Merge(sum2_result) == SzlTabEntry::MergeOk);
  sum1_->Flush(&sum1_result);

  enc2.Reset();
  enc2.PutInt(elements + 2);
  enc2.PutInt(tot + 5 + 12);
  enc2.PutFloat(totf + 2.71828 + 17);

  CHECK_EQ(enc2.data(), sum1_result);
}


void SzlSumTest::CheckTupleAndMemory() {
  InitializeSingle();
  CHECK_EQ(sum1_->TupleCount(), 1);
  int64 i = 0;

  int initialMem = sum1_->Memory();
  // Test a few simple adds.
  static const int64 kAdds = 1000;

  // Sum by adding together all the numbers from 0 to 999 in sequence. Verify
  // that the tuple count and memory are correct at each iteration.
  for (i = 0; i < kAdds; i++) {
    SzlEncoder enc;
    enc.Reset();
    enc.PutInt(i);
    sum1_->AddElem(enc.data());
    CHECK_EQ(sum1_->TupleCount(), 1) << "tuple count should always be 1";
    CHECK_EQ(sum1_->Memory(), initialMem);
  }
}

namespace {

// This function encodes the specified map and accumulates
// the map counts, if a sum is specified.
void EncodeMap(const map<string, int64>& to_encode,
               SzlEncoder* enc,
               map<string, int64>* sum) {
  enc->Start(SzlType::MAP);
  enc->PutInt(to_encode.size() * 2);  // Length (keys + values)

  // We're relying on the fact that map<> uses sorting.
  for (map<string, int64>::const_iterator pos = to_encode.begin();
       pos != to_encode.end(); ++pos) {
    enc->PutString(pos->first.c_str());
    enc->PutInt(pos->second);

    if (sum != NULL) {
      (*sum)[pos->first] += pos->second;
    }
  }
  enc->End(SzlType::MAP);
}

}  // namespace

void SzlSumTest::PerformMapSummingAndMerging() {
  // Our testing type: table sum of {int, map[string] of int}.
  SzlType sumt(
      SzlNamedTable("sum")
      .Of(SzlNamedTuple()
          .Field(SzlNamedInt())
          .Field(SzlNamedMap()
                 .Index(SzlNamedString())
                 .Of(SzlNamedInt()))).type());

  // Create a couple of tables.
  string error;
  sumwr_ = SzlTabWriter::CreateSzlTabWriter(sumt, &error);
  sum1_ = sumwr_->CreateEntry("");
  sum2_ = sumwr_->CreateEntry("");

  // First check initial conditions and empty merge.
  CHECK_EQ(sum1_->TotElems(), 0);
  CHECK_EQ(sum2_->TotElems(), 0);
  string s1;
  sum1_->Flush(&s1);
  string s2;
  sum2_->Flush(&s2);
  CHECK_EQ(s1, s2);
  CHECK_EQ(sum1_->Merge(s2), SzlTabEntry::MergeOk);
  string s3;
  sum1_->Flush(&s3);
  CHECK_EQ(s1, s3);

  // Test a few simple adds.
  int64 tot = 0;
  map<string, int64> tot_map_counts;
  int elements = 0;
  static const int kAdds = 1000;
  SzlACMRandom rng(GetTestRandomSeed());

  // These keys are used for the map. The keys must
  // be sorted alphabetically!
  static const char *keys[] = { "a", "b", "c", NULL };

  // Sum 1000 integer and float values together.
  for (int i = 0; i < kAdds; i++) {
    map<string, int64> map_counts;
    SzlEncoder enc;
    tot += i;

    // Generate random counts for the map.
    // Note that sometimes the map will be empty, which
    // is fine.
    for (int j = 0; keys[j] != NULL; ++j) {
      if (rng.OneIn(5)) {
        map_counts[keys[j]] = rng.Uniform(5);
      }
    }
    elements++;

    // Emit new counts and sum.
    enc.Reset();
    enc.PutInt(i);
    EncodeMap(map_counts, &enc, &tot_map_counts);

    sum1_->AddElem(enc.data());
    CHECK_EQ(sum1_->TotElems(), elements)
        << "did not increment element count correctly. i = " << i;

    // Encode the expected number of elements and expected total. This is
    // necessary as the encoded value consists of a pair of the number of
    // elements and the sum.
    enc.Reset();
    enc.PutInt(elements);
    enc.PutInt(tot);
    EncodeMap(tot_map_counts, &enc, NULL);

    string sum1_result;
    sum1_->Flush(&sum1_result);
    sum1_->Merge(sum1_result);
    CHECK_EQ(sum1_result, enc.data())
        << "did not sum correctly, mismatch with actual value. i = " << i;
  }
  string sum1_result;
  string sum2_result;
  SzlEncoder enc2;

  // Put a couple of known values into another sum.
  enc2.Reset();
  enc2.PutInt(5);
  {
    map<string, int64> map_counts;
    map_counts["a"] = 10;
    map_counts["b"] = 134;
    map_counts["e"] = 12;
    EncodeMap(map_counts, &enc2, &tot_map_counts);
  }
  sum2_->AddElem(enc2.data());
  CHECK_EQ(sum2_->TotElems(), 1);
  enc2.Reset();
  enc2.PutInt(12);
  {
    map<string, int64> map_counts;
    map_counts["c"] = 2;
    map_counts["b"] = 6;
    map_counts["e"] = 33;
    map_counts["f"] = 100;
    EncodeMap(map_counts, &enc2, &tot_map_counts);
  }
  sum2_->AddElem(enc2.data());
  CHECK_EQ(sum2_->TotElems(), 2);

  // Merge the sums, and confirm the sum value.
  sum2_->Flush(&sum2_result);
  CHECK(sum1_->Merge(sum2_result) == SzlTabEntry::MergeOk);
  sum1_->Flush(&sum1_result);

  enc2.Reset();
  enc2.PutInt(elements + 2);
  enc2.PutInt(tot+5 + 12);
  EncodeMap(tot_map_counts, &enc2, NULL);
  CHECK_EQ(enc2.data(), sum1_result);
}


}  // end namespace sawzall


int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  typedef sawzall::SzlSumTest Test;
  Test test;
  test.RunTest(&Test::CreateWriterAndEntries);
  test.RunTest(&Test::PerformsEmptyMerge);
  test.RunTest(&Test::PerformsSummingAndMerging);
  test.RunTest(&Test::PerformTupleSummingAndMerging);
  test.RunTest(&Test::CheckTupleAndMemory);
  test.RunTest(&Test::PerformMapSummingAndMerging);

  puts("PASS");
  return 0;
}
