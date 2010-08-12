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

// The notation v is borrowed from Fig.1 - 2 of [1] (see
// weighted-reservoir-sampler.h for the reference [1]).

#include <math.h>
#include <algorithm>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/random_base.h"

#include "emitters/weighted-reservoir-sampler-impl.h"


// Note: the original version of this code used a custom fast version
// of the "log()" function.  The impact of using the standard version
// is unknown, nor is the effect on the issue regarding the accuracy
// of "exp()" given in the comments below.


class KeyIndexLess {
 public:
  explicit KeyIndexLess(const double* keys) : keys_(keys) {
  }
  KeyIndexLess(const KeyIndexLess& another) : keys_(another.keys_) {
  }
  bool operator() (int index1, int index2) const {
    return keys_[index1] > keys_[index2];
  }
 private:
  const double* keys_;
};

int WRSAlgorithm::ReplaceMinKey(double key) {
  DCHECK_EQ(current_sample_size(), max_sample_size());
  KeyIndexLess op(keys_);
  int* begin = key_index_heap_;
  int* end = begin + max_sample_size();
  pop_heap(begin, end, op);
  int sample_index = end[-1];
  keys_[sample_index] = key;
  push_heap(begin, end, op);
  return sample_index;
}

int WRSAlgorithm::AddKey(double key) {
  DCHECK_LT(current_sample_size(), max_sample_size());
  int sample_index = current_sample_size();
  keys_[sample_index] = key;
  key_index_heap_[sample_index] = sample_index;
  if (++current_sample_size_ == max_sample_size()) {
    int* begin = key_index_heap_;
    int* end = begin + max_sample_size();
    make_heap(begin, end, KeyIndexLess(keys_));
  }
  return sample_index;
}

int WRSAlgorithm::ConsiderKey(double key) {
  if (current_sample_size() < max_sample_size())
    return AddKey(key);
  if (min_key() < key)
    return ReplaceMinKey(key);
  return -1;
}

int SimpleWRSAlgorithm::ConsiderWeight(double weight) {
  return ConsiderKey(log(rnd()->RandDouble()) / weight);
}

int FastWRSAlgorithm::ConsiderWeight(double weight) {
  if (current_sample_size() < max_sample_size()) {
    int sample_index = AddKey(log(rnd()->RandDouble()) / weight);
    if (current_sample_size() == max_sample_size())
      ResetThreshold();
    return sample_index;
  }
  sum_skipped_weights_ += weight;
  if (sum_skipped_weights_ <= sum_skipped_weights_threshold_)
    return -1;
  const double r = min_key();
  // NOTE: exp(x) tends to yield huge errors when abs(x) is
  // great. The output value could be as high as 1e30 while the expected
  // value is < 1. The alternative "1.0 / exp(-x)" does not help.
  DCHECK_LE(r, 0.0);
  const double t = exp(weight * r);
  DCHECK_LE(t, 1.0);
  DCHECK_GE(t, 0.0);
  // Input of log is a uniform random variable in [t, 1].
  const double v = log(1.0 - rnd()->RandDouble() * (1.0 - t)) / weight;
  DCHECK_LE(v, 0.0);
  int sample_index = ReplaceMinKey(v);
  sum_skipped_weights_ = 0.0;
  ResetThreshold();
  return sample_index;
}

inline void FastWRSAlgorithm::ResetThreshold() {
  const double r = min_key();
  DCHECK_LE(r, 0.0);
  sum_skipped_weights_threshold_ = log(rnd()->RandDouble()) / r;
  // When all weights in the samples are +inf, then r = -0.0 and
  // sum_skipped_weights_threshold_ becomes +inf, blocking all future data
  DCHECK_GT(sum_skipped_weights_threshold_, 0.0);
}
