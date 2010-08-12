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

#include <assert.h>
#include <stdio.h>
#include <algorithm>
#include <string>

#include <zlib.h>
#include <zconf.h>

using namespace std;


#include "public/porting.h"
#include "public/logging.h"

#include "utilities/strutils.h"
#include "utilities/zlibwrapper.h"


// --------- COMPRESS ---------

// According to the zlib manual, when you Compress, the destination
// buffer must have size at least src + .1%*src + 12.  This function
// helps you calculate that.  Augment this with a few bytes of slack.
uLongf ZLibMinCompressbufSize(int input_size) {
  return input_size + input_size/1000 + 40;
}

int ZLibCompress(bool no_header_mode, unsigned char* dest, int dest_avail,
                 int *dest_len, const unsigned char* source, int source_len) {
  z_stream comp_stream;
  comp_stream.next_in = (Bytef*) source;
  comp_stream.avail_in = (uInt) source_len;
  comp_stream.next_out = (Bytef*) dest;
  comp_stream.avail_out = (uInt) dest_avail;

  comp_stream.zalloc = (alloc_func)0;
  comp_stream.zfree = (free_func)0;
  comp_stream.opaque = (voidpf)0;
  int err =  deflateInit2(&comp_stream,
                          Z_DEFAULT_COMPRESSION,
                          Z_DEFLATED,
                          no_header_mode ? -MAX_WBITS : MAX_WBITS,
                          8,  // default mem level
                          Z_DEFAULT_STRATEGY);
  if (err != Z_OK) return err;

  // This is used to figure out how many bytes we wrote *this chunk*
  int compressed_size = comp_stream.total_out;

  err = deflate(&comp_stream, Z_FINISH);  // Z_FINISH for all mode

  const uLong source_bytes_consumed = source_len - comp_stream.avail_in;

  if ((err == Z_STREAM_END || err == Z_OK)
      && comp_stream.avail_in == 0
      && comp_stream.avail_out != 0 ) {
    // we processed everything ok and the output buffer was large enough.
    ;
  } else if (err == Z_STREAM_END && comp_stream.avail_in > 0) {
    deflateEnd(&comp_stream);
    return Z_BUF_ERROR;                            // should never happen
  } else if (err != Z_OK && err != Z_STREAM_END && err != Z_BUF_ERROR) {
    // an error happened
    deflateEnd(&comp_stream);
    return err;
  } else if (comp_stream.avail_out == 0) {     // not enough space
    err = Z_BUF_ERROR;
  }

  assert(err == Z_OK || err == Z_STREAM_END || err == Z_BUF_ERROR);
  if (err == Z_STREAM_END)
    err = Z_OK;

  // update the size
  compressed_size = comp_stream.total_out - compressed_size;  // delta
  *dest_len = compressed_size;

  deflateEnd(&comp_stream);
  return err;
}


// --------- UNCOMPRESS ---------

const int kGzipFooterSize = 8;

int ZLibUncompress(bool no_header_mode, int buf_size, string *dest,
                   const unsigned char* source, int source_len) {
  if ( source_len == 0 )
    return Z_OK;
  z_stream uncomp_stream;

  uncomp_stream.zalloc = (alloc_func)0;
  uncomp_stream.zfree = (free_func)0;
  uncomp_stream.opaque = (voidpf)0;
  int wbits = (no_header_mode ? - MAX_WBITS : MAX_WBITS);
  int err =  inflateInit2(&uncomp_stream, wbits);
  if (err != Z_OK) return err;

  uncomp_stream.next_in = (Bytef*) source;
  uncomp_stream.avail_in = (uInt) source_len;
  char* buffer = new char[buf_size];

  while (true) {
    uncomp_stream.next_out = (Bytef*) buffer;
    uncomp_stream.avail_out = (uInt) buf_size;

    const uLong old_total_out = uncomp_stream.total_out;
    const uLong old_total_in = uncomp_stream.total_in;
    const uLong old_avail_in = uncomp_stream.avail_in;
    err = inflate(&uncomp_stream, Z_SYNC_FLUSH);  // Z_SYNC_FLUSH for chunked mode
    const uLong bytes_read = uncomp_stream.total_in - old_total_in;
    const uLong bytes_written = uncomp_stream.total_out - old_total_out;
    CHECK_LE(bytes_read, old_avail_in);

    if ((err == Z_STREAM_END || err == Z_OK) && uncomp_stream.avail_in == 0) {
      // everything went ok and we read it all; copy the data and stop
      dest->append(buffer, bytes_written);
      break;
    } else if (err == Z_STREAM_END && uncomp_stream.avail_in > 0) {
      LOG(WARNING)
        << "Uncompress: Received some extra data, bytes total: "
        << uncomp_stream.avail_in << " bytes";
      inflateEnd(&uncomp_stream);
      delete [] buffer;
      return Z_DATA_ERROR;       // what's the extra data for?
    } else if (err != Z_OK && err != Z_STREAM_END && err != Z_BUF_ERROR) {
      LOG(WARNING) << "Uncompress: Error: " << err
                   << " avail_out: " << uncomp_stream.avail_out;
      inflateEnd(&uncomp_stream);
      delete [] buffer;
      return err;
    } else if (uncomp_stream.avail_out == 0) {
      // copy the data and keep going
      dest->append(buffer, bytes_written);
    }
  }

  inflateEnd(&uncomp_stream);
  delete [] buffer;
  return Z_OK;
}
