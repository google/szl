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

// A library for weighted reservoir sampling (WRS) without replacement.
//
// Weighted reservoir sampling without replacement is the sampling process
// that has EQUIVALENT results as the following:
//   Input: population { x_i with weight w_i > 0 } of size N,
//          and max sample size n.
//   Repeat min(n, N) times:
//     sampling one datum from the population, with
//       Probability(x_i is sampled) = w_i / (sum of w_i among population),
//     and then removing the sampled datum from the population.
// Note that in general,
//   Probability(x_i is sampled in the above steps) != n * w_i / sum(w_i)
// But the equality holds true when all w_i's are the same, and approximately
// true when n * max(w_i) << sum(w_i) (which implies that n << N and that no
// w_i is dominant).
//
// In this library, the algorithms WRS (precise mode) and WRS-FF (fast-forward
// mode) proposed in [1] are implemented as SimpleWRS and FastWRS, respectively.
// They can handle a population of unknown size with one pass. WRS-FF is a
// fast-forward variant of WRS with data skipping simulation; it is an
// approximation of weighted reservoir sampling with significantly fewer random
// number generations (run weighted-reservoir-sampler_test --gunit_print_time to
// see the difference). The speed improvement, however, depends on the max
// sample size n, the population size N, and many other factors, and thus may
// not be worth the loss in accuracy.
//
// The specifications are listed below.
//   Requirements of sample type T:
//     Default constructor T::T() is public, and one of the following is true,
//     depending on the template argument SampleTraits.
//     a. T has a public assignment operator T::operator=(const T&),
//        if SampleTraits = AssignableSampleTraits<T> (default);
//     b. swap(T&, T&) is specialized, if SampleTraits =
//        SwappableSampleTraits<T>;
//     c. T is a protocol buffer, if SampleTraits = SwappableProtoTraits<T>;
//     d. SampleTraits is user defined.
//   Inputs:
//     max sample size n (must be positive; checked at initialization);
//     w_i, x_i (one pair at a time; ignored if w_i <= 0 or = NaN).
//   Output:
//     min(n, N) samples, where N is the number of input (x_i, w_i) pairs,
//       excluding those with w_i <= 0 or = NaN.
//   Performance:
//     Space complexity: O(n);
//     Worst case time complexity: O(N log(n));
//     Expected time complexity: O(N + n log(n) log(N / n));
//     Number of random variable generations:
//       For SimpleWRS: N.
//       For FastWRS:
//         if N < n: N;
//         if N = n: n + 1;
//         if N > n: random; assuming w_i's are independent random variables
//           with a common continuous distribution, the expected value is
//           <= n + 1 + 2 n log(N / n); worst case: 2 N - n + 1.
//   Thread safety:
//     None. You must guard each mutation method with mutex for multi-threading.
//
// Example usages:
//   1) Basic usages:
//     1.1) Each datum is of a primitive type, a C pointer or a small struct:
//
//     MTRandom rnd;
//     SimpleWRS<int> sampler(max_sample_size, &rnd);
//     // Or: FastWRS<int> sampler(max_sample_size, &rnd);
//     for (...) {
//       int x_i = ...;
//       double w_i = ...;
//       sampler.ConsiderSample(w_i, x_i);
//     }
//     for (int i = 0; i < sampler.current_sample_size(); ++i) {
//       int sample = sampler.sample(i);
//       ...  // use sample
//     }
//
//     1.2) Each datum is an STL container, a string, a scoped_ptr, or of any
//          other class whose swapping operation is more efficient than copying.
//          std::swap() must be specialized, or the performance would be even
//          worse than copying. Note that currently std::swap is not specialized
//          for protocol buffers; you may use SwappableProtoTraits instead of
//          SwappableSampleTraits for protocol buffers.
//
//     SimpleWRS<string, SwappableSampleTraits<string> >
//         sampler(max_sample_size, &rnd);
//     for (...) {
//       string x_i = ...;
//       double w_i = ...;
//       sampler.ConsiderSample(w_i, &x_i);
//       // If x_i is chosen, it will be swapped with a previous sample, and
//       // here the previous sample is destructed when x_i goes out of scope.
//     }
//     for (int i = 0; i < sampler.current_sample_size(); ++i) {
//       const string& sample = sampler.sample(i);
//       ...  // use sample
//     }
//     // All chosen samples are destructed when sampler goes out of scope.
//
//   2) Optimization for speed when it is expensive to fully populate a datum:
//
//     SimpleWRS<int> sampler1(max_sample_size, &rnd);
//     for (...) {
//       double w_i = ...;
//       const int sample_index = sampler1.ConsiderSample(w_i, 0);
//       if (sample_index >= 0)
//         *sampler1.mutable_sample(sample_index) = ExpensiveAlgorithm(...);
//     }
//
//     SimpleWRS<string, SwappableSampleTraits<string> >
//         sampler2(max_sample_size, &rnd);
//     for (...) {
//       string dummy;  // not fully populated until selected
//       double w_i = ...;
//       const int sample_index = sampler2.ConsiderSample(w_i, &dummy);
//       if (sample_index >= 0) {
//         string* x_i = sampler2.mutable_sample(sample_index);
//         ... // Fully populate x_i.
//         // Now dummy is a discarded record and should be ignored.
//       }
//     }
//
// Reference:
// [1] Kolonko, M. and Wasch, D. 2006. Sequential reservoir sampling with
//     a nonuniform distribution. ACM Trans. Math. Softw. 32, 2 (Jun. 2006),
//     257-273. DOI= http://doi.acm.org/10.1145/1141885.1141891

#include <algorithm>

class RandomBase;


// A traits struct needs to define SrcFromPtr only when MergeFrom is called.

template <typename T>
struct AssignableSampleTraits {
  typedef const T& SrcType;
  static SrcType SrcFromPtr(const T* sample) {
    return *sample;
  }
  static void SetSample(SrcType src, T* dest) {
    *dest = src;
  }
};

template <typename T>
struct SwappableSampleTraits {
  typedef T* SrcType;
  static SrcType SrcFromPtr(T* sample) {
    return sample;
  }
  static void SetSample(SrcType src, T* dest) {
    swap(*dest, *src);
  }
};

// For protocol buffers, whose swap method is not specialized by default.
template <typename T>
struct SwappableProtoTraits {
  typedef T* SrcType;
  static SrcType SrcFromPtr(T* sample) {
    return sample;
  }
  static void SetSample(SrcType src, T* dest) {
    dest->Swap(src);
  }
};

// This class is the common base of SimpleWRS, FastWRS, and WRSMerger.
// It is made public only for documentation and code reuse.
// Users must use the above subclasses instead of this class directly.
//
// By default, the input weights are not stored. To have weights stored,
// wrap your data T and the weight into another struct. For example,
// you can use SimpleWRS<pair<T, double> >. It's your responsibility to set
// the weight element before calling ConsiderSample.
template <typename T, typename AlgorithmType, typename SampleTraits>
class WRSBase {
 public:
  // Returns the max_samples used to construct this instance. Never changes.
  int max_sample_size() const {
    return algorithm_.max_sample_size();
  }
  // Returns the current number of stored samples.
  int current_sample_size() const {
    return algorithm_.current_sample_size();
  }

  double key(int sample_index) const {
    DCHECK_GE(sample_index, 0);
    DCHECK_LT(sample_index, current_sample_size());
    return algorithm_.key(sample_index);
  }
  // NOTE: Do not assume that the samples are ordered by any criterion.
  const T& sample(int sample_index) const {
    DCHECK_GE(sample_index, 0);
    DCHECK_LT(sample_index, current_sample_size());
    return reservoir_[sample_index];
  }
  // NOTE: For external users, this method is typically used for conditional
  // initialization (see example usage 2) and for merging samples. The users
  // should be aware that each sample is associated with a constant key. For
  // example, if they pass a sample, mutable or not, to ConsiderSampledDatum,
  // they must also pass the corresponding key. If the users break the
  // correspondence, e.g., by swapping samples externally, then this sampler
  // instance must be quarantined, i.e., no more samples can be passed to this
  // sampler, and this sampler cannot be merged into another sampler instance.
  // Therefore, it would be better to sort an external array of T* pointers
  // or indexes than reordering the samples directly in a sampler instance.
  T* mutable_sample(int sample_index) {
    DCHECK_GE(sample_index, 0);
    DCHECK_LT(sample_index, current_sample_size());
    return &reservoir_[sample_index];
  }

  // Clears the samples, as if no sample had been passed to this sampler
  // instance. current_sample_size() will be 0 but max_sample_size() remains.
  void clear() {
    algorithm_.clear();
    delete [] reservoir_;
    reservoir_ = new T[max_sample_size()];
  }
  // Estimated memory in bytes used by the sampler, excluding sizeof(*this)
  // and the data pointed from the samples. Designed for applications where
  // memory usage is monitored.
  int extra_memory() const {
    return sizeof(T) * max_sample_size() + algorithm_.extra_memory();
  }

 protected:
  WRSBase(int max_sample_size, RandomBase* rnd) :
      algorithm_(max_sample_size, rnd), reservoir_(new T[max_sample_size]) {
  }
  ~WRSBase() {
    delete [] reservoir_;
  }
  // Visit a sample with a weight; the sample may or may not be inserted into
  // the reservoir. The sample must come directly from the population instead
  // of another sampler (if the sample has been selected by another sampler,
  // you must call ConsiderSampledDatum to merge it into "this"; otherwise the
  // result will be incorrect).
  //
  // If weight is <= 0 or = NaN, this sample is definitely not chosen;
  // else, if the current sample size is less than max sample size, this
  // sample is definitely chosen;
  // else, if weight is +infinity and not all chosen samples have
  // weight = +infinity, this sample is definitely chosen.
  // Even if the sample is chosen, future samples might replace it.
  //
  // Returns a negative value to indicate that the sample is not chosen, or
  // a non-negative value as the index in the reservoir where the sample is
  // stored.
  int ConsiderSample(double weight, typename SampleTraits::SrcType sample) {
    if (weight > 0) {
      int sample_index = algorithm_.ConsiderWeight(weight);
      if (sample_index >= 0)
        SampleTraits::SetSample(sample, mutable_sample(sample_index));
      return sample_index;
    }
    return -1;
  }
  // This method is provided for merging samples of different samplers, as if
  // all the population had been passed to this sampler. It may be useful
  // in map-reduction.
  //
  // The key must be the key (not weight) associated with the input sample,
  // both returned from a sampler with the same sample_index.
  //
  // Returns a negative value to indicate that the sample is not chosen, or
  // a non-negative value as the index in the reservoir where the sample is
  // stored.
  //
  // Note: This method's behavior is not random. Since each sampler simply keeps
  // the samples with n largest keys, the merger only needs to merge and find
  // the n largest keys again, without additional random sampling.
  int ConsiderSampledDatum(double key, typename SampleTraits::SrcType sample) {
    int sample_index = algorithm_.ConsiderKey(key);
    if (sample_index >= 0)
      SampleTraits::SetSample(sample, mutable_sample(sample_index));
    return sample_index;
  }
  // Merges samples with another, as if all samples had been passed to "this".
  // Clears another after done.
  // "this" and "another" must have the same max sample size for
  // result correctness.
  // AnotherWRS can be SimpleWRS, FastWRS, or WRSMerger, even with different T
  // and SampleTraits arguments, as long as the static method
  // "SampleTraits::SrcType SampleTraits::SrcFromPtr(AnotherWRS::T*)" is
  // defined for the template argument SampleTraits of "this".
  template <typename AnotherWRS>
  void MergeFrom(AnotherWRS* another) {
    CHECK_EQ(max_sample_size(), another->max_sample_size());
    for (int i = 0; i < another->current_sample_size(); ++i)
      ConsiderSampledDatum(
          another->key(i),
          SampleTraits::SrcFromPtr(another->mutable_sample(i)));
    another->clear();
  }

 private:
  AlgorithmType algorithm_;
  T* reservoir_;
};

template <typename T, typename SampleTraits = AssignableSampleTraits<T> >
class SimpleWRS : public WRSBase<T, SimpleWRSAlgorithm, SampleTraits> {
 public:
  // max_sample_size must be > 0.
  // rnd must not be NULL. This instance does not own rnd.
  // The period of rnd must be far greater than the population size N.
  SimpleWRS(int max_sample_size, RandomBase* rnd) : Base(max_sample_size, rnd) {
  }
  ~SimpleWRS() {
  }
  int ConsiderSample(double weight, typename SampleTraits::SrcType sample) {
    return Base::ConsiderSample(weight, sample);
  }
  int ConsiderSampledDatum(double key, typename SampleTraits::SrcType sample) {
    return Base::ConsiderSampledDatum(key, sample);
  }
  template <typename AnotherWRS>
  void MergeFrom(AnotherWRS* another) {
    Base::MergeFrom(another);
  }
 private:
  typedef WRSBase<T, SimpleWRSAlgorithm, SampleTraits> Base;
};

// "Fast" only means less random number generations. FastWRS is not a
// WRS algorithm in the strict sense but an approximation. If random number
// generation does not cost most of the execution time (such as in
// map-reduction), you should consider using SimpleWRS instead.
template <typename T, typename SampleTraits = AssignableSampleTraits<T> >
class FastWRS : public WRSBase<T, FastWRSAlgorithm, SampleTraits> {
 public:
  // max_sample_size must be > 0.
  // rnd must not be NULL. This instance does not own rnd.
  // The period of rnd must be far greater than the number of random variable
  // generations (see the specifications at the beginning of this file).
  FastWRS(int max_sample_size, RandomBase* rnd) : Base(max_sample_size, rnd) {
  }
  ~FastWRS() {
  }
  int ConsiderSample(double weight, typename SampleTraits::SrcType sample) {
    return Base::ConsiderSample(weight, sample);
  }
  // ConsiderSampledDatum and MergeFrom would break the integrity; not provided.
 private:
  typedef WRSBase<T, FastWRSAlgorithm, SampleTraits> Base;
};

// For merging without random number generators.
template <typename T, typename SampleTraits = AssignableSampleTraits<T> >
class WRSMerger : public WRSBase<T, WRSAlgorithm, SampleTraits> {
 public:
  // max_sample_size must be the same as that of the source sampler instance.
  explicit WRSMerger(int max_sample_size) : Base(max_sample_size, NULL) {
  }
  ~WRSMerger() {
  }
  int ConsiderSampledDatum(double key, typename SampleTraits::SrcType sample) {
    return Base::ConsiderSampledDatum(key, sample);
  }
  template <typename AnotherWRS>
  void MergeFrom(AnotherWRS* another) {
    Base::MergeFrom(another);
  }
 private:
  typedef WRSBase<T, WRSAlgorithm, SampleTraits> Base;
};
