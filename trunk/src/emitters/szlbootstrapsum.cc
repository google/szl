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

// This class implements a bootstrap based statistical sum table.
// In place of a single sum of the values it generates N samples
// of the sum where each value emitted to the table is
// probabilistically added to each of the N samples. The samples
// are an approximation of the distribution of the underlying
// aggregate variable computed by the ordinary sum table.

#include <string>


#include "public/porting.h"
#include "public/commandlineflags.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "utilities/random_base.h"
#include "utilities/mt_random.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szltype.h"
#include "public/szlvalue.h"
#include "public/szltabentry.h"


DEFINE_bool(bootstrapsum_fastpath, true, "Enable fast path sampling.");
DEFINE_string(bootstrapsum_seed, "",
              "Seed used when SetRandomSeed is not called.");

namespace {

// This table is essentially the CDF for the poisson distribution.
// It is used to transform a uniform random variable in [0, 2^32)
// to a poisson distributed variable.
extern const uint32 kPoissonCutoffs[];

// Indicates whether a given value of byte is sufficient to determine
// a poisson value if the byte is the MSB of a uniform random value.
struct PoissonDispatchEntry {
  // Minimum possible poisson value.
  uint16 value;
  // Whether the minimum value is also the only possible value.
  uint16 ambiguous;
};

// This array contains 256 entries, one for each possible value of a byte.
extern const PoissonDispatchEntry kDispatchTable[];

// We use this type to allow storing integers and floats in a single array.
// SzlValue is not reused because it contains pointer types which makes its
// size larger due to LP64 pointer + int length = 12 bytes.
struct Summable {
  union {
    int64 integer;
    double real;
  };
  Summable(): integer(0) {}
};

// Supports efficient generation of integers following the poisson
// distribution. This class is able to consume individual bytes
// from a 4-byte random number and uses a dispatch table to avoid
// looping through the poisson cutoffs in the common case.
// This ends up saving on both the random number generation and
// the cost of converting a random number to a poisson distributed
// integer.
template<class Source>
class PoissonDice {
 public:
  // Initialize the poisson generator with the given random number
  // generator. fast_path indicates if the optimized generation
  // strategy should be employed.
  explicit PoissonDice(Source *random, bool fast_path)
      : random_(random), fast_path_(fast_path), remaining_(0)   { }

  // Generate an integer drawn from the canonical poisson distribution.
  int Roll() {
    int value;
    uint32 coin;
    if (fast_path_) {
      // In the fast path approach we initially consume a single random
      // byte and look up a table with an entry for each possible value
      // of a byte to tell us whether the first byte (considered as the
      // MSB), independently of any possible values for the next 3 bytes,
      // determines a result. If it does we just return that result,
      // otherwise we request 4 additional bytes, replace the MSB of those
      // with initial byte pulled and fallback to the naive approach.
      // One minor additional optimization is that we start the cutoff
      // search as far down as possible (derived from the lookup table).
      coin = GetByte();
      value = kDispatchTable[coin].value;
      if (kDispatchTable[coin].ambiguous) {
        coin = (coin << 24) | (0x00FFFFFF & random_->Rand32());
      } else {
        return value;
      }
    } else {
      value = 0;
      coin = random_->Rand32();
    }
    while (kPoissonCutoffs[value] < coin) ++value;
    DCHECK_GE(value, 0);
    return value;
  }

  void Reset(Source *random) {
    delete random_;
    random_ = random;
    remaining_ = 0;
  }

 private:
  // Since extracting a single byte is a very common case having a fast
  // custom inline implementation yields noticeable performance gains.
  // Using Rand8 instead of this custom implementation leads to a 20%
  // performance regression in the micro-benchmarks.
  uint32 GetByte() {
    if (remaining_ == 0) {
      buffer_.rand32 = random_->Rand32();
      remaining_ = 3;
    } else {
      remaining_ -= 1;
    }
    return static_cast<uint32>(buffer_.bytes[remaining_]);
  }

  Source* random_;
  bool fast_path_;
  int remaining_;
  union {
    uint32 rand32;
    uint8 bytes[4];
  } buffer_;
};

// Generates random numbers using a 64-bit linear congruential generator.
// Constants from 64-bit update to TAOCP.
class Random64Source {
 public:
  explicit Random64Source(uint64 seed) : current_(seed) {}

  uint32 Rand32() {
    current_ = 6364136223846793005ULL * current_
             + 1442695040888963407ULL;
    return static_cast<uint32>(current_ >> 32);
  }

 private:
  uint64 current_;
};


//----------------------------------------------------------------------
// SzlBootstrapsum
//
// Bootstrap table writer that creates bootstrap table entries. It also
// manages the single instance of the poisson dice used per table.
//----------------------------------------------------------------------

class SzlBootstrapsum : public SzlTabWriter {
 private:
  explicit SzlBootstrapsum(const SzlType& type)
      : SzlTabWriter(type, true, false), dice_(NULL) { }
  virtual ~SzlBootstrapsum()  { }

 public:
  template<class Dice> class SzlBootstrapsumEntry;
  static SzlTabWriter* Create(const SzlType& type, string* error) {
    if (!SzlOps::IsNumeric(type.element()->type())) {
      *error = "contains non-numeric fields.";
      return NULL;
    }
    if (type.weight() == NULL ||
        type.weight()->type().kind() != SzlType::kFingerprint.kind()) {
      *error = "requires a weight of type fingerprint.";
      return NULL;
    }
    return new SzlBootstrapsum(type);
  }

  virtual SzlTabEntry* CreateEntry(const string& index) const {
    // Check if SetRandomSeed may not have been called.
    if (dice_ == NULL) {
      LOG(WARNING) << "Using weak random seed for poisson dice.";
      InitializeDice(FLAGS_bootstrapsum_seed.empty() ?
                     new MTRandom() :
                     new MTRandom(FLAGS_bootstrapsum_seed));
    }
    return new SzlBootstrapsumEntry<Dice>(element_ops_, param(), dice_);
  }

  virtual void SetRandomSeed(const string& seed) {
    InitializeDice(new MTRandom(FingerprintString(seed)));
  }

 private:
  typedef PoissonDice<RandomBase> Dice;
  mutable Dice* dice_;

  void InitializeDice(RandomBase *rng) const {
    if (dice_ == NULL) {
      dice_ = new Dice(rng, FLAGS_bootstrapsum_fastpath);
    } else {
      dice_->Reset(rng);
    }
  }

 public:
  template<class Dice>
  class SzlBootstrapsumEntry : public SzlTabEntry {
   public:
    explicit SzlBootstrapsumEntry(const SzlOps& element_ops, int param,
                                  Dice *dice)
        : element_ops_(element_ops), num_rows_(param),
          dice_(CHECK_NOTNULL(dice)), samples_(NULL), packed_(NULL)  { }
    virtual ~SzlBootstrapsumEntry()  { }

    virtual int AddWeightedElem(const string& elem, const SzlValue& weight);
    virtual void Flush(string* output);
    virtual void FlushForDisplay(vector<string>* output);
    virtual SzlTabEntry::MergeStatus Merge(const string& val);

    virtual void Clear() {
      tot_elems_ = 0;
      delete [] samples_;
      delete [] packed_;
      samples_ = NULL;
      packed_ = NULL;
    }

    virtual int Memory() {
      int size = sizeof(SzlBootstrapsumEntry);
      if (samples_ != NULL) {
        size += sizeof(Summable) * element_ops_.nflats() * num_rows_;
      }
      if (packed_ != NULL) {
        size += sizeof(Summable) * element_ops_.nflats();
      }
      return size;
    }

    virtual int TupleCount()  { return num_rows_; }

   private:
    const SzlOps& element_ops_;
    // Common stuff for the table.
    const int num_rows_;
    Dice *dice_;
    // We store the bootstrap table as an MxN array where
    //   M = element_ops_.nflats() ie. size of output tuple.
    //   N = number of bootstrap samples.
    Summable* samples_;
    // Buffer to store a packed entry during summation.
    Summable* packed_;
  };
};

REGISTER_SZL_TAB_WRITER(bootstrapsum, SzlBootstrapsum);



//----------------------------------------------------------------------
// SzlBootstrapsumEntry implementation.
//----------------------------------------------------------------------

// Performs the core bootstrap sampling and summation.
// Templatized to simplify testing.
template<class Dice>
inline void BootstrapSampleAndSum(const int row_size,
                                  const int num_integers,
                                  const int num_rows,
                                  Summable *cursor,
                                  Summable *update,
                                  Dice *dice) {
  // We always add it to the first "sample".
  int value = 1;
  for (int row = 0; row < num_rows; ++row) {
    if (value != 0) {
      int j = 0;
      for (; j < num_integers; ++j, ++cursor) {
        cursor->integer += value * update[j].integer;
      }
      for (; j < row_size; ++j, ++cursor) {
        cursor->real += value * update[j].real;
      }
    } else {
      // Here we altogether skip the loop.
      cursor += row_size;
    }
    value = dice->Roll();
  }
}

// For a given tuple we reorganize the order so that all the integer values
// appear at the front of the array in the same relative order as the tuple
// and the floats appear the end of the array in the reverse relative order.
// This rearrangement allows for efficient summation by eliminating a type
// switch within an inner most loop.
inline int PackSzlValue(const SzlOps& element_ops,
                        const SzlValue& elemv,
                        Summable *packed) {
  const int row_size = element_ops.nflats();
  const SzlValue *elems = element_ops.type().kind() == SzlType::TUPLE ?
      elemv.s.vals : &elemv;

  int pos_integer = 0;
  int pos_float = row_size - 1;
  for (int i = 0; i < row_size; ++i) {
    switch (element_ops.kind(i)) {
      case SzlType::INT: packed[pos_integer++].integer = elems[i].i; break;
      case SzlType::FLOAT: packed[pos_float--].real = elems[i].f; break;
      default: LOG(FATAL) << "Can't emit non-numerics to bootstrapsum tables";
    }
  }
  return pos_integer;
}

template<class Dice>
int SzlBootstrapsum::SzlBootstrapsumEntry<Dice>::
AddWeightedElem(const string& elem, const SzlValue& weight) {
  const int row_size = element_ops_.nflats();

  // Initialize memory.
  int mem = 0;
  if (samples_ == NULL) {
    const int table_size = row_size * num_rows_;
    samples_ = new Summable[table_size];
    mem += sizeof(Summable) * table_size;
  }
  if (packed_ == NULL) {
    packed_ = new Summable[row_size];
    mem += sizeof(Summable) * row_size;
  }

  // Unpack the szl value into the summable array.
  SzlValue elemv;
  CHECK(element_ops_.ParseFromArray(elem.data(), elem.size(), &elemv));
  int pos_integer = PackSzlValue(element_ops_, elemv, packed_);
  element_ops_.Clear(&elemv);

  tot_elems_++;
  uint64 seed = static_cast<uint64>(weight.i);
  if (seed) {
    Random64Source source(seed);
    PoissonDice<Random64Source> seeded_dice(
        &source, FLAGS_bootstrapsum_fastpath);
    BootstrapSampleAndSum(row_size, pos_integer, num_rows_,
                          samples_, packed_, &seeded_dice);
  } else {
    BootstrapSampleAndSum(row_size, pos_integer, num_rows_,
                          samples_, packed_, dice_);
  }

  return mem;
}


template<class Dice>
void SzlBootstrapsum::SzlBootstrapsumEntry<Dice>::Flush(string* output) {
  if (tot_elems_ == 0) {
    output->clear();
    return;
  }

  // Combine all of the counts and tags into a single sorted string.
  SzlEncoder enc;
  enc.PutInt(tot_elems_);
  const int row_size = element_ops_.nflats();
  SzlValue elemv;
  for (int i = 0, cursor = 0; i < num_rows_; ++i, cursor += row_size) {
    Summable *packed = samples_ + cursor;
    int pos_integer = 0;
    int pos_float = row_size - 1;
    for (int j = 0; j < row_size; ++j) {
      switch (element_ops_.kind(j)) {
        case SzlType::INT:
          element_ops_.PutInt(packed[pos_integer++].integer, j, &elemv);
          break;
        case SzlType::FLOAT:
          element_ops_.PutFloat(packed[pos_float--].real, j, &elemv);
          break;
        default: LOG(FATAL) << "Can't emit non-numerics to bootstrapsum tables";
      }
    }
    element_ops_.Encode(elemv, &enc);
  }
  element_ops_.Clear(&elemv);

  enc.Swap(output);
  Clear();
}


template<class Dice>
void SzlBootstrapsum::SzlBootstrapsumEntry<Dice>::FlushForDisplay(
                      vector<string>* output) {
  output->clear();
  if (tot_elems_ == 0) {
    output->push_back("");
    return;
  }

  // Combine all of the counts and tags into sorted strings.
  const int row_size = element_ops_.nflats();
  SzlValue elemv;
  for (int i = 0, cursor = 0; i < num_rows_; ++i, cursor += row_size) {
    SzlEncoder enc;
    Summable *packed = samples_ + cursor;
    int pos_integer = 0;
    int pos_float = row_size - 1;
    for (int j = 0; j < row_size; ++j) {
      switch (element_ops_.kind(j)) {
        case SzlType::INT:
          element_ops_.PutInt(packed[pos_integer++].integer, j, &elemv);
          break;
        case SzlType::FLOAT:
          element_ops_.PutFloat(packed[pos_float--].real, j, &elemv);
          break;
        default: LOG(FATAL) << "Can't emit non-numerics to bootstrapsum tables";
      }
    }
    element_ops_.Encode(elemv, &enc);
    string encoded;
    enc.Swap(&encoded);
    output->push_back(encoded);
  }
  element_ops_.Clear(&elemv);
}

// Merge a flushed state into the current state.
// Returns whether the string looked like a valid SzlBootstrapsumEntry Flush state.
template<class Dice>
SzlTabEntry::MergeStatus SzlBootstrapsum::SzlBootstrapsumEntry<Dice>::Merge(
                                          const string& val) {
  if (val.empty())
    return MergeOk;

  SzlDecoder dec(val.data(), val.size());
  int64 new_elements;
  if (!dec.GetInt(&new_elements) || new_elements <= 0)
    return MergeError;

  const int row_size = element_ops_.nflats();
  int pos_integer = 0;  // Need this outside the loop.
  Summable* new_samples = new Summable[row_size * num_rows_];
  Summable *cursor = new_samples;
  SzlValue elemv;
  for (int i = 0; i < num_rows_; ++i, cursor += row_size) {
    if (element_ops_.Decode(&dec, &elemv)) {
      pos_integer = PackSzlValue(element_ops_, elemv, cursor);
    } else {
      delete [] new_samples;
      return MergeError;
    }
  }
  element_ops_.Clear(&elemv);

  if (!dec.done()) return MergeError;

  // The data was well formatted so now merge with current data.
  if (samples_ == NULL) {
    samples_ = new_samples;
  } else {
    const int num_integers = pos_integer;
    for (int i = 0, cursor = 0; i < num_rows_; ++i) {
      int j = 0;
      for (; j < num_integers; ++j, ++cursor) {
        samples_[cursor].integer += new_samples[cursor].integer;
      }
      for (; j < row_size; ++j, ++cursor) {
        samples_[cursor].real += new_samples[cursor].real;
      }
    }
    delete [] new_samples;
  }

  tot_elems_ += new_elements;

  return MergeOk;
}

// Constant tables.

// This table is the CDF of the poisson distribution normalized by 2^32.
//   kPoissonCutoffs[i] = 2^32 * CDF_poisson(i)
const uint32 kPoissonCutoffs[] = {
  1580030169UL, 3160060337UL, 3950075422UL, 4213413783UL,
  4279248373UL, 4292415291UL, 4294609777UL, 4294923275UL,
  4294962462UL, 4294966816UL, 4294967251UL, 4294967291UL, 4294967295UL };

// The values of this table are defined as:
// Given a value b in [0, 256)
//   value = varmin_i(2^24 * b < kPoissonCutoffs[i])
//   ambiguous = 2^24 * (b + 1) - 1 >= kPoissonCutoff[value]
// In english, each entry tells us that when its corresponding
// byte is used as the MSB for a 32bit uniform random value what
// the minimum possible poisson distributed value and whether the
// next 24bits could potentially push the uniform random value
// past a higher poisson cutoff.
const PoissonDispatchEntry kDispatchTable[] = {
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 1}, {1, 0},
  {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
  {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
  {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
  {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
  {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
  {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
  {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
  {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
  {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
  {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
  {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
  {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 1}, {2, 0}, {2, 0}, {2, 0},
  {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0},
  {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0},
  {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0},
  {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0},
  {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0}, {2, 0},
  {2, 0}, {2, 0}, {2, 0}, {2, 1}, {3, 0}, {3, 0}, {3, 0}, {3, 0},
  {3, 0}, {3, 0}, {3, 0}, {3, 0}, {3, 0}, {3, 0}, {3, 0}, {3, 0},
  {3, 0}, {3, 0}, {3, 0}, {3, 1}, {4, 0}, {4, 0}, {4, 0}, {4, 1}
};

}  // namespace
