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
#include <string>

namespace sawzall {


class RecordReader {
 private:
  RecordReader(FILE* file) : file_(file), buffer_(NULL), buffer_size_(0) { }
 public:
  ~RecordReader() {
    if (file_ != NULL)
      fclose(file_);
    if (buffer_ != NULL)
      delete [] buffer_;
  }
  static RecordReader* Open(const char* filename);
  bool Read(char** record_ptr, size_t* record_size);
  bool Eof() const { return feof(file_); }
  const string& error_message() const { return error_message_; }

 private:
  FILE* file_;
  char* buffer_;
  size_t buffer_size_;
  string error_message_;
};


class RecordWriter {
 private:
  RecordWriter(FILE* file) : file_(file) { }
 public:
  ~RecordWriter() {
    if (file_ != NULL) {
      fflush(file_);
      fclose(file_);
    }
  }
  static RecordWriter* Open(const char* filename);
  bool Write(const char* record_ptr, size_t record_size);
  const string& error_message() const { return error_message_; }

 private:
  FILE* file_;
  string error_message_;
};


}  // namespace sawzall
