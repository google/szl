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

// ACM minimal standard random number generator.
// 1. Thoroughly tested in the literature
// 2. Period of 2^31-1
// 3. Fast: about 60 cycles per value
// 4. Compact: 4 bytes of state
// 5. Multiple threads can use different instantiations without sync

// SzlACMRandom is a multiplicative generator and outputs A^k mod M,
// where A = 16807, M = 2^31-1.  A is a generator for Z_M^*.  So, (0)
// forms a 1-cycle (disallowed by Reset()), and (1, A, A^2, A^3, ...)
// forms an M-1 cycle, and the seed determines the location in this
// cycle from which the generator starts outputting values.
//
// BE VERY CAREFUL when using SzlACMRandom for applications where very
// good statistical randomness is required in extended monte carlo
// experiments.  While SzlACMRandom is completely uniform over its range
// of [1, 2^31-1), its (relatively) short cycle length of 2^31-2 can
// cause problems in some situations.
//
// On modern hardware (e.g., my 2.7GHz desktop), it takes only 96
// seconds to cycle through this entire range (measured by running
// "time arc4_perf_test -acmcycle"; cycle length can be checked by
// "arc4_perf_test -floyd"), so the kinds of monte carlo experiments
// that can exhaust the output cycle is quite feasible.  For these
// extended monte carlo experiments, this relatively short cycle
// length could have an effect on the results -- esp if the LCM of the
// number of Next() invocations per sample in the experiment and the
// cycle length is no greater than the cycle length, then running the
// monte carlo experiment for anything longer than one cycle of the
// generator is useless.  Since 2^31-2 = 2 3 3 7 11 31 151 331, so
// there are many small factors, which increases the odds of this
// happening.  If there are K invocations of Next() per experimental
// sample, the experiment cycles after LCM(K,M-1)/K samples.
//
// In particular Next64() is just two invocations of Next(), so it has
// a cycle length of LCM(2,2^31-2)/2 = 2^30-1, not anything like
// approximately 2^64 that one might otherwise expect.  (We could
// think of Next64() as an experiment where we use Next() to sample
// the space of 64-bit numbers.)
//
// Other generators such as bsd's random() has a longer cycle length
// of 16*(2^31-1), which though longer, does not improve things that
// much wrt cycling in the space of experimental samples.
//
// DO use SzlACMRandom for situations where extreme statistical quality
// is not important.


// This class is not a proper implementation of RandomBase.
// In particular, the high-order bit of Rand32() and the two high-order
// bits of Rand64() are always zero, and the methods do not return all
// possible values in the remaining bits.


class SzlACMRandom : public RandomBase {
 public:
  virtual ~SzlACMRandom();
   // You must choose a seed.
  inline explicit SzlACMRandom(int32 seed);

  // Seed initializer with determinism.
  static int32 DeterministicSeed() {
    return 301;
  }

  // Seed initializer with some initial entropy.
  static int32 HostnamePidTimeSeed();

  // No DevRandomSeed() because of the risk of blocking on
  // low-entropy production machines.

  // If 'seed' is not in [1, 2^31-2], the range of numbers normally
  // generated, it will be silently set to 1.
  inline void Reset(int32 seed);
  int32 GetSeed() const { return seed_; }

  // Checkpoint the state of this RNG.
  RandomBase* Clone() const  { return new SzlACMRandom(GetSeed()); }

  // Returns a pseudo-random number in the range [1, 2^31-2].
  // Note that this is one number short on both ends of the full range of
  // non-negative 32-bit integers, which range from 0 to 2^31-1.
  inline int32 Next();

  // DO NOT USE Next64() IF A SHORT CYCLE LENGTH IS IMPORTANT
  //
  // Returns a pseudo-random number in the range [1, (2^31-2)^2].
  // Note that this does not cover all non-negative values of int64, which
  // range from 0 to 2^63-1.  The top two bits are ALWAYS ZERO.
  int64 Next64();

  // If n == 0, returns the next pseudo-random number in the range [0 .. 0]
  // If n != 0, returns the next pseudo-random number in the range [0 .. n)
  // This definition overrides RandomBase::Uniform() to ensure compatibility
  // with previous implementation of SzlACMRandom::Uniform().
  virtual int32 Uniform(int32 n) {
    if (n == 0) {
      return Next() * 0;
    } else {
      return Next() % n;
    }
  }

  // If n == 0, returns the next pseudo-random number in the range [0 .. 0]
  // If n != 0, returns the next pseudo-random number in the range [0 .. n)
  // Slightly less efficient than Uniform but generates more accurate
  // uniform distribution for big n.
  // This function can call Next more than once.
  // n must be in the range [0 .. 2^31-1).
  virtual int32 UnbiasedUniform(int32 n) ;

  // Returns a floating-point number in the range (0, 1).  Note that
  // neither 0 nor 1 is a possible value.
  float RndFloat() {
    return Next() * 0.000000000465661273646;  // x: x * (M-1) = 1 - eps
  }

  // RandXX: Generate random numbers conforming to the RandomBase interface.
  // Since we do not have a pool, Rand8 and Rand16 do a full RNG cycle
  // and mask off the appropriate bits.
  // Rand32 and Rand64 also generate numbers within a reduced range.
  // Rand32 generates a subset with the range [0, 2^31-3].
  // Rand64 generates a subset with the range [0, (2^31-2)^2-1].
  virtual uint8  Rand8();
  virtual uint16 Rand16();
  virtual uint32 Rand32();
  virtual uint64 Rand64();

  // Deprecated

  // The 0-arg constructor is deprecated.  It implicitly uses a fixed seed.
  // Fixed seeds are fine for some purposes, but if you want one,
  // you should explicitly choose to have one.
  // http://wiki/Main/FixAcmRandom
  SzlACMRandom() {
    Reset(DeterministicSeed());
  }

  // This name is deprecated.  Use HostnamePidTimeSeed instead,
  // which expresses more accurately what seed you are getting.
  // http://wiki/Main/FixAcmRandom
  static int32 GoodSeed() { return HostnamePidTimeSeed(); }

  // This name is deperecated.  It is part of the transition plan
  // to eliminate the 0-arg constructor.
  // http://wiki/Main/FixAcmRandom
  static int32 DeprecatedDefaultSeed() {
    return DeterministicSeed();
  }

  // End of deprecated

 private:
  static const uint32 M = 2147483647L;   // 2^31-1
  uint32 seed_;
};

inline void SzlACMRandom::Reset(int32 s) {
  seed_ = s & 0x7fffffff;  // make this a non-negative number
  if (seed_ == 0 || seed_ == M) {
    seed_ = 1;
  }
}

inline SzlACMRandom::SzlACMRandom(int32 s) {
  Reset(s);
}

inline int32 SzlACMRandom::Next() {
  static const uint64 A = 16807;  // bits 14, 8, 7, 5, 2, 1, 0
  // We are computing
  //       seed_ = (seed_ * A) % M,    where M = 2^31-1
  //
  // seed_ must not be zero or M, or else all subsequent computed values
  // will be zero or M respectively.  For all other values, seed_ will end
  // up cycling through every number in [1,M-1]
  uint64 product = seed_ * A;

  // Compute (product % M) using the fact that ((x << 31) % M) == x.
  seed_ = (product >> 31) + (product & M);
  // The first reduction may overflow by 1 bit, so we may need to repeat.
  // mod == M is not possible; using > allows the faster sign-bit-based test.
  if (seed_ > M) {
    seed_ -= M;
  }
  return seed_;
}
