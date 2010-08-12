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

// Implementation of the Mersenne Twister RNG.  (MT19937). MT is a very
// good RNG, and is intended to be a replacement for users of SzlACMRandom.
// It generates sequences that have more apparent randomness, and is
// much faster (approximately 50%, optimized).
// (Rand32() MTRandom: 13ns vs. 17 ns. on a P4 3.2 Ghz)
// MTRandom maintain about 2Kb of state.
//
// The MT random number generator has a period of 2^19937-1.
//
// NOTE: Statistical analysis of MT has demonstrated detectable
// bias in sequences that it generates.  It is a good candidate for
// monte-carlo simulation, but should not be used where security
// is required.
//
// Also, our initialization routine may not be identical to other publicly
// available implementations, and methods that generate real numbers also
// differ from other publicly availble implementations, so take care when
// comparing with other implementations.

#include <string>


// MTRandom: An implementation of the MT19937 RNG class.  Implements
// the RandomBase interface.
//
// Example:
//   RandomBase* b = new MTRandom();
//   cout << " Hello, a random number is: " << b->Rand32() << endl;
//   delete b;

class MTRandom : public RandomBase {
 public:
  // Create an instance of MTRandom using a single seed value.
  // Calls InitSeed() to initialize the context.
  explicit MTRandom(uint32 seed);

  // Seed MTRandom using a string as seed.  Uses InitArray().
  explicit MTRandom(const string& seed);

  // Seed MTRandom using an array of uint32.  When using this initializer,
  // 'seed' should be well distributed random data of kMTSizeBytes bytes
  // aligned to a uint32, since no additional mixing is done.
  // Requires: num_words == kMTNumWords
  // TODO: Change num_words by size (in bytes.
  MTRandom(const uint32* seed, int num_words);

  // Creates an MTRandom generator object that has been seeded using
  // Some weak random data.  (time of day, hostname, etc.).  Uses InitArray().
  MTRandom();

  virtual ~MTRandom();

  virtual MTRandom *Clone() const;

  virtual uint8  Rand8();
  virtual uint16 Rand16();
  virtual uint32 Rand32();
  virtual uint64 Rand64();

  static int SeedSize() { return kMTSizeBytes; }

  // The log2 of the RNG buffers (based on uint32).
  static const int kMTNumWords = 624;

  // The size of the RNG buffers in bytes.
  static const int kMTSizeBytes = kMTNumWords * sizeof(uint32);

 private:
  // InitRaw: Initialize the MTRandom context using an array of raw
  // uint32 values.  Requires length == SeedSize().
  void InitRaw(const uint32* seed, int length);

  // Initialize the MTRandom context using a 32-bit seed.  The seed
  // is distributed across the initial space.
  // NOTE: This will not seed the generator with identical values as
  // either of the seed algorithms in the original paper.  If an
  // identical sequence is required, use InitRaw.
  void InitSeed(uint32 seed);

  // InitArray: Initialize the MTRandom context using an array of
  // uint32 values.  The values will be mixed to form an initial seed.
  void InitArray(const uint32* seed, int length);

  // The MT context. This holds our RNG state and the current generation
  // of generated numbers.
  struct MTContext {
    int8   poolsize;  // For giving back bytes.
    int32  randcnt;  // Count of remaining bytes.
    uint32 pool;
    uint32 buffer[kMTNumWords];
  };

  // This method cycles the MTContext and generates the next set of random
  // numbers.
  void Cycle();

  MTContext context_;
};
