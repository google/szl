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

// Provides a base class with common operations for random number
// generators.  This class does not include any routines that maintain
// any state information.

#include <string>


inline uint32 Word32At(const uint8* ptr) {
  return ((static_cast<uint32>(ptr[0])) +
          (static_cast<uint32>(ptr[1]) << 8) +
          (static_cast<uint32>(ptr[2]) << 16) +
          (static_cast<uint32>(ptr[3]) << 24));
}


class RandomBase {
 public:
  // constructors.  Don't do too much.
  RandomBase() {}
  virtual ~RandomBase();

  // Clone: generate a direct copy of this pseudorandom number generator.
  // NB: Returns NULL if Clone is not implemented/available.
  virtual RandomBase* Clone() const = 0;

  // Generate pseudorandom output of various sizes.  Output must be
  // *uniformly* random for all possible values of the various output
  // sizes.  Some generators naturally output more than 8 bits at a
  // time, and have to buffer.  We leave these as virtual so that such
  // generators could output natural sizes if the request is for
  // greater than the natural size is requested, and draw from
  // buffered output for fractional output.  E.g., AES in counter mode
  // with key = seed would be one such generator.
  virtual uint8  Rand8()  = 0;
  virtual uint16 Rand16() = 0;
  virtual uint32 Rand32() = 0;
  virtual uint64 Rand64() = 0;

  // Returns a string of random bytes of a given desired length,
  // constructed by invoking Rand8 repeatedly.
  //
  // Note, however, that for secure random number generators based on
  // block ciphers, extracting output from the generator one byte at a
  // time is somewhat inefficient.  This is a virtual function to
  // permit block-cipher--based implementations to override the
  // provided definition.
  virtual string RandString(int desired_len);

  // UnbiasedUniform: The standard Uniform() generates a number with a
  // bias towards low values.  This method will generate a number that
  // is unbiased.
  virtual int32 UnbiasedUniform(int32 n);

  virtual uint64 UnbiasedUniform64(uint64 n);

  // ------------------------------------------------------------
  // General output utilities for all random number generators.

  // DEPRECATED_RndFloat: returns a random float NOT QUITE uniformly
  // distributed in the range [0.0, 1.0).  Use RandFloat instead.
  // Note that this include 0.0, so differs from SzlACMRandom's version.
  float DEPRECATED_RndFloat() {
    // Because 0x7fffffff / 0x8000000 is too close to 1.0 and IEEE-754
    // rounds, the following does not work:
    //
    // return static_cast<double>(0x7fffffff & Rand32()) / (1U<<31);
    //
    // using the compile time evaluated expression is more accurate:
    // we should get 4.6566128730773926e-10 instead of
    // 0.00000000046566128730; // 2^{-31}
    //
    // What we want is the maximum value to be 24 one bits (incl
    // implicit leading 1 in normalize float representation) in the
    // single precision mantissa, i.e., 1.0 - 2**-24.  The integer
    // random value of 0xffffffff should map to this, so we use (1.0 -
    // 2**-24) / 0xffffffff) as the multiplier (compile-time const).
    //
    // NB: Note that distribution among floating point numbers is NOT
    // uniform, since due to rounding some values are more likely to
    // appear than others in a Moire-like pattern.  The density of
    // pseudorandom floats within each subrange of [0,1) is
    // approximately uniform, however, so this may still be useful --
    // a single floating point multiply is slightly faster than
    // bitfield manipulation plus a floating point subtract (and
    // normalize) on some processors.  On my desktop 2.793 GHz P4,
    // DEPRECATED_RndFloat generates 100,000,000 numbers in 12.215
    // seconds, whereas the bitfield RandFloat took 12.143 seconds, so
    // the difference is in the noise.
    //
    // return static_cast<float>(Rand32()) *
    //  ((1.0 - 0.00000005960464477539)/ static_cast<double>(0xffffffff));

    // waldemar's suggestion:
    return static_cast<float>((Rand32() & 0x7fffff) *  // 23bits mantissa
                              0.000000119209289550781250000);  // 2^{-23}
  }
  // The obvious analog for a RndDouble (this interface did not exist
  // in acmrandom) would simply use different constants but require
  // the LL longlong gnu extension and so we omit this code.  The
  // difference between bit-field--based RandDouble and the following
  // is noticeable.  Bit-field--based RandDouble on the same machine
  // took 18.919s to output 100,000,000 numbers, but the following
  // version took 28.382s.

  // double DEPRECATED_RndDouble() {
  //  return static_cast<double>((Rand64() & 0xfffffffffffffLL) *
  //                .0000000000000002220446049250313080847263336181640625000);
  // }

  // RandFloat: returns a uniformly distributed random float in the
  // range [0.0, 1.0), for the following notion of uniform: All
  // floating point numbers that are distinguishable within 2^-m where
  // m is the number of bits in the mantissa are uniformly generated.
  //
  // We generate numbers by creating pseudorandom numbers y where y is
  // uniform between [1.0, 2.0), that is, uniform probability for all
  // floating point number y satisfying 1.0 <= y < 2.0.  This is
  // "natural" with the implicit 1 in the mantissa.  To get
  // pseudorandom numbers in [0.0, 1.0), we simply set x = y - 1.0.
  // This means that some floating point numbers, e.g., 1.0e-40, will
  // never be output.
  virtual float RandFloat();

  double RandDouble();

  // RandExponential: Generate a random number conforming to an
  // exponential distribution
  double RandExponential();

  // Uniform: return a pseudorandom integer in [0, n).  Note that
  // uniformity is approximate, so for large n -- e.g., close to 2**20
  // or so -- the bias towards the low values will become detectable
  // in many statistical tests.  Typical input values are small.
  // SzlACMRandom::Uniform() overrides this definition in order to maintain
  // backwards compatibility.
  virtual int32 Uniform(int32 n) {
    assert(n >= 0);          // runtime check for negative modulus since % neg
                             // is not well defined and mod'ing by large
                             // values will not be uniform anyways.
    if (0 == n) {
      return Rand32() * 0;   // consume an output in any case
    } else {
      return Rand32() % n;
    }
  }

  // A functor-style version of Uniform, so a generator can be a model of
  // STL's RandomNumberGenerator concept.  Example usage:
  //   SzlACMRandom rand(FLAGS_random_seed);
  //   random_shuffle(myvec.begin(), myvec.end(), rand);
  int32 operator() (int32 n) {
    return Uniform(n);
  }

  bool OneIn(int X) { return Uniform(X) == 0; }

  // PlusOrMinus: return a uniformly distributed value in the range
  // [value - (value * multiplier), value + (value * multiplier) )
  // (i.e. inclusive on the lower end and exclusive on the upper end).
  //
  // Be careful of floating point rounding, e.g., 1.0/29 is inexactly
  // represented, and so we get:
  //
  // PlusOrMinus(2 * 29, 1.0/29) is
  // PlusOrMinus(58, 0.0344827849223) which gives
  // range = static_cast<int32>(1.999999992549) = 1, rand_val \in [0, 2)
  // and return result \in [57, 59) rather than [56, 60) as probably
  // intended.  (This holds for IEEE754 floating point.)
  //
  // see util/random/plus_or_minus_test.cc
  int32 PlusOrMinus(int32 value, float multiplier) {
    const int32 range = static_cast<int32>(value * multiplier);
    const int32 rand_val = Uniform(range * 2);
    return value - range + rand_val;
  }

  // A similar version to PlusOrMinus, but for floating point values
  float PlusOrMinusFloat(float value, float multiplier) {
    const float range = value * multiplier;
    const float rand_val = RandFloat() * range * 2;
    return value - range + rand_val;
  }

  // Skewed: pick "base" uniformly from range [0,max_log] and then
  // return "base" random bits.  The effect is to pick a number in the
  // range [0,2^max_log-1] with bias towards smaller numbers.
  int32 Skewed(int max_log) {
    const int32 base = Rand32() % (max_log+1);
    // this distribution differs slightly from SzlACMRandom's Skewed,
    // since 0 occurs approximately 3 times more than 1 here, and
    // SzlACMRandom's Skewed never outputs 0.
    return Rand32() & ((1u << base)-1);
  }

  // Utility methods to generate weak seeds for various RNG
  // implementations. (IE. MTRandom, SzlACMRandom).
  static uint32 WeakSeed32();

  // Utility method to generate a weak seed string.
  static int WeakSeed(uint8* buffer, int length);

};
