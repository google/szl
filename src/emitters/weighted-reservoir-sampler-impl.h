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

#include <string.h>

class RandomBase;

// This class manages the keys that are closely related to the weights of the
// samples. Given a sample weight w_i, key_i = log(u_i) / w_i, where u_i is
// uniformly distributed in [0, 1]. WRS Algorithm simply keeps the samples
// with n largest keys. These keys can be accessed publicly, for map-reduction.
// Note that in [1] (see weighted-reservoir-sampler.h for the reference),
// key i is defined as -log(u_i) / w_i, and the maximum key is replaced each
// time when a sample is replaced; here we save the negation operator in key
// evaluation and other computations, and we replace the minimum key instead.
class WRSAlgorithm {
 public:
  // unused_rnd is defined for a uniform interface with its subclasses,
  // which is necessary for WRSBase.
  explicit WRSAlgorithm(int max_sample_size, RandomBase* unused_rnd = NULL) :
      keys_(new double[max_sample_size]),
      key_index_heap_(new int[max_sample_size]),
      max_sample_size_(max_sample_size) {
    clear();
  }
  ~WRSAlgorithm() {
    delete [] keys_;
    delete [] key_index_heap_;
  }
  int max_sample_size() const {
    return max_sample_size_;
  }
  int current_sample_size() const {
    return current_sample_size_;
  }
  double key(int sample_index) const {
    DCHECK_GE(sample_index, 0);
    DCHECK_LT(sample_index, max_sample_size());
    return keys_[sample_index];
  }
  int ConsiderKey(double key);

  // This method requires current_sample_size() < max_sample_size()
  int AddKey(double key);

  // These 2 methods require current_sample_size() == max_sample_size()
  double min_key() const {
    DCHECK_EQ(current_sample_size(), max_sample_size());
    return keys_[key_index_heap_[0]];
  }
  int ReplaceMinKey(double key);
  void clear() {
    current_sample_size_ = 0;
    memset(keys_, 0, sizeof(keys_[0]) * max_sample_size_);
    memset(key_index_heap_, 0, sizeof(key_index_heap_[0]) * max_sample_size_);
  }
  int extra_memory() const {
    return (sizeof(keys_[0]) + sizeof(key_index_heap_[0])) * max_sample_size_;
  }

 private:
  // When current_sample_size_ reaches max_sample_size_,
  // keys_[key_index_heap_[0..max_sample_size_]] will be maintained as a heap
  // (min first).
  double* keys_;
  int* key_index_heap_;
  int max_sample_size_;
  int current_sample_size_;
};

class SimpleWRSAlgorithm : public WRSAlgorithm {
 public:
  SimpleWRSAlgorithm(int max_sample_size, RandomBase* rnd) :
      WRSAlgorithm(max_sample_size), rnd_(rnd) {
    CHECK(rnd != NULL);
  }
  ~SimpleWRSAlgorithm() {
  }
  RandomBase* rnd() const {
    return rnd_;
  }
  int ConsiderWeight(double weight);

 private:
  RandomBase* rnd_;
};

class FastWRSAlgorithm : public SimpleWRSAlgorithm {
 public:
  FastWRSAlgorithm(int max_sample_size, RandomBase* rnd) :
      SimpleWRSAlgorithm(max_sample_size, rnd),
      sum_skipped_weights_(0.0),
      sum_skipped_weights_threshold_(0.0) {
  }
  ~FastWRSAlgorithm() {
  }
  int ConsiderWeight(double weight);

 private:
  inline void ResetThreshold();
  // NOTE: using float would take approximately 50% more time in the unit
  // tests, possibly due to internal casting between float and double.
  double sum_skipped_weights_;
  double sum_skipped_weights_threshold_;
};
