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

#include <string>

#include "public/porting.h"
#include "public/logging.h"
#include "public/recordio.h"

#include "utilities/strutils.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szltabentry.h"
#include "public/szlresults.h"


// Test filtering the value; since recordio doesn't support keys,
// we check to make sure the output key is empty.
static void TestFilter(const SzlTabWriter* wr, const string& value) {
  string fk = "junk";
  uint64 shardfp;
  wr->FilterKey("there shouldn't be a key", &fk, &shardfp);
  CHECK_EQ(fk, "");

  SzlEncoder enc;
  string v;
  enc.Reset();
  enc.PutString(value.c_str());
  enc.Swap(&v);
  string fv = "junk";
  wr->FilterValue(v, &fv);
  CHECK_EQ(fv, value);
}

// Writes some output to wr, deletes it, and checks that the recordio output
// has the expected values.  Returns the size of the recordio file.
static void TestRecordioOutput(SzlTabWriter* wr) {
  // Make an entry for write values.
  SzlTabEntry* e = wr->CreateEntry("");
  CHECK(e != NULL);

  // Set up for the recordio builder.
  const char* tmpdir = getenv("SZL_TMP");
  if (tmpdir == NULL)
    tmpdir = "/tmp";
  string filename = StringPrintf("%s/recordio.test.%d", tmpdir, wr->param());
  wr->CreateOutput(filename);

  // 'Write' some values; this should end up writing to a recordio.
  e->Write("xyzzy");
  e->Write("foobar");
  e->Write("another test");

  // Add a very compressible value.
  string a4k(4096, 'a');
  e->Write(a4k);

  // Deleting the recordio writer completes the recordio build.
  delete e;
  delete wr;

  // Check that the recordio has the entries we expect.
  sawzall::RecordReader *reader = sawzall::RecordReader::Open(filename.c_str());
  CHECK(reader != NULL);
  char* record_ptr;
  size_t record_size;
  CHECK(reader->Read(&record_ptr, &record_size));
  CHECK_EQ(string(record_ptr, record_size), string("xyzzy"));

  CHECK(reader->Read(&record_ptr, &record_size));
  CHECK_EQ(string(record_ptr, record_size), string("foobar"));

  CHECK(reader->Read(&record_ptr, &record_size));
  CHECK_EQ(string(record_ptr, record_size), string("another test"));

  CHECK(reader->Read(&record_ptr, &record_size));
  CHECK_EQ(string(record_ptr, record_size), a4k);

  CHECK(!reader->Read(&record_ptr, &record_size));
  delete reader;
}

// Basic recordio output test.
static void TestSzlRecordio() {
  // out testing type: recordio(0) of goo: string
  SzlType tabty(SzlType::TABLE);
  tabty.set_table("recordio");
  SzlField tabtyelem("goo", SzlType::kString);
  tabty.set_element(&tabtyelem);
  tabty.set_param(0);
  string error;
  CHECK(tabty.Valid(&error)) << ": " << error;

  // create a table.
  SzlTabWriter* tabwr = SzlTabWriter::CreateSzlTabWriter(tabty, &error);
  CHECK(tabwr != NULL) << ": " << error;

  // Check that the table filters and doesn't write to a mill.
  CHECK(tabwr->Filters());
  CHECK(!tabwr->Aggregates());
  CHECK(!tabwr->WritesToMill());

  // Filter some values; this is what happens during emits.
  TestFilter(tabwr, "blah blah blah");
  TestFilter(tabwr, "");
  TestFilter(tabwr, "another simple test string");

  // Test output.
  TestRecordioOutput(tabwr);
}

int main(int argc, char* argv[]) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();

  TestSzlRecordio();

  printf("PASS\n");

  return 0;
}
