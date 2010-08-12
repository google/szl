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

#ifndef UTIL_LZW_LZW_H__
#define UTIL_LZW_LZW_H__

// A class to handle decompressing LZW streams, as found in .Z archives.


class LZWInflate {
 public:
  // Constructor, must specify block mode and maxbits.
  // `block` indicates that the tables should be flushed when saturated.
  // `maxbits` is the maximum code length, the code length starts at 9
  // and continues until this length as they're used up.
  //
  // There is no way to derive these values from the LZW stream itself,
  // So they are usually provided in the header or specifications for the
  // stream you are decompressing.
  //
  // For .Z archives, these values can be determined by masking out the code
  // bits from the third byte of the archive.
  LZWInflate(int32 maxbits, bool block);
  ~LZWInflate() {
    delete [] zs_.htab;
    delete [] zs_.codetab;
  }

  // Inflate at most avail bytes to out, return number of bytes written.
  int64 Inflate(void* out, int64 avail);

  // Read at most avail bytes of deflated data from in, return true if valid.
  bool Input(const void* in, int64 avail);

  // Return how many bytes of input have not yet been consumed.
  int64 Tell() const;

 private:
  // Fills the output buffer by uncompressing the input buffer.  Returns
  // true if we're not at the end of the file.
  bool FillOutputBuffer();

  // Get a compression code from the input file.
  int32 CreateCompressCode();

  // LZW compression state (used for .Z archives).
  typedef struct {
    bool    init;        // Initialised flag.
    bool    clear_flag;  // Clear tables.
    int32   n_bits;      // Current code length.
    int32   maxbits;     // Maximum code length.
    int32   maxcode;     // Maximum code.
    int32   maxmaxcode;  // Maximum Maximum code.
    int32*  htab;        // Tables.
    uint16* codetab;
    int32   freeent;     // Free table entries.
    bool    block_compress;  // Block compress mode?
    int32   code;        // Current lzw code.
    int32   oldcode;     // Codes used to maintain state.
    int32   incode;
    int32   finchar;
    int32   offset;      // Bit offset that next code starts at.
    int32   loctets;     // number of octets missing from offset.
    uint8*  stackp;      // Stack Pointer.
  } z_state;

  z_state zs_;
  const int8* next_in_;  // Next input byte.
  uint8* out_buf_;       // Ouput buffer.
  int64 avail_in_;       // Available bytes of input.
  int64 avail_out_;      // Available bytes in output buf.
  int64 out_buf_avail_;  // Space used in output buffer.
};

#endif  // UTIL_LZW_LZW_H__
