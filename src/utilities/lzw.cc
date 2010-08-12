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

#include <limits.h>
#include <memory.h>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/lzw.h"

// decompressor settings
class LZWParams {
 public:
  enum {
    kHtabSize = 69001,  // 95% occupancy
    kInitBits = 9,      // initial bits
    kBits     = 16,     // default bits
    kFirst    = 257,    // first free entry
    kClear    = 256,    // table clear output code
  };
};

LZWInflate::LZWInflate(int32 maxbits, bool block) {
  avail_in_ = 0;
  avail_out_ = 0;
  out_buf_avail_ = 0;

  memset(&zs_, 0x00, sizeof(zs_));

  // Validate maxbits, which cannot be greater than 16.
  if (maxbits > 16) {
    zs_.maxbits = 16;
  } else if (maxbits < 9) {
    zs_.maxbits = 9;
  } else {
    zs_.maxbits = maxbits;
  }

  zs_.block_compress = block;

  zs_.htab = new int32[LZWParams::kHtabSize];
  zs_.codetab = new uint16[LZWParams::kHtabSize];
}

int64 LZWInflate::Inflate(void* out, int64 avail) {
  // Check we have enough input to do anything with.
  if (avail <= 0) {
    return avail;
  }

  avail_out_ = avail;
  out_buf_avail_ = 0;
  out_buf_ = static_cast<uint8*>(out);

  FillOutputBuffer();

  return out_buf_avail_;
}

int64 LZWInflate::Tell() const {
  return avail_in_;
}

bool LZWInflate::Input(const void* in, int64 avail) {
  if (avail < 0 || (in == NULL && avail != 0)) {
    return false;
  } else {
    next_in_ = static_cast<const int8*>(in);
    avail_in_ = avail;
    return true;
  }
}

int32 LZWInflate::CreateCompressCode() {
  if (zs_.clear_flag) {
    zs_.n_bits -= zs_.loctets;
    zs_.loctets = 0;
   
    // Paranoid check (should never happen)
    if (zs_.n_bits < 0) {
      LOG(FATAL) << "Error occurred while proessing clear code in data stream";
    }

    // Do we have enough data to consume the slack?
    if (avail_in_ >= zs_.n_bits) {
      avail_in_ -= zs_.n_bits;
      next_in_ += zs_.n_bits;
    } else {
      // Not enough data, consume whatever is available
      // then return -1.
      next_in_ += avail_in_;
      zs_.n_bits -= avail_in_;
      avail_in_ = 0;
      return -1;
    }

    // Okay, operation successful.
    zs_.n_bits = LZWParams::kInitBits;
    zs_.maxcode = (1L << zs_.n_bits) - 1;
    zs_.clear_flag = false;
  }

  if (avail_in_ < 2) {
    return -1;
  }

  if ((zs_.loctets << 3) >= ((zs_.n_bits << 3) - (zs_.n_bits - 1))) {
    zs_.loctets = 0;
  }

  // If we're in block mode, check if the table is saturated.
  if (zs_.freeent > zs_.maxcode) {
    zs_.n_bits++;
    if (zs_.n_bits == zs_.maxbits) {
      zs_.maxcode = zs_.maxmaxcode;
    } else {
      zs_.maxcode = (1L << zs_.n_bits) - 1;
    }
  }

  const uint8 *bp = reinterpret_cast<const uint8*>(next_in_);
  int32 gcode = (*bp++ >> zs_.offset);
  int32 r_off = CHAR_BIT - zs_.offset;
  int32 bits = zs_.n_bits - r_off;

  // Bits will never be > 16.
  if (bits > CHAR_BIT) {
    if (avail_in_ < 3)
      return -1;
    gcode |= *bp++ << r_off;
    r_off += CHAR_BIT;
    bits -= CHAR_BIT;
  }

  // Now find the final lzw code.
  gcode |= (*bp & ((1L << bits) - 1)) << r_off;
  zs_.offset += zs_.n_bits;

  // Move the input past any bytes no longer needed.
  next_in_ += (zs_.offset >> 3);
  zs_.loctets += (zs_.offset >> 3);
  avail_in_ -= (zs_.offset >> 3);

  // Clear those bytes out of the offset.
  zs_.offset &= 7;

  return gcode;
}

bool LZWInflate::FillOutputBuffer() {
  // Initialise the state if this hasnt been done before.
  if (zs_.init == false) {
    zs_.n_bits     = LZWParams::kInitBits;
    zs_.maxcode    = (1L << zs_.n_bits) - 1;
    zs_.maxmaxcode = (1L << zs_.maxbits);

    // Initialize the tables.
    for (zs_.code = 255L; zs_.code >= 0; zs_.code--) {
      zs_.codetab[zs_.code] = 0;
      zs_.htab[zs_.code] = zs_.code;
    }

    zs_.freeent = zs_.block_compress ? LZWParams::kFirst : 256;
    zs_.oldcode = CreateCompressCode();
    zs_.finchar = zs_.oldcode;
    if (zs_.finchar == -1) {
      return false;
    }

    out_buf_[out_buf_avail_++] = zs_.finchar;

    // Stack pointer at the end of the table.
    zs_.stackp = reinterpret_cast<uint8 *>(&zs_.htab[1 << LZWParams::kBits]);
    zs_.init = true;
  } else {
    goto noinit;
  }

  // Start reading lzw bitcodes from the input data stream.
  while (true) {
    zs_.code = CreateCompressCode();

    if (zs_.code == -1) {
      break;
    }

    // Table is saturated, clear.
    if ((zs_.code == LZWParams::kClear) && zs_.block_compress) {
      for (zs_.code = 255L; zs_.code >= 0; zs_.code--)
        zs_.codetab[zs_.code] = 0;
      zs_.clear_flag = true;
      zs_.freeent = LZWParams::kFirst - 1;
      zs_.code = CreateCompressCode();
      if (zs_.code == -1) {
        break;
      }
    }

    zs_.incode = zs_.code;

    // Special case for KwKwK string.
    if (zs_.code >= zs_.freeent) {
      *zs_.stackp++ = zs_.finchar;
      zs_.code = zs_.oldcode;
    }

    while (zs_.code >= 256) {
      if (zs_.stackp >= reinterpret_cast<void*>
          (&(zs_.htab[LZWParams::kHtabSize]))) {
        // If we get here, the input table was invalid. this input
        // could be trying to trigger a known vulnerability in several
        // lzw compress implementations, where the stack pointer continues
        // past the start of the htab.
        LOG(ERROR) << "Detected possible lzw compress exploit attempt.\n";

        // This isnt really eof, but let's not continue.
        return false;
      }
      *zs_.stackp++ = zs_.htab[zs_.code];
      zs_.code      = zs_.codetab[zs_.code];
    }

    *zs_.stackp++ = zs_.finchar = zs_.htab[zs_.code];

noinit:
    while (zs_.stackp > reinterpret_cast<void*>
        (&zs_.htab[1 << LZWParams::kBits])) {
      if (out_buf_avail_ >= avail_out_) {
        CHECK_EQ(out_buf_avail_, avail_out_);
        return true;
      }
      out_buf_[out_buf_avail_++] = *--(zs_.stackp);
    }

    // Generate new entry.
    if (out_buf_avail_) {
      if ((zs_.code = zs_.freeent) < zs_.maxmaxcode) {
        zs_.codetab[zs_.code] = zs_.oldcode;
        zs_.htab[zs_.code] = zs_.finchar;
        zs_.freeent = zs_.code + 1;
      }
      zs_.oldcode = zs_.incode;
    }
  }
  return (out_buf_avail_ > 0);
}
