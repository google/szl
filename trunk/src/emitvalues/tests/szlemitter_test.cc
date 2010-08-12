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

#include "public/porting.h"
#include "public/logging.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlvalue.h"
#include "public/szlresults.h"
#include "public/emitterinterface.h"
#include "public/szlemitter.h"
#include "public/szltabentry.h"


const int64 kIndex1 = 5;
const int64 kIndex2 = 7;
const int64 kIndex3 = 1;

const int64 kInt1 = 591823;
const int64 kInt2 = 1928378;
const int64 kInt3 = 199928;

const float kFloat1 = 3.14;
const float kFloat2 = 5.1521;
const float kFloat3 = 10.55519283;

static bool fail = false;

typedef SzlEmitter::KeyValuePair KeyValuePair;
typedef pair<string, vector<string> > KeyMergedPair;

// -----------------------------------------------------------------------------

// Override WriteValue() to write results to a vector.

class SzlEmitterTestEmitter : public SzlEmitter {
public:
  SzlEmitterTestEmitter(const string& name, const SzlTabWriter* writer,
                        vector<KeyValuePair>* result)
    : SzlEmitter(name, writer, false), result_(result) { }

private:
  virtual void WriteValue(const string& key, const string& value) {
    result_->push_back(KeyValuePair(key, value));
  }

  vector<KeyValuePair>* result_;
};


// -----------------------------------------------------------------------------


class SzlEmitterTest {
 public:
  SzlEmitterTest()
      : test_table(SzlType::TABLE) {
    test_table.set_table("set");
    test_table.set_param(10);
    data1 = "a random string of data";
    data2 = "more data that can be added";
    data3 = "even more data that can be added";
    found1 = false;
    found2 = false;
    found3 = false;
  }

  // Tests
  void KeepsStateCorrectly();
  void AddsIntsCorrectly();
  void AddsBoolsCorrectly();
  void AddsBytesCorrectly();
  void AddsFloatsCorrectly();
  void AddsFingerprintsCorrectly();
  void AddsStringsCorrectly();
  void AddsTimeCorrectly();
  void ClearsEmitterCorrectly();


  void SignalEmitIndex(SzlEmitter* emitter) {
    emitter->Begin(SzlEmitter::EMIT, 0);
    emitter->Begin(SzlEmitter::INDEX, 0);
  }


  void SignalEmitElement(SzlEmitter* emitter) {
    emitter->Begin(SzlEmitter::EMIT, 0);
    emitter->Begin(SzlEmitter::ELEMENT, 0);
  }


  void SignalEndElement(SzlEmitter* emitter) {
    emitter->End(SzlEmitter::ELEMENT, 0);
    emitter->End(SzlEmitter::EMIT, 0);
  }


  void CheckForFinds(bool b1, bool b2, bool b3) {
    CHECK(found1) << "Did not find first element.";
    CHECK(found2) << "Did not find second element.";
    CHECK(found3) << "Did not find third element.";
  }


  void EmitThreeFingerprints(SzlEmitter* emitter) {
    SignalEmitElement(emitter);
    emitter->PutFingerprint(kInt1);
    SignalEndElement(emitter);

    SignalEmitElement(emitter);
    emitter->PutFingerprint(kInt2);
    SignalEndElement(emitter);

    SignalEmitElement(emitter);
    emitter->PutFingerprint(kInt3);
    SignalEndElement(emitter);
  }

  void ValidateThreeFingerprints(SzlEmitter* emitter,
                                 vector<KeyValuePair>* result) {
    string encoded_data1, encoded_data2, encoded_data3;

    SzlEncoder enc;
    enc.Reset();
    enc.PutFingerprint(kInt1);
    encoded_data1 = enc.data();

    enc.Reset();
    enc.PutFingerprint(kInt2);
    encoded_data2 = enc.data();

    enc.Reset();
    enc.PutFingerprint(kInt3);
    encoded_data3 = enc.data();

    emitter->Flusher();
    vector<KeyMergedPair> kmp = ParseMergedResult(result);
    CHECK_EQ(kmp.size(), 1) << "Incorrect number of indices.";
    vector<string>& values = kmp[0].second;
    CHECK_EQ(values.size(), 3) << "Incorrect number of values.";

    for (int i = 0; i < values.size(); i++) {
      if (values[i] == encoded_data1) {
        found1 = true;
      } else if (values[i] == encoded_data2) {
        found2 = true;
      } else if (values[i] == encoded_data3) {
        found3 = true;
      } else {
        fputs("Found a result that should not have existed in the emitter.",
              stderr);
        fail = true;
      }
    }

    CheckForFinds(found1, found2, found3);
  }

  vector<KeyMergedPair> ParseMergedResult(vector<KeyValuePair>* result) {
    vector<KeyMergedPair> kmp;
    SzlResults* szlresults = SzlResults::CreateSzlResults(test_table, &error);
    for (int i = 0; i < result->size(); i++) {
      CHECK(szlresults->ParseFromString((*result)[i].second));
      kmp.push_back(KeyMergedPair((*result)[i].first, *szlresults->Results()));   
    }
    return kmp;
  }

  SzlType test_table;
  string error;

  // Values used for testing.
  string data1;
  string data2;
  string data3;

  bool found1;
  bool found2;
  bool found3;
};


void SzlEmitterTest::KeepsStateCorrectly() {
  SzlField element("", SzlType::kInt);
  test_table.set_element(&element);
  SzlTabWriter* test_wr = SzlTabWriter::CreateSzlTabWriter(test_table, &error);
  SzlEmitter* test_emitter = new SzlEmitter("UnitTest", test_wr, false);
  CHECK(test_emitter != NULL);
  CHECK(test_emitter->name() == "UnitTest") << "Names do not match.";
  delete test_emitter;
  // The SzlEmitter destructor deletes the writer.
}


void SzlEmitterTest::AddsIntsCorrectly() {
  SzlField element("", SzlType::kInt);
  test_table.set_element(&element);
  SzlTabWriter* test_wr = SzlTabWriter::CreateSzlTabWriter(test_table, &error);
  vector<KeyValuePair> result;
  SzlEmitter* test_emitter =
    new SzlEmitterTestEmitter("UnitTest", test_wr, &result);

  // Data must be passed in in the correct format.
  // Add in 3 ints with 3 different indices first.
  SignalEmitIndex(test_emitter);
  test_emitter->PutInt(kIndex1);
  test_emitter->End(SzlEmitter::INDEX, 0);
  test_emitter->Begin(SzlEmitter::ELEMENT, 0);
  test_emitter->PutInt(kInt1);
  SignalEndElement(test_emitter);

  SignalEmitIndex(test_emitter);
  test_emitter->PutInt(kIndex2);
  test_emitter->End(SzlEmitter::INDEX, 0);
  test_emitter->Begin(SzlEmitter::ELEMENT, 0);
  test_emitter->PutInt(kInt2);
  SignalEndElement(test_emitter);

  SignalEmitIndex(test_emitter);
  test_emitter->PutInt(kIndex3);
  test_emitter->End(SzlEmitter::INDEX, 0);
  test_emitter->Begin(SzlEmitter::ELEMENT, 0);
  test_emitter->PutInt(kInt3);
  SignalEndElement(test_emitter);

  // Retrieve the data and check it for correctness.
  string encodedIndex1, encodedIndex2, encodedIndex3;
  string encodedValue1, encodedValue2, encodedValue3;

  SzlEncoder enc;
  enc.Reset();
  enc.PutInt(kIndex1);
  encodedIndex1 = enc.data();
  enc.Reset();
  enc.PutInt(kInt1);
  encodedValue1 = enc.data();

  enc.Reset();
  enc.PutInt(kIndex2);
  encodedIndex2 = enc.data();
  enc.Reset();
  enc.PutInt(kInt2);
  encodedValue2 = enc.data();

  enc.Reset();
  enc.PutInt(kIndex3);
  encodedIndex3 = enc.data();
  enc.Reset();
  enc.PutInt(kInt3);
  encodedValue3 = enc.data();

  test_emitter->Flusher();
  vector<KeyMergedPair> kmp = ParseMergedResult(&result);
  CHECK_EQ(kmp.size(), 3) << "Incorrect number of indices.";

  // A search is necessary as Szl does not guarantee any ordering of the
  // returned values in set table cases.
  for (int i = 0; i < kmp.size(); i++) {
    const string& index = kmp[i].first;
    CHECK_EQ(kmp[i].second.size(), 1);
    const string& value = kmp[i].second[0];
    if (index == encodedIndex1) {
      if (value == encodedValue1)
        found1 = true;
    } else if (index == encodedIndex2) {
      if (value == encodedValue2)
        found2 = true;
    } else if (index == encodedIndex3) {
      if (value == encodedValue3)
        found3 = true;
    } else {
      fputs("Found a result that should not have existed in the emitter.",
            stderr);
      fail = true;
    }
  }
  delete test_emitter;
  // The SzlEmitter destructor deletes the writer.
  CheckForFinds(found1, found2, found3);
}


void SzlEmitterTest::AddsBoolsCorrectly() {
  SzlField element("", SzlType::kBool);
  test_table.set_element(&element);
  SzlTabWriter* test_wr = SzlTabWriter::CreateSzlTabWriter(test_table, &error);
  vector<KeyValuePair> result;
  SzlEmitter* test_emitter =
    new SzlEmitterTestEmitter("UnitTest", test_wr, &result);

  // Data must be passed in in the correct format. This tests adding
  // bool entries to the emitter.
  SignalEmitElement(test_emitter);
  test_emitter->PutBool(true);
  SignalEndElement(test_emitter);

  SignalEmitElement(test_emitter);
  test_emitter->PutBool(false);
  SignalEndElement(test_emitter);

  SignalEmitElement(test_emitter);
  test_emitter->PutBool(true);
  SignalEndElement(test_emitter);

  // Retrieve the data and check it for correctness.
  int true_count = 0;
  int false_count = 0;

  string encoded_true, encoded_false;

  SzlEncoder enc;
  enc.Reset();
  enc.PutBool(true);
  encoded_true = enc.data();

  enc.Reset();
  enc.PutBool(false);
  encoded_false = enc.data();

  test_emitter->Flusher();
  vector<KeyMergedPair> kmp = ParseMergedResult(&result);
  CHECK_EQ(kmp.size(), 1) << "Incorrect number of indices.";
  vector<string>& values = kmp[0].second;
  CHECK_EQ(values.size(), 2) << "Incorrect number of values.";

  for (int i = 0; i < values.size(); i++) {
    if (values[i] == encoded_true) {
      true_count++;
    } else if (values[i] == encoded_false) {
      false_count++;
    }
  }
  delete test_emitter;
  // The SzlEmitter destructor deletes the writer.
  CHECK_EQ(1, true_count) << "Incorrect number of true entries found.";
  CHECK_EQ(1, false_count) << "Incorrect number of false entries found.";
}


void SzlEmitterTest::AddsBytesCorrectly() {
  SzlField element("", SzlType::kBytes);
  test_table.set_element(&element);
  SzlTabWriter* test_wr = SzlTabWriter::CreateSzlTabWriter(test_table, &error);
  vector<KeyValuePair> result;
  SzlEmitter* test_emitter =
    new SzlEmitterTestEmitter("UnitTest", test_wr, &result);

  // Data must be passed in in the correct format. This tests adding byte
  // arrays to the emitter.
  SignalEmitElement(test_emitter);
  test_emitter->PutBytes(data1.data(), data1.length());
  SignalEndElement(test_emitter);

  SignalEmitElement(test_emitter);
  test_emitter->PutBytes(data2.data(), data2.length());
  SignalEndElement(test_emitter);

  SignalEmitElement(test_emitter);
  test_emitter->PutBytes(data3.data(), data3.length());
  SignalEndElement(test_emitter);

  // Retrieve the data and check it for correctness.
  string encoded_data1, encoded_data2, encoded_data3;

  SzlEncoder enc;
  enc.Reset();
  enc.PutBytes(data1.data(), data1.length());
  encoded_data1 = enc.data();

  enc.Reset();
  enc.PutBytes(data2.data(), data2.length());
  encoded_data2 = enc.data();

  enc.Reset();
  enc.PutBytes(data3.data(), data3.length());
  encoded_data3 = enc.data();

  test_emitter->Flusher();
  vector<KeyMergedPair> kmp = ParseMergedResult(&result);
  CHECK_EQ(kmp.size(), 1) << "Incorrect number of indices.";
  vector<string>& values = kmp[0].second;
  CHECK_EQ(values.size(), 3) << "Incorrect number of values.";

  for (int i = 0; i < values.size(); i++) {
    if (values[i] == encoded_data1) {
      found1 = true;
    } else if (values[i] == encoded_data2) {
      found2 = true;
    } else if (values[i] == encoded_data3) {
      found3 = true;
    } else {
      fputs("Found a result that should not have existed in the emitter.",
            stderr);
      fail = true;
    }
  }
  delete test_emitter;
  // The SzlEmitter destructor deletes the writer.
  CheckForFinds(found1, found2, found3);
}


void SzlEmitterTest::AddsFloatsCorrectly() {
  SzlField element("", SzlType::kFloat);
  test_table.set_element(&element);
  SzlTabWriter* test_wr = SzlTabWriter::CreateSzlTabWriter(test_table, &error);
  vector<KeyValuePair> result;
  SzlEmitter* test_emitter =
    new SzlEmitterTestEmitter("UnitTest", test_wr, &result);

  // Data must be passed in in the correct format. This tests adding floats
  // to the emitter.
  SignalEmitElement(test_emitter);
  test_emitter->PutFloat(kFloat1);
  SignalEndElement(test_emitter);

  SignalEmitElement(test_emitter);
  test_emitter->PutFloat(kFloat2);
  SignalEndElement(test_emitter);

  SignalEmitElement(test_emitter);
  test_emitter->PutFloat(kFloat3);
  SignalEndElement(test_emitter);

  // Retrieve the data and check it for correctness.
  string encoded_data1, encoded_data2, encoded_data3;

  SzlEncoder enc;
  enc.Reset();
  enc.PutFloat(kFloat1);
  encoded_data1 = enc.data();

  enc.Reset();
  enc.PutFloat(kFloat2);
  encoded_data2 = enc.data();

  enc.Reset();
  enc.PutFloat(kFloat3);
  encoded_data3 = enc.data();

  test_emitter->Flusher();
  vector<KeyMergedPair> kmp = ParseMergedResult(&result);
  CHECK_EQ(kmp.size(), 1) << "Incorrect number of indices.";
  vector<string>& values = kmp[0].second;
  CHECK_EQ(values.size(), 3) << "Incorrect number of values.";

  for (int i = 0; i < values.size(); i++) {
    if (values[i] == encoded_data1) {
      found1 = true;
    } else if (values[i] == encoded_data2) {
      found2 = true;
    } else if (values[i] == encoded_data3) {
      found3 = true;
    } else {
      fputs("Found a result that should not have existed in the emitter.",
            stderr);
      fail = true;
    }
  }
  delete test_emitter;
  // The SzlEmitter destructor deletes the writer.
  CheckForFinds(found1, found2, found3);
}


void SzlEmitterTest::AddsFingerprintsCorrectly() {
  SzlField element("", SzlType::kFingerprint);
  test_table.set_element(&element);
  SzlTabWriter* test_wr = SzlTabWriter::CreateSzlTabWriter(test_table, &error);
  vector<KeyValuePair> result;
  SzlEmitter* test_emitter =
    new SzlEmitterTestEmitter("UnitTest", test_wr, &result);

  // Data must be passed in in the correct format. This tests adding
  // fingerprints to the emitter.
  EmitThreeFingerprints(test_emitter);

  // Retrieve the data and check it for correctness.
  ValidateThreeFingerprints(test_emitter, &result);
  delete test_emitter;
  // The SzlEmitter destructor deletes the writer.
}


void SzlEmitterTest::AddsStringsCorrectly() {
  SzlField element("", SzlType::kString);
  test_table.set_element(&element);
  SzlTabWriter* test_wr = SzlTabWriter::CreateSzlTabWriter(test_table, &error);
  vector<KeyValuePair> result;
  SzlEmitter* test_emitter =
    new SzlEmitterTestEmitter("UnitTest", test_wr, &result);

  // Data must be passed in in the correct format. Test by adding three
  // different strings to the emitter.
  SignalEmitElement(test_emitter);
  test_emitter->PutString(data1.data(), data1.length());
  SignalEndElement(test_emitter);

  SignalEmitElement(test_emitter);
  test_emitter->PutString(data2.data(), data2.length());
  SignalEndElement(test_emitter);

  SignalEmitElement(test_emitter);
  test_emitter->PutString(data3.data(), data3.length());
  SignalEndElement(test_emitter);

  // Retrieve the data and check it for correctness.
  string encoded_data1, encoded_data2, encoded_data3;

  SzlEncoder enc;
  enc.Reset();
  enc.PutString(data1.data(), data1.length());
  encoded_data1 = enc.data();

  enc.Reset();
  enc.PutString(data2.data(), data2.length());
  encoded_data2 = enc.data();

  enc.Reset();
  enc.PutString(data3.data(), data3.length());
  encoded_data3 = enc.data();

  test_emitter->Flusher();
  vector<KeyMergedPair> kmp = ParseMergedResult(&result);
  CHECK_EQ(kmp.size(), 1) << "Incorrect number of indices.";
  vector<string>& values = kmp[0].second;
  CHECK_EQ(values.size(), 3) << "Incorrect number of values.";

  for (int i = 0; i < values.size(); i++) {
    if (values[i] == encoded_data1) {
      found1 = true;
    } else if (values[i] == encoded_data2) {
      found2 = true;
    } else if (values[i] == encoded_data3) {
      found3 = true;
    } else {
      fputs("Found a result that should not have existed in the emitter.",
            stderr);
      fail = true;
    }
  }
  delete test_emitter;
  // The SzlEmitter destructor deletes the writer.
  CheckForFinds(found1, found2, found3);
}


void SzlEmitterTest::AddsTimeCorrectly() {
  SzlField element("", SzlType::kTime);
  test_table.set_element(&element);
  SzlTabWriter* test_wr = SzlTabWriter::CreateSzlTabWriter(test_table, &error);
  vector<KeyValuePair> result;
  SzlEmitter* test_emitter =
    new SzlEmitterTestEmitter("UnitTest", test_wr, &result);

  // Data must be passed in in the correct format.
  // Add in 3 ints with 3 different indices first.
  SignalEmitElement(test_emitter);
  test_emitter->PutTime(kInt1);
  SignalEndElement(test_emitter);

  SignalEmitElement(test_emitter);
  test_emitter->PutTime(kInt2);
  SignalEndElement(test_emitter);

  SignalEmitElement(test_emitter);
  test_emitter->PutTime(kInt3);
  SignalEndElement(test_emitter);

  // Retrieve the data and check it for correctness.
  string encoded_data1, encoded_data2, encoded_data3;

  SzlEncoder enc;
  enc.Reset();
  enc.PutTime(kInt1);
  encoded_data1 = enc.data();

  enc.Reset();
  enc.PutTime(kInt2);
  encoded_data2 = enc.data();

  enc.Reset();
  enc.PutTime(kInt3);
  encoded_data3 = enc.data();

  test_emitter->Flusher();
  vector<KeyMergedPair> kmp = ParseMergedResult(&result);
  CHECK_EQ(kmp.size(), 1) << "Incorrect number of indices.";
  vector<string>& values = kmp[0].second;
  CHECK_EQ(values.size(), 3) << "Incorrect number of values.";

  for (int i = 0; i < values.size(); i++) {
    if (values[i] == encoded_data1) {
      found1 = true;
    } else if (values[i] == encoded_data2) {
      found2 = true;
    } else if (values[i] == encoded_data3) {
      found3 = true;
    } else {
      fputs("Found a result that should not have existed in the emitter.",
            stderr);
      fail = true;
    }
  }
  delete test_emitter;
  // The SzlEmitter destructor deletes the writer.
  CheckForFinds(found1, found2, found3);
}


void SzlEmitterTest::ClearsEmitterCorrectly() {
  SzlField element("", SzlType::kFingerprint);
  test_table.set_element(&element);
  SzlTabWriter* test_wr = SzlTabWriter::CreateSzlTabWriter(test_table, &error);
  vector<KeyValuePair> result;
  SzlEmitter* test_emitter =
    new SzlEmitterTestEmitter("UnitTest", test_wr, &result);

  EmitThreeFingerprints(test_emitter);
  test_emitter->Clear();
  test_emitter->Flusher();

  // First check is that the emitter is empty after Clear.
  CHECK_EQ(0, result.size()) << "Emitter was not correctly cleared";

  // Second check is that the emitter still accepts new inputs after being
  // cleared.
  EmitThreeFingerprints(test_emitter);
  ValidateThreeFingerprints(test_emitter, &result);

  delete test_emitter;
  // The SzlEmitter destructor deletes the writer.
}


int main(int argc, char** argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  SzlEmitterTest().KeepsStateCorrectly();
  SzlEmitterTest().AddsIntsCorrectly();
  SzlEmitterTest().AddsBoolsCorrectly();
  SzlEmitterTest().AddsBytesCorrectly();
  SzlEmitterTest().AddsFloatsCorrectly();
  SzlEmitterTest().AddsFingerprintsCorrectly();
  SzlEmitterTest().AddsStringsCorrectly();
  SzlEmitterTest().AddsTimeCorrectly();
  SzlEmitterTest().ClearsEmitterCorrectly();

  puts(fail ? "FAIL" : "PASS");
  return 0;
}
