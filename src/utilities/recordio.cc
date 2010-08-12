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

// Simple record reader and writer.

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <string>

#include "public/porting.h"
#include "public/recordio.h"
#include "public/varint.h"


namespace sawzall {


RecordReader* RecordReader::Open(const char* filename) {
  FILE* file = fopen(filename, "r");
  if (file != NULL)
    return new RecordReader(file);
  else
    return NULL;
}


bool RecordReader::Read(char** record_ptr, size_t* record_size) {
  char prefix[kMaxUnsignedVarint64Length];
  char* prefix_end = prefix + sizeof(prefix);
  char* end = prefix;
  int next;
  do {
    if (end == prefix_end) {
      error_message_ = "Corrupt record length";
      return false;
    }
    next = fgetc(file_);
    if (next == EOF) {
      if (end != prefix)
        error_message_ = "Corrupt record length at EOF";
      return false;
    }
    *end++ = next;
  } while (next >= 128);
  uint64 size;
  DecodeUnsignedVarint64(prefix, &size);
  if (implicit_cast<size_t>(size) != size) {
    error_message_ = "Corrupt record length";
    return false;
  }
  if (size > buffer_size_) {
    delete [] buffer_;
    buffer_ = new char[size];
    buffer_size_ = size;
  }
  *record_ptr = buffer_;
  *record_size = size;
  if (fread(buffer_, 1, size, file_) == size)
    return true;
  if (feof(file_)) {
    error_message_ = "EOF in the middle of a record";
    return false;
  }
  char buffer[1024];
  strerror_r(errno, buffer, sizeof(buffer));
  error_message_ = buffer;
  return false;
}


RecordWriter* RecordWriter::Open(const char* filename) {
  FILE* file = fopen(filename, "w");
  if (file != NULL)
    return new RecordWriter(file);
  else
    return NULL;
}


bool RecordWriter::Write(const char* record_ptr, size_t record_size) {
  char prefix[kMaxUnsignedVarint64Length];
  char* end = EncodeUnsignedVarint64(prefix, record_size);
  int prefix_size = end - prefix;
  if (fwrite(prefix, 1, prefix_size, file_) != prefix_size)
    return false;
  if (fwrite(record_ptr, 1, record_size, file_) == record_size)
    return true;
  char buffer[1024];
  strerror_r(errno, buffer, sizeof(buffer));
  error_message_ = buffer;
  return false;
}


}  // namespace sawzall
