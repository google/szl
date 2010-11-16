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
#include <math.h>
#include <stdlib.h>
#include <errno.h>

// Have to include the file since there is no header file.
#include "emitters/szlbootstrapsum.cc"

#include "utilities/strutils.h"


namespace sawzall {

static const int kDefaultRandomSeed = 301;
static const double kEpsilon = 0.000000001;

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

// Verifies that dice is statistically sane.
void RunTest1() {
  uint64 random_seed = GetTestRandomSeed();
  string seed = StringPrintf("seed%d", random_seed);
  MTRandom mt_source(random_seed);
  Random64Source bucket_source(random_seed);
  PoissonDice<RandomBase> fast(new MTRandom(seed), true);
  PoissonDice<Random64Source> bucket(&bucket_source, true);
  PoissonDice<RandomBase> slow(new MTRandom(seed), false);

  const int kSamples = 1000000;
  const int kMax = 50;
  double fast_counts[kMax];
  double bucket_counts[kMax];
  double slow_counts[kMax];
  for (int i = 0; i < kMax; ++i) {
    fast_counts[i] = bucket_counts[i] = slow_counts[i] = 0;
  }
  for (int i = 0; i < kSamples; ++i) {
    int roll;
    roll = fast.Roll();
    fast_counts[roll < kMax ? roll : kMax - 1] += 1.0;
    roll = fast.Roll();
    bucket_counts[roll < kMax ? roll : kMax - 1] += 1.0;
    roll = slow.Roll();
    slow_counts[roll < kMax ? roll : kMax - 1] += 1.0;
  }
  double prob = exp(-1.0);
  for (int i = 0; i < kMax; ++i) {
    if (i) {
      prob = prob / static_cast<double>(i);
    }
    // This test is a very rough statistical test to check that the
    // generated distribution is roughly poisson. It compares the
    // observed distribution to the theoretical Poisson PDF and ensures
    // that they are within a reasonable tolerance. With 1 million
    // samples it is reasonable to expect the observed values to be
    // very close to the theoretical values. More sophisticated tests
    // (using R) are required to properly verify the sampling code.
    CHECK_GT(0.0001, abs(long(prob - fast_counts[i] / kSamples)));
    CHECK_GT(0.0001, abs(long(prob - bucket_counts[i] / kSamples)));
    CHECK_GT(0.0001, abs(long(prob - slow_counts[i] / kSamples)));
  }
}

class MockDice {
 public:
  MockDice(int* values, int count) : values_(values), count_(count), i_(0)  { }
  int Roll()  { return values_[(i_ %= count_)++]; }
 private:
  int* values_;
  int count_;
  int i_;
};

void RunTest2() {
  const int kRowSize = 2;
  const int kNumRows = 5;
  Summable original[kRowSize * kNumRows];
  Summable table[kRowSize * kNumRows];
  Summable update[kRowSize];

  int dice_values[] = { 0, 1, 2, 3, 0 };
  MockDice dice(dice_values, sizeof(dice_values)/sizeof(dice_values[0]));;

  int num_integers = 0;
  update[num_integers++].integer = 7;
  update[num_integers + 0].real = 3.1415;

  Summable *o_cursor = &original[0];
  Summable *t_cursor = &table[0];
  for (int i = 0; i < kNumRows; ++i) {
    int j = 0;
    for (; j < num_integers; ++j, ++o_cursor, ++t_cursor) {
      (*o_cursor).integer = (*t_cursor).integer =
          ((i + 1) * (kNumRows + kRowSize - j - i)) % 7;
    }
    for (; j < kRowSize; ++j, ++o_cursor, ++t_cursor) {
      (*o_cursor).real = (*t_cursor).real =
          (i + 1) * (kNumRows + kRowSize - j - i) / 7.0;
    }
  }

  BootstrapSampleAndSum(kRowSize, num_integers, kNumRows,
                        reinterpret_cast<Summable *>(table),
                        reinterpret_cast<Summable *>(update), &dice);

  // The first row should just contain the update
  int offset = 0;
  CHECK_EQ(original[offset + 0].integer + update[0].integer,
            table[offset + 0].integer);
  CHECK_LT(fabs(original[offset + 1].real + update[1].real - 
                table[offset + 1].real), kEpsilon);
  for (int r = 1; r < kNumRows; ++r) {
    offset += kRowSize;
    CHECK_EQ(original[offset + 0].integer + (r - 1) * update[0].integer,
              table[offset + 0].integer);
    CHECK_LT(fabs(original[offset + 1].real + (r - 1) * update[1].real -
                  table[offset + 1].real), kEpsilon);
  }
}

void RunTest3() {
  SzlType element_type(SzlType::INT);
  string error;
  CHECK(element_type.Valid(&error)) << error;
  SzlOps element_ops(element_type);
  SzlValue value(11);
  SzlValue weight(0);
  string input_str;
  element_ops.AppendToString(value, &input_str);

  int dice_values[] = { 1, 0, 0, 0 };
  MockDice dice(dice_values, sizeof(dice_values)/sizeof(dice_values[0]));;

  SzlBootstrapsum::SzlBootstrapsumEntry<MockDice> entry(element_ops, 2, &dice);
  CHECK_EQ(sizeof(entry), entry.Memory());
  CHECK_EQ(0, entry.TotElems());

  entry.AddWeightedElem(input_str, weight);
  CHECK_EQ(1, entry.TotElems());
  const int mem_with_table = entry.Memory();
  CHECK_LT(sizeof(entry), mem_with_table);
  entry.AddWeightedElem(input_str, weight);
  CHECK_EQ(mem_with_table, entry.Memory());
  CHECK_EQ(2, entry.TotElems());

  string output;
  entry.Flush(&output);
  CHECK_EQ(sizeof(entry), entry.Memory());
  CHECK_EQ(0, entry.TotElems());

  SzlDecoder dec(output.data(), output.size());
  int64 count;
  CHECK(dec.GetInt(&count));
  CHECK_EQ(2, count);
  CHECK(element_ops.Decode(&dec, &value));
  CHECK_EQ(2 * 11, value.i);
  CHECK(element_ops.Decode(&dec, &value));
  CHECK_EQ(11, value.i);
  CHECK(dec.done());

  element_ops.Clear(&value);
}

void RunTest4() {
  SzlType element_type(SzlType::TUPLE);
  element_type.AddField("f", SzlType(SzlType::FLOAT));
  element_type.AddField("i", SzlType(SzlType::INT));
  string error;
  CHECK(element_type.Valid(&error)) << error;
  SzlOps element_ops(element_type);
  SzlValue value;
  SzlValue weight(0);
  element_ops.PutFloat(3.14, 0, &value);
  element_ops.PutInt(7, 1, &value);
  string input_str;
  element_ops.AppendToString(value, &input_str);

  int dice_values[] = { 2, 0 };
  MockDice dice(dice_values, sizeof(dice_values)/sizeof(dice_values[0]));;
  SzlBootstrapsum::SzlBootstrapsumEntry<MockDice> entry(element_ops, 2, &dice);

  entry.AddWeightedElem(input_str, weight);
  string output;
  entry.Flush(&output);

  SzlDecoder dec(output.data(), output.size());
  int64 count;
  CHECK(dec.GetInt(&count));
  CHECK_EQ(1, count);
  CHECK(element_ops.Decode(&dec, &value));
  CHECK_LT(fabs(3.14 - value.s.vals[0].f), kEpsilon);
  CHECK_EQ(7, value.s.vals[1].i);
  CHECK(element_ops.Decode(&dec, &value));
  CHECK_LT(fabs((2 * 3.14) - value.s.vals[0].f), kEpsilon);
  CHECK_EQ(2 * 7, value.s.vals[1].i);
  CHECK(dec.done());

  element_ops.Clear(&value);
}

void RunTest5() {
  SzlType element_type(SzlType::TUPLE);
  element_type.AddField("f", SzlType(SzlType::FLOAT));
  element_type.AddField("i", SzlType(SzlType::INT));
  string error;
  CHECK(element_type.Valid(&error)) << error;
  SzlOps element_ops(element_type);
  SzlValue value;
  SzlValue weight(0);
  element_ops.PutFloat(3.14, 0, &value);
  element_ops.PutInt(7, 1, &value);
  string input_str;
  element_ops.AppendToString(value, &input_str);

  int dice_values[] = { 1, 0, 1, 0 };
  MockDice dice(dice_values, sizeof(dice_values)/sizeof(dice_values[0]));;
  SzlBootstrapsum::SzlBootstrapsumEntry<MockDice> entry(element_ops, 2, &dice);

  entry.AddWeightedElem(input_str, weight);
  string output;
  entry.Flush(&output);

  SzlBootstrapsum::SzlBootstrapsumEntry<MockDice> merger(element_ops, 2, &dice);
  merger.Merge(output);
  string merged;
  merger.Flush(&merged);
  CHECK_EQ(output, merged);
  merged.clear();

  merger.Merge(output);
  merger.AddWeightedElem(input_str, weight);
  merger.Flush(&merged);

  merger.Merge(output);
  merger.Merge(output);
  CHECK_EQ(2, merger.TotElems());

  string double_merge;
  merger.Flush(&double_merge);
  CHECK_EQ(merged, double_merge);

  element_ops.Clear(&value);
}

void RunTest6() {
  SzlType element_type(SzlType::TUPLE);
  element_type.AddField("f", SzlType(SzlType::FLOAT));
  element_type.AddField("i", SzlType(SzlType::STRING));
  SzlType table_type(SzlType::TABLE);
  table_type.set_table("bootstrapsum");
  table_type.set_param(20);
  table_type.set_element("element", element_type);
  table_type.set_weight("bucket", SzlType::kFingerprint);
  string error;
  CHECK(element_type.Valid(&error)) << error;
  error.clear();
  SzlTabWriter* writer = SzlTabWriter::CreateSzlTabWriter(table_type, &error);
  CHECK(writer == NULL);
  CHECK_LT(0, error.size());
  delete writer;
}

void RunTest7() {
  SzlType element_type(SzlType::TUPLE);
  element_type.AddField("f", SzlType(SzlType::FLOAT));
  element_type.AddField("i", SzlType(SzlType::INT));
  SzlType table_type(SzlType::TABLE);
  table_type.set_table("bootstrapsum");
  table_type.set_param(20);
  table_type.set_element("element", element_type);
  table_type.set_weight("bucket", SzlType::kFingerprint);
  string error;
  CHECK(element_type.Valid(&error)) << error;

  SzlOps element_ops(element_type);
  SzlValue value;
  SzlValue weight(0);
  element_ops.PutFloat(3.14, 0, &value);
  element_ops.PutInt(7, 1, &value);
  string input_str;
  element_ops.AppendToString(value, &input_str);
  element_ops.Clear(&value);

  error.clear();
  SzlTabWriter* writer = SzlBootstrapsum::Create(table_type, &error);
  CHECK(writer != NULL);

  SzlTabEntry* entry1 = writer->CreateEntry("1");
  SzlTabEntry* entry2 = writer->CreateEntry("2");
  SzlTabEntry* entry3 = writer->CreateEntry("3");

  const string kSeed1("seed1");
  const string kSeed2("seed2");

  writer->SetRandomSeed(kSeed1);
  entry1->AddWeightedElem(input_str, weight);
  writer->SetRandomSeed(kSeed2);
  entry2->AddWeightedElem(input_str, weight);
  writer->SetRandomSeed(kSeed1);
  entry3->AddWeightedElem(input_str, weight);

  string value1;
  entry1->Flush(&value1);
  string value2;
  entry2->Flush(&value2);
  string value3;
  entry3->Flush(&value3);

  CHECK_NE(value1, value2);
  CHECK_EQ(value1, value3);
  delete entry1;
  delete entry2;
  delete entry3;
  delete writer;
}

static const char *kBenchmarkSeed = "il5a,u518/.,re097r";

void BM_PoissonDice_FastRoll(int iters) {
  PoissonDice<RandomBase>* dice =
    new PoissonDice<RandomBase>(new MTRandom(kBenchmarkSeed), true);

  for (int i = 0; i < iters; ++i) {
    for (int j = 0; j < 100; ++j) {
      dice->Roll();
    }
  }
  delete dice;
}

void BM_PoissonDice_SlowRoll(int iters) {
  PoissonDice<RandomBase>* dice
     = new PoissonDice<RandomBase>(new MTRandom(kBenchmarkSeed), false);

  for (int i = 0; i < iters; ++i) {
    for (int j = 0; j < 100; ++j) {
      dice->Roll();
    }
  }
  delete dice;
}

class VirtualDice : private PoissonDice<RandomBase> {
 public:
  VirtualDice(RandomBase *rng, bool fast_path)
      : PoissonDice<RandomBase>(rng, fast_path) {}
  virtual ~VirtualDice() {}

  virtual int VirtualRoll() {
    return Roll();
  }
};

// We do not make this Roll method virtual for testing in PoissonDice
// to avoid the cost of virtual dispatch. This benchmark helps justify
// this decision.
void BM_PoissonDice_VirtualRoll(int iters) {
  VirtualDice* dice = new VirtualDice(new MTRandom(kBenchmarkSeed), true);

  for (int i = 0; i < iters; ++i) {
    for (int j = 0; j < 100; ++j) {
      dice->VirtualRoll();
    }
  }
  delete dice;
}

}  // namespace sawzall

int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  sawzall::RunTest1();
  sawzall::RunTest2();
  sawzall::RunTest3();
  sawzall::RunTest4();
  sawzall::RunTest5();
  sawzall::RunTest6();
  sawzall::RunTest7();

  // TODO: consider timing these?
  sawzall::BM_PoissonDice_FastRoll(1);
  sawzall::BM_PoissonDice_SlowRoll(1);
  sawzall::BM_PoissonDice_VirtualRoll(1);

  puts("PASS");
  return 0;
}

