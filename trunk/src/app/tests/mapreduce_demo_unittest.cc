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

// A demo program to illustrate usage of Sawzall in a map-reduce context.
// The built-in SzlEmitter class is used (see eval_demo_unittest for examples
// of using a custom emitter) with the help of methods in that class supplied
// to assist with map-reduce.


#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <map>
#include <algorithm>

#include "public/porting.h"
#include "public/commandlineflags.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "public/sawzall.h"
#include "public/szltype.h"
#include "public/szltabentry.h"
#include "public/emitterinterface.h"
#include "public/szlemitter.h"
#include "public/szldecoder.h"
#include "public/szlresults.h"
#include "public/emitterinterface.h"


typedef SzlEmitter::KeyValuePair KeyValuePair;
typedef pair<string, vector<string> > KeyMergedPair;

const char kSzlKeyValueSep[] = ":";


// Test table type - file scope for debugging.  Initialized in main().
static SzlType test_table_type(SzlType::kInt);


// In a large-scale map-reduce the combined outputs of all mappers for
// a given mapper output shard would be written to a file (probably on
// a different machine).
// In this code we simulate each file as a MapOutputShard.
typedef vector<KeyValuePair> MapOutputShard;

// In a large-scale mapreduce each mapper output shards would be sorted
// by key and all values for a given key would be collected together.
// In this code we simulate the result of that process as a ReduceInputShard.
typedef vector<KeyMergedPair> ReduceInputShard;

// For consistency we name the reducer outputs as well.
typedef vector<KeyValuePair> ReducerOutput;


// -----------------------------------------------------------------------------

// Sample Sawzall program to aggregate elements of a set.
// Since we are testing mapreduce logic, not aggragation,
// we set the size large enough to save all members of the set.
// Reads strings (as if lines from a file) in the form: index,value

const int kSetParam = 100;   // hard-coded in program below
const int kNumValues = 100;
const int kMinValue = 0;
const int kMaxValue = 50;  // ensure some duplication
const int kMinIndex = 0;
const int kMaxIndex = 7;
const int kInvalidIndex = kMinIndex - 1;


string program =
    "t: table set(100)[int] of int;"
    "fields: array of bytes = splitcsvline(input);"
    "index: int = int(string(fields[0]),10);"
    "value: int = int(string(fields[1]),10);"
    "emit t[index] <- value;";


struct IndexValue {
  int index;
  int value;
};


static vector<IndexValue> CreateInput() {
  vector<IndexValue> result;
  for (int i = 0; i < kNumValues; i++) {
    IndexValue iv = {
      random() % (kMaxIndex - kMinIndex + 1) + kMinIndex,
      random() % (kMaxValue - kMinValue + 1) + kMinValue
    };
    result.push_back(iv);
  }
  return result;
}


static vector<string> FormatInput(vector<IndexValue>& input) {
  vector<string> result;
  for (int i = 0; i < input.size(); i++) {
    char line[20];
    sprintf(line, "%d,%d", input[i].index, input[i].value);
    result.push_back(line);
    VLOG(2) << "input: " << result.back();
  }
  return result;
}


// -----------------------------------------------------------------------------

// Override WriteValue() to write mapper results.

class MapreduceDemoEmitter : public SzlEmitter {
public:
  MapreduceDemoEmitter(const string& name, const SzlTabWriter* writer,
                       vector<MapOutputShard>* result, int num_shards)
    : SzlEmitter(name, writer, false),
      result_(result), num_shards_(num_shards),
      prefix_(name +  kSzlKeyValueSep) { }

private:
  const SzlTabWriter* writer() { return writer_; }
  virtual void WriteValue(const string& key, const string& value);

  vector<MapOutputShard>* result_;
  int num_shards_;
  string prefix_;
};


void MapreduceDemoEmitter::WriteValue(const string& key, const string& value) {
  // Note that we prepend the table name before computing the shard;
  // this gives better distribution when there are many values with the
  // same key for different tables.

  // At this point we could have an associated non-mill output table
  // (e.g. text or recordio), and set output_table to that.
  const SzlTabWriter* output_table = writer_;

  int shard;
  if (output_table->Filters()) {
    string fkey;
    Fprint shardfp;
    output_table->FilterKey(key, &fkey, &shardfp);

    // For non-aggregating tables with no indices, shard semi-uniformly.
    if (output_table->HasIndices()) {
      shard = shardfp % num_shards_;
    } else {
      static int counter = 0;
      shard = (counter++) % num_shards_;
    }
    (*result_)[shard].push_back(KeyValuePair(prefix_ + fkey, value));
  } else if (output_table->IsMrCounter()) {
    SzlDecoder dec(value.data(), value.size());
    int64 i = 0;
    CHECK(dec.GetInt(&i)) << "mrcounter expected an int";
    // If we were tracking a counter, would increment it here.
    //counter_->IncrementBy(i);
    return;
  } else if (!output_table->Aggregates() && !output_table->HasIndices()) {
    // For non-aggregating tables with no indices, shard semi-uniformly.
    // Tables with indices are always sent to the same shard so that
    // resaw-merge can put them back together.
    static int counter = 0;
    shard = (counter++) % num_shards_;
    (*result_)[shard].push_back(KeyValuePair(prefix_ + key, value));
  } else {
    // Compute the shard from the key.
    shard = FingerprintString(key.data(), key.size()) % num_shards_;
    (*result_)[shard].push_back(KeyValuePair(prefix_ + key, value));
  }
  if (FLAGS_v >= 2) {
    LOG(INFO) << "Map output to shard " << shard;
    SzlDecoder dec(key.data(), key.size());
    int64 num_key;
    CHECK(dec.GetInt(&num_key));
    LOG(INFO) << "  key: " << num_key;
    string error;
    SzlResults* sres = SzlResults::CreateSzlResults(test_table_type, &error);
    CHECK(sres != NULL);
    CHECK(sres->ParseFromString(value));
    const vector<string>* results = sres->Results();
    for (int i = 0; i < results->size(); i++) {
      SzlDecoder dec((*results)[i].data(), (*results)[i].size());
      int64 num_value;
      CHECK(dec.GetInt(&num_value));
      LOG(INFO) << "    value: " << num_value;
    }
  }
}


class DemoEmitterFactory : public sawzall::EmitterFactory {
 public:
  DemoEmitterFactory(vector<MapOutputShard>* result, int num_shards)
    : result_(result), num_shards_(num_shards) { }
  ~DemoEmitterFactory() {
    // Explicitly deallocate each emitter
    for (int i = 0; i < emitters_.size(); i++) {
      sawzall::Emitter* e = emitters_[i];
      delete e;
    }
  }

  // Returns a new emitter.
  sawzall::Emitter* NewEmitter(sawzall::TableInfo* table_info, string* error) {
    const char* name = table_info->name();
    SzlEmitter* emitter = NULL;
    SzlType szl_type(SzlType::VOID);
    if (szl_type.ParseFromSzlArray(table_info->type_string().data(),
                                   table_info->type_string().size(),
                                   error)) {
      SzlTabWriter* tab_writer = SzlTabWriter::CreateSzlTabWriter(szl_type,
                                                                  error);
      if (tab_writer != NULL)
        emitter = new MapreduceDemoEmitter(name, tab_writer,
                                           result_, num_shards_);
    }
    emitters_.push_back(emitter);
    return emitter;
  }

  // Return the emitters.
  vector<SzlEmitter*>& emitters() { return emitters_; }

 private:
  vector<SzlEmitter*> emitters_;
  vector<MapOutputShard>* result_;
  int num_shards_;
};


// -----------------------------------------------------------------------------

// The mapper executes the Sawzall program with a supplied subset
// of the overall input and writes the output to multiple strings, appending
// to the results of previous mappers.  In a real map-reduce the mappers
// would execute in parallel (possibly on different machines) and the
// map-reduce system would combine corresponding shards of the output
// from each mapper.
// Note that the mapper appends its results to the existing output shards.

void Mapper(const string& program_name, const string& source,
            const string* input, int num_input_lines,
            vector<MapOutputShard>* result, int num_shards) {
  // Compile the program
  sawzall::Executable exe(program_name.c_str(), source.c_str(),
                          sawzall::kNormal);
  if (!exe.is_executable())
    LOG(FATAL) << "could not compile " << program_name;
  // Create a Sawzall process and register all the emitters
  sawzall::Process process(&exe, false, NULL);
  DemoEmitterFactory emitter_factory(result, num_shards);
  process.set_emitter_factory(&emitter_factory);
  sawzall::RegisterEmitters(&process);

  // Run the program
  if (!process.Initialize())
    LOG(FATAL) << "could not initialize " << program_name;
  for (int i = 0; i < num_input_lines; i++) {
    // In a large map-reduce we could check whether the total memory
    // allocated by an emitter was getting large (using GetMemoryUsage()),
    // and do an intermediate call to Flusher() as needed.
    // An alternative is the cheaper but less reliable GetMemoryEstimate().
    if (!process.Run(input[i].data(), input[i].size(), NULL, 0))
      LOG(FATAL) << "could not successfully execute " << program_name;
  }

  // Flush the emitter output to the mapper output shards.
  for (int i = 0; i < emitter_factory.emitters().size(); i++) {
    SzlEmitter* emitter = emitter_factory.emitters()[i];
    emitter->Flusher();
  }
}


vector<MapOutputShard>
InvokeMappers(vector<string>& mapper_phase_input,
              int num_mappers, int num_shards) {
  // Create the mapper output shards, initially empty.
  vector<MapOutputShard> result;
  for (int i = 0; i < num_shards; i++)
    result.push_back(MapOutputShard());

  for (int i = 0; i < num_mappers; i++) {
    int start = i * mapper_phase_input.size() / num_mappers;
    int end = (i+1) * mapper_phase_input.size() / num_mappers;
    VLOG(2) << "Mapper " << i << " on input [" << start << "," << end << "]";
    // Each invocation of Mapper potentially appends to each shard in "result".
    Mapper("mapreduce_demo", program, &mapper_phase_input[start], (end-start),
           &result, num_shards);
  }
  return result;
}


// -----------------------------------------------------------------------------


// None of the processing after the mappers requires executing Sawzall code
// and so it all could be implemented in a separate program that does not
// use the Sawzall compiler and runtime.  Or it could be in the same program
// which at some times is run for mapping and at others for reducing.
// In this demo we use the same program but avoid using any support from
// the compilation and execution code.

// After the mappers run and before the reducers run, each shard output
// by the mapper is sorted and all of the values for a given key are
// collected together with that key.

vector<ReduceInputShard>
IntermediateSort(vector<MapOutputShard>& mapper_outputs) {
  vector<ReduceInputShard> result;

  for (int i = 0; i < mapper_outputs.size(); i++) {
    MapOutputShard& mapout = mapper_outputs[i];
    result.push_back(ReduceInputShard());
    ReduceInputShard& reducein = result.back();
    sort(mapout.begin(), mapout.end());
    KeyMergedPair* kmp = NULL;
    for (int j = 0; j < mapout.size(); j++) {
      KeyValuePair* kvp = &mapout[j];
      if (kmp == NULL || kvp->first != kmp->first) {
        reducein.push_back(KeyMergedPair());
        kmp = &reducein.back();
        kmp->first = kvp->first;
      }
      kmp->second.push_back(kvp->second);
    }
  }
  return result;
}



// -----------------------------------------------------------------------------

// In the reduce phase, for each key all the values with that key,
// from all mappers, are combined to form a single result.

// This function proceses a single shard of mapper output after it has been
// sorted and the values associated with each key have been collected together.
// In a real map-reduce the reducers would run in parallel.

ReducerOutput
Reducer(ReduceInputShard& reducer_input,
        map<string,SzlTabWriter*>& tabwriters) {
  ReducerOutput result;
  // The table name is prefixed to the actual key.
  for (int i = 0; i < reducer_input.size(); i++) {
    const string& name_key = reducer_input[i].first;
    const vector<string>& values = reducer_input[i].second;
    size_t separator_index = name_key.find(kSzlKeyValueSep);
    CHECK(separator_index != name_key.npos) << ": internal error";
    string name = name_key.substr(0,separator_index);
    string key = name_key.substr(separator_index+1);

    map<string,SzlTabWriter*>::iterator it = tabwriters.find(name);
    CHECK(it != tabwriters.end()) << ": internal error";
    const SzlTabWriter* tw = it->second;
  
    // Create the tabwriter and tabentry for the key & value.
    SzlTabEntry* te = tw->CreateEntry(key);
    CHECK(te != NULL) << ": out of memory creating table entry for " << name; 
  
    // We either need to merge all values for aggregating tables,
    // or pass through all values for other tables.
    if (tw->Aggregates()) {
      // For aggregating tables, first merge the values.
      for (int j = 0; j < values.size(); j++) {
        SzlTabEntry::MergeStatus status = te->Merge(values[j]);
        if (status == SzlTabEntry::MergeError)
          LOG(FATAL) << "error merging results";
      }
      string value;
      te->Flush(&value);
  
      // Write the output to the mill
      result.push_back(KeyValuePair(name_key,value));
    } else {
      // Non-aggregating tables.
      if (tw->WritesToMill()) {
        // Just write the value directly into the mill.
        for (int j = 0; j < values.size(); j++) {
          result.push_back(KeyValuePair(name_key,values[j]));
        }
      } else {
        // Direct output table - let the table write the value.
        for (int j = 0; j < values.size(); j++) {
          te->Write(values[j]);
        }
      }
    }
    delete te;
  }
  return result;
}


vector<ReducerOutput>
InvokeReducers(vector<ReduceInputShard>& reducer_phase_input,
               map<string,SzlTabWriter*>& tabwriters) {
  vector<ReducerOutput> result;
  for (int i = 0; i < reducer_phase_input.size(); i++)
    result.push_back(Reducer(reducer_phase_input[i], tabwriters));
  return result;
}



// -----------------------------------------------------------------------------

SzlType CreateSetTableType(const SzlType& index_type, const SzlType& value_type,
                           int param) {
  SzlType t(SzlType::TABLE);
  t.set_table("set");
  SzlField value_field("", value_type);
  t.set_element(&value_field);
  t.set_param(param);
  t.AddIndex("", index_type);
  string error;
  CHECK(t.Valid(&error)) << ": " << error;
  return t;
}


map<string,SzlTabWriter*>
CreateTabWriters(vector<pair<string,SzlType> > table_types) {
  map<string,SzlTabWriter*> result;
  for (int i = 0; i < table_types.size(); i++) {
    string& name = table_types[i].first;
    SzlType& t = table_types[i].second;
    string error;
    SzlTabWriter* swr = SzlTabWriter::CreateSzlTabWriter(t, &error);
    CHECK(swr != NULL) << ": " << error;
    result[name] = swr;
  }
  return result;
}


// -----------------------------------------------------------------------------

void CheckResult(vector<IndexValue>& input,
                 vector<ReducerOutput>& reducer_outputs,
                 SzlType& t) {

  // Parse and check the results.
  // Verify by removing all matching (index,value) pairs from the input.
  // If any pairs are not found, or any remain at the end, the output is wrong.

  string error;
  SzlResults* sres = SzlResults::CreateSzlResults(t, &error);
  CHECK(sres != NULL) << ": " << error;

  for (int i = 0; i < reducer_outputs.size(); i++) {
    VLOG(2) << "Reducer " << i << " output";
    for (int j = 0; j < reducer_outputs[i].size(); j++) {
      KeyValuePair& kvp = reducer_outputs[i][j];
      string& name_key = kvp.first;
      size_t separator_index = name_key.find(kSzlKeyValueSep);
      CHECK(separator_index != name_key.npos) << ": internal error";
      string name = name_key.substr(0,separator_index);
      CHECK(name == "t");
      string key = name_key.substr(separator_index+1);
      SzlDecoder dec(key.data(), key.size());
      int64 int_key;
      CHECK(dec.GetInt(&int_key));
      CHECK(sres->ParseFromString(kvp.second));
      const vector<string>* results = sres->Results();
      VLOG(2) << "   index: " << int_key;
      for (int k = 0; k < results->size(); k++) {
        SzlDecoder dec((*results)[k].data(), (*results)[k].size());
        int64 int_value;
        CHECK(dec.GetInt(&int_value));
        VLOG(2) << "      value: " << int_value;
        // Remove all instances of (key,value) from the input vector.
        bool found = false;
        for (int k = 0; k < input.size(); k++) {
          if (input[k].index == int_key && input[k].value == int_value) {
            input[k].index = kInvalidIndex;
            found = true;
          }
        }
        CHECK(found);
      }
    }
  }
  for (int i = 0; i < input.size(); i++)
    CHECK_EQ(input[i].index, kInvalidIndex);
}


// -----------------------------------------------------------------------------


int main(int argc, char **argv) {
  ProcessCommandLineArguments(argc, argv);
  InitializeAllModules();
  sawzall::RegisterStandardTableTypes();

  // Generate random input.
  vector<IndexValue> input = CreateInput();
  vector<string> mapper_phase_input = FormatInput(input);

  // Define the number of mappers and reducers.
  const int kNumMappers = 3;
  const int kNumReducers = 4;

  // Create the top table type.  In the general case the table type
  // should be serialized (encoded) and passed with the data.
  test_table_type = CreateSetTableType(SzlType::kInt, SzlType::kInt, kSetParam);

  // Run the mappers, creating map output shards: one per reducer.
  vector<MapOutputShard> mapper_outputs =
    InvokeMappers(mapper_phase_input, kNumMappers, kNumReducers); 

  // Sort each shard and group all the values for a given key together.
  vector<ReduceInputShard> reducer_inputs = IntermediateSort(mapper_outputs);

  // Create a tabwriter map so the reducer can find the right tabwriter.
  vector<pair<string,SzlType> > table_types;
  table_types.push_back(pair<string,SzlType>("t",test_table_type));
  map<string,SzlTabWriter*> tabwriters = CreateTabWriters(table_types);

  // Run the reducers.
  vector<ReducerOutput> reducer_outputs =
      InvokeReducers(reducer_inputs, tabwriters);

  // Get the expected output and compare.
  CheckResult(input, reducer_outputs, test_table_type);

  printf("PASS\n");
  return 0;
}
