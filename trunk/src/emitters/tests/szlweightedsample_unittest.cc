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

// Limited testing.

#include <stdio.h>
#include <string>
#include <vector>
#include <limits>

#include "public/porting.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/szltabentry.h"


namespace sawzall {


SzlTabWriter* CreateTabWriter(const SzlType* weight_type,
                              int kMaxSamples, string* error) {
  SzlType type(SzlType::TABLE);
  type.set_table("weightedsample");
  SzlField element("", SzlType::kString);
  type.set_element(&element);
  if (weight_type != NULL) {
    SzlField weight("", *weight_type);
    type.set_weight(&weight);
  }
  type.set_param(kMaxSamples);
  return SzlTabWriter::CreateSzlTabWriter(type, error);
}

// The elements of entry must have been set by Uint64ToKey.
void GetElementsByEncodedDispValue(SzlTabEntry* entry, vector<uint64>* elems) {
  vector<string> encoded_values;
  entry->FlushForDisplay(&encoded_values);
  elems->resize(encoded_values.size());
  for (size_t i = 0; i < encoded_values.size(); ++i) {
    CHECK_GE(encoded_values[i].size(), 8);
    // Ignore the encoded data after the uint64 element.
    (*elems)[i] = KeyToUint64(string(encoded_values[i].data(), 8));
  }
}

void RunTest1() {
  static const double kWeights[] = {
      -numeric_limits<double>::infinity(),
      -numeric_limits<double>::max(),
      -1e12,
      -1.0,
      -1e-12,
      -numeric_limits<double>::epsilon(),
      -numeric_limits<double>::min(),
      0.0,
      numeric_limits<double>::quiet_NaN(),
      numeric_limits<double>::min(),
      numeric_limits<double>::epsilon(),
      1e-12,
      1.0,
      1e12,
      numeric_limits<double>::max(),
      numeric_limits<double>::infinity()
  };
  const int kFirstPositiveWeightIndex = 9;
  const int kSamples = ARRAYSIZE(kWeights);

  string error;
  SzlTabWriter* writer =
      CreateTabWriter(&SzlType::kFloat, kSamples + 1, &error);
  CHECK(writer != NULL)  << ": " << error;
  SzlTabEntry* entry = writer->CreateEntry("");
  CHECK(entry != NULL);

  vector<uint64> expected_samples;
  vector<uint64> elems;
  for (int i = 0; i < kSamples; ++i) {
    SzlValue weight(kWeights[i]);
    entry->AddWeightedElem(Uint64ToKey(i), weight);
    if (i >= kFirstPositiveWeightIndex)
      expected_samples.push_back(i);
    GetElementsByEncodedDispValue(entry, &elems);
    CHECK_EQ(expected_samples, elems);
  }
  delete writer;
  delete entry;
}

void RunTest2() {
  const int kMaxSamples = 5;
  const int kSamples = 1000;

  string error;
  SzlTabWriter* writer =
      CreateTabWriter(&SzlType::kInt, kMaxSamples, &error);
  CHECK(writer != NULL)  << ": " << error;
  SzlTabEntry* entry = writer->CreateEntry("");
  CHECK(entry != NULL);

  vector<uint64> elems;
  for (int i = 0; i < kSamples; ++i) {
    entry->AddElem(Uint64ToKey(i * 10));
    GetElementsByEncodedDispValue(entry, &elems);
    for (int j = 0; j < elems.size(); ++j)
      CHECK_EQ(0, elems[j] % 10);
  }
  CHECK_EQ(kMaxSamples, elems.size());
  delete writer;
  delete entry;
}

// The elements of entry must have been set by Uint64ToKey(x), where x is a
// multiple of 8.
void TestMergingEntry(int max_samples, SzlTabEntry* entry1, SzlTabEntry* entry2,
                      SzlTabEntry* merged_entry) {
  string encoded;
  merged_entry->Clear();
  entry1->Flush(&encoded);
  entry1->Merge(encoded);
  CHECK_EQ(SzlTabEntry::MergeOk, merged_entry->Merge(encoded));
  entry2->Flush(&encoded);
  entry2->Merge(encoded);
  CHECK_EQ(SzlTabEntry::MergeOk, merged_entry->Merge(encoded));
  vector<uint64> elems;
  GetElementsByEncodedDispValue(merged_entry, &elems);
  CHECK_EQ(entry1->TotElems() + entry2->TotElems(), merged_entry->TotElems());
  CHECK_EQ(MinInt(max_samples,
                entry1->TupleCount() + entry2->TupleCount()), elems.size());
  for (int j = 0; j < elems.size(); ++j)
    CHECK_EQ(0, elems[j] % 8);
}

void RunTest3() {
  const int kMaxSamples = 5;
  const int kSamples = 1000;

  string error;
  SzlTabWriter* writer = CreateTabWriter(&SzlType::kInt, kMaxSamples, &error);
  CHECK(writer != NULL) << ": " << error;
  SzlTabEntry* entry1 = writer->CreateEntry("");
  CHECK(entry1 != NULL);
  SzlTabEntry* entry2 = writer->CreateEntry("");
  CHECK(entry2 != NULL);
  SzlTabEntry* entry3 = writer->CreateEntry("");
  CHECK(entry3 != NULL);

  for (int i = 0; i < kSamples; ++i) {
    TestMergingEntry(kMaxSamples, entry1, entry2, entry3);
    entry1->AddWeightedElem(Uint64ToKey(i * 16), SzlValue(i * 16 + 4));
    TestMergingEntry(kMaxSamples, entry1, entry2, entry3);
    entry2->AddWeightedElem(Uint64ToKey(i * 16 + 8), SzlValue(i * 16 + 12));
  }
  delete writer;
  delete entry1;
  delete entry2;
  delete entry3;
}

void RunTest4() {
  string error;
  SzlTabWriter* writer;
  writer = CreateTabWriter(&SzlType::kVoid, 10, &error);
  CHECK(writer == NULL);
  writer = CreateTabWriter(&SzlType::kString, 10, &error);
  CHECK(writer == NULL);
  writer = CreateTabWriter(NULL, 10, &error);
  CHECK(writer == NULL);

  writer = CreateTabWriter(&SzlType::kInt, 1000, &error);
  CHECK(writer != NULL) << ": " << error;
  delete writer;
  writer = CreateTabWriter(&SzlType::kInt, 1, &error);
  CHECK(writer != NULL) << ": " << error;
  delete writer;
  writer = CreateTabWriter(&SzlType::kInt, 0, &error);
  CHECK(writer == NULL);
  delete writer;
  writer = CreateTabWriter(&SzlType::kInt, -1, &error);
  CHECK(writer == NULL);
  delete writer;

  writer = CreateTabWriter(&SzlType::kFloat, 1000, &error);
  CHECK(writer != NULL) << ": " << error;
  delete writer;
  writer = CreateTabWriter(&SzlType::kFloat, 1, &error);
  CHECK(writer != NULL) << ": " << error;
  delete writer;
  writer = CreateTabWriter(&SzlType::kFloat, 0, &error);
  CHECK(writer == NULL);
  delete writer;
  writer = CreateTabWriter(&SzlType::kFloat, -1, &error);
  CHECK(writer == NULL);
  delete writer;
}


}  // namespace szl


int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  sawzall::RunTest1();
  sawzall::RunTest2();
  sawzall::RunTest3();
  sawzall::RunTest4();

  puts("PASS");
  return 0;
}
