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

// Structure for estimating of weights of elements in a sequence,
// without actually storing the elements.
// based on the CountSzlSketch algorithm from
// "Finding Frequent Items in Data Streams",
//   Moses Charikar, Kevin Chen, and Martin Farach-Colton.

class SzlSketch {
 public:
  // Bounds on the number of tables we create.
  // Always create an odd number of tables so we can compute the median.
  static const int kMinTabs = 15;
  static const int kMaxTabs = 31;

  typedef struct {
    struct {
      unsigned elem;
      unsigned sign;
    } index[kMaxTabs];
  } Index;

  // Return the dimensions of a sketch with approximately totalSize entries.
  static void Dims(int totalSize, int* nTabs, int* tabSize);

  // Build a new sketch with a given table dimensions,
  // which must have been computed by Dims.
  SzlSketch(const SzlOps& weight_ops, int nTabs, int tabSize);

  ~SzlSketch();

  // Compute the index into the sketch for a string.
  void ComputeIndex(const string& s, Index* index);

  // Adjust the estimated weight for an index.
  void AddSub(Index* index, const SzlValue& value, int isAdd);

  // Compute the estimated weight for an index.
  void Estimate(Index* index, SzlValue* est);

  // Compute the estimated standard deviation of values in the sketch.
  void StdDeviation(double* deviations);

  // Add another sketch's weight into this sketch.
  void AddSketch(const SzlSketch& sketch);

  // Encode a sketch.
  void Encode(SzlEncoder* enc);

  // Parse a pickled sketch.
  bool Decode(SzlDecoder* dec);

  // Return the number of tables in the sketch.
  int nTabs() const { return nTabs_; }

  // Return the size of each table in the sketch.
  int tabSize() const { return tabSize_; }

  // Estimate memory currently allocated.
  int Memory();

 private:
  // Operations on our weights.
  const SzlOps& weight_ops_;

  // storage of the sketch.
  SzlValue* weights_;           // 2d array of weights[nTabs][tabsize]
  SzlValue tmp_[kMaxTabs];      // temporary computation array
  int nTabs_;                   // kMinTabs <= nTabs <= kMaxTabs
  int tabSize_;                 // must be pow(2)
  int tabBits_;                 // log2(tabSize_) == tabBits_
};
