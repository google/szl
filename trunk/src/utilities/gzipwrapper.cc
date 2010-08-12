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

#include <algorithm>
#include <string>

#include "zlib.h"

#include "public/porting.h"
#include "utilities/strutils.h"

#include "utilities/gzipwrapper.h"
#include "utilities/lzw.h"


const int kBufferSize = 4092;

// magic numbers

namespace GZipParams {
  const char magic[] = { 0x1f, 0x8b };
  const int HEADER_SIZE = 10;
  const int FOOTER_SIZE = 8;
  const int MAGIC_SIZE  = 2;

 // flags
  const int ASCII_FLAG   = 0x01;  // bit 0 set: file probably ascii text
  const int HEAD_CRC     = 0x02;  // bit 1 set: header CRC present
  const int EXTRA_FIELD  = 0x04;  // bit 2 set: extra field present
  const int ORIG_NAME    = 0x08;  // bit 3 set: original file name present
  const int COMMENT      = 0x10;  // bit 4 set: file comment present
  const int RESERVED     = 0xE0;  // bits 5..7: reserved

  const int kCompressedBufferSize = 64 * 1024;
  const int kPlaintextBufferSize = 2 * kCompressedBufferSize;
}


// Parameters related to zlib stream format. See RFC 1950 for details.
namespace ZlibParams {
 const int HEADER_SIZE = 2;
}

namespace CompressParams {
  // compress magic header
  const char magic[] = { 0x1f, 0x9d };

  const int HEADER_SIZE = 3;
  const int MAGIC_SIZE  = 2;

  const int MASK_CODELEN  = 0x1f;  // # compression bits (ie, length of codes)
  const int MASK_EXTENDED = 0x20;  // unused, could mean 4th hdr byte is present
  const int MASK_RESERVED = 0x40;  // unused
  const int MASK_BLOCK    = 0x80;  // block compression used

  const int kMaxMaxBits = 16;
}


// ========== Decompression ==========



static bool DoGZipUncompress(const unsigned char* source, int source_len,
                             string* dest);
static bool DoZlibUncompress(const unsigned char* source, int source_len,
                             string* dest);
static bool DoLZWUncompress(const unsigned char* source, int source_len,
                            string* dest);
static bool DoGZipOrZlibUncompress(const unsigned char* source, int source_len,
                                   string* dest, bool zlib);


bool GunzipString(const unsigned char* source, int source_len, string* dest) {
  // check for gzip archive
  if (source_len >= GZipParams::MAGIC_SIZE &&
      memcmp(source, GZipParams::magic, GZipParams::MAGIC_SIZE) == 0)
    return DoGZipUncompress(source, source_len, dest);

  // not gzip, LZW?
  if (source_len >= CompressParams::MAGIC_SIZE &&
      memcmp(source, CompressParams::magic, CompressParams::MAGIC_SIZE) == 0)
    return DoLZWUncompress(source, source_len, dest);

  // zlib format
  if (source_len >= ZlibParams::HEADER_SIZE)
    return DoZlibUncompress(source, source_len, dest);

  // unrecognised archive
  return false;
}


static bool DoGZipUncompress(const unsigned char* source, int source_len,
                             string* dest) {
  // Process and skip the header, then use common code.
  unsigned const char* flags = source;
  source += GZipParams::HEADER_SIZE;
  source_len -= GZipParams::HEADER_SIZE;

  if (flags[2] != Z_DEFLATED)
    return false;

  if ((flags[3] & GZipParams::EXTRA_FIELD) != 0) {  // skip the extra field
    if (source_len < 2)
      return false;
    source += 2;
    source_len -= 2;
  }

  if ((flags[3] & GZipParams::ORIG_NAME) != 0) {  // skip the original file name
    const void* p = memchr(source, '\0', source_len);
    if (p == NULL)
      return false;
    source_len -= ((unsigned char*)p - source + 1);
    source = (unsigned char*)p + 1;
  }

  if ((flags[3] & GZipParams::COMMENT) != 0) {  // skip the comment
    const void* p = memchr(source, '\0', source_len);
    if (p == NULL)
      return false;
    source_len -= ((unsigned char*)p - source + 1);
    source = (unsigned char*)p + 1;
  }

  if ((flags[3] & GZipParams::HEAD_CRC) != 0) {  // skip the header CRC
    if (source_len < 2)
      return false;
    source += 2;
    source_len -= 2;
  }

  return DoGZipOrZlibUncompress(source, source_len, dest, false);
}

static bool DoZlibUncompress(const unsigned char* source, int source_len,
                             string* dest) {
  // Process the header but do not skip it, then use common code.
  const unsigned char* header = source;

  if ((header[0] & 0x0F) != Z_DEFLATED)  // check compression method
    return false;

  if ((((header[0] & 0xF0) >> 4) + 8) > MAX_WBITS)  // check window size
    return false;

  if (((header[0] << 8) + header[1]) % 31)  // test check bits
    return false;

  return DoGZipOrZlibUncompress(source, source_len, dest, true);
}

static bool DoLZWUncompress(const unsigned char* source, int source_len,
                            string* dest) {
  // Process and skip the header.
  const unsigned char* flags = source;
  source += CompressParams::HEADER_SIZE;
  source_len -= CompressParams::HEADER_SIZE;

  int32 maxbits       = flags[2] & CompressParams::MASK_CODELEN;
  bool block_compress = flags[2] & CompressParams::MASK_BLOCK;

  if ((flags[2] & CompressParams::MASK_EXTENDED) != 0)
    // unsupported, we can probably safely ignore the
    // reserved flag, but if extended flag is present,
    // there may be an extra header byte which will
    // desynchronise our stream.
    return false;

  if (maxbits > CompressParams::kMaxMaxBits)
    return false;

  // just a header? valid I guess.
  if (source_len ==  0)
    return true;

  // Uncompress.
  LZWInflate zs(maxbits, block_compress);
  char buffer[kBufferSize];

  while (source_len > 0) {
    zs.Input(source, source_len);
    int out_len = zs.Inflate(buffer, sizeof(buffer));
    int in_len = source_len - zs.Tell();

    if (out_len < 0)
      return false;

    source += in_len;
    source_len -= in_len;
    dest->append(buffer, out_len);
  }
  return !dest->empty();
}


static bool DoGZipOrZlibUncompress(const unsigned char* source, int source_len,
                                   string* dest, bool zlib) {
  char buffer[kBufferSize];
  uint32 current_crc = 0;

  // gzip format is indicated by negative window bits.
  int window_bits = MAX_WBITS;
  if (!zlib)
    window_bits = -window_bits;

  z_stream zstream;
  memset(&zstream, 0, sizeof(zstream));
  if (inflateInit2(&zstream, window_bits) != Z_OK)
    return false;
  zstream.next_in = (Bytef*) source;
  zstream.avail_in = source_len;

  while (true) {
    if (zstream.avail_in == 0)
      return false;

    zstream.next_out = (Bytef*)buffer;
    zstream.avail_out = sizeof(buffer);
    int status = inflate(&zstream, Z_NO_FLUSH);
    if (status != Z_OK && status != Z_STREAM_END)
      return false;
    int out_len = (char*)zstream.next_out - buffer;
    dest->append(buffer, out_len);
    current_crc = crc32(current_crc, (Bytef*)buffer, out_len);

    if (status == Z_STREAM_END)
      break;
  }

  // Skip footer processing if input is zlib format.
  if (zlib)
    return true;

  if (zstream.avail_in < GZipParams::FOOTER_SIZE)
    return false;
  const Bytef *p = zstream.next_in;
  uint32 crc = p[0] | (p[1] << 8) | (p[2] << 16) | (uint32(p[3]) << 24);
  uint32 len = p[4] | (p[5] << 8) | (p[6] << 16) | (uint32(p[7]) << 24);

  // Length is stored in the footer as actual uncompressed length mod 2^32
  if (current_crc != crc || len != (zstream.total_out & 0xffffffffULL) ||
      inflateEnd(&zstream) != Z_OK)
    return false;
  return true;
}


// ========= Compression ==========

bool GzipString(const unsigned char* source, int source_len,
                string* dest, int compressionLevel) {
  // Write header.

  char header[GZipParams::HEADER_SIZE];
  memset(header, 0, sizeof header);
  header[0] = GZipParams::magic[0];
  header[1] = GZipParams::magic[1];
  header[2] = Z_DEFLATED;
  dest->append(header, sizeof(header));

  // Initialize compression stream.

  z_stream zstream;
  memset(&zstream, 0, sizeof(zstream));
  // Pass -MAX_WBITS to indicate no zlib header
  if (deflateInit2(&zstream, compressionLevel, Z_DEFLATED, -MAX_WBITS,
                   9, Z_DEFAULT_STRATEGY) != Z_OK)
    return false;

  // Compress data.

  zstream.next_in = (Bytef*) source;
  zstream.avail_in = source_len;
  uint32 crc = crc32(0, reinterpret_cast<const Bytef*>(source), source_len);
  char buffer[kBufferSize];
  while (true) {
    zstream.next_out = (Bytef*) buffer;
    zstream.avail_out = sizeof(buffer);
    int status = deflate(&zstream, Z_FINISH);
    if (status != Z_OK && status != Z_STREAM_END)
      return false;
    int length = zstream.next_out - reinterpret_cast<Bytef*>(buffer);
    dest->append(buffer, length);
    if (status == Z_STREAM_END)
      break;
  }

  // Clean up compression stream.

  if (deflateEnd(&zstream) != Z_OK)
    return false;

  // Write footer.

  char footer[GZipParams::FOOTER_SIZE];
  footer[0] = crc & 0xff;
  footer[1] = (crc >>  8) & 0xff;
  footer[2] = (crc >> 16) & 0xff;
  footer[3] = (crc >> 24) & 0xff;
  int len = source_len;
  footer[4] = len & 0xff;
  footer[5] = (len >>  8) & 0xff;
  footer[6] = (len >> 16) & 0xff;
  footer[7] = (len >> 24) & 0xff;
  dest->append(footer, sizeof(footer));

  return true;
}
