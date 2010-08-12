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

#include <string>
#include <vector>


namespace sawzall {

// ELFGen generates a minimal ELF file containing code, symbols, and line
// number information for the generated Sawzall code. The generated ELF
// file is not executed, but read by pprof to analyze Sawzall profiles.

class ELFGen {
 public:
  ELFGen();
  ~ELFGen();

  // Buffer implemented as a string.  Could be improved.
  class Buffer {
   public:
    Buffer()  { }
    const void* Data() const  {
      return reinterpret_cast<const void*>(value_.data());
    }
    size_t Length() const  { return value_.length(); }
    void Write(const void* data, int length) {
      value_.append(reinterpret_cast<const char*>(data), length);
    }
    void WriteByte(uint8 value)  { Write(&value, sizeof(value)); }
    void WriteShort(uint16 value)  { Write(&value, sizeof(value)); }
    void WriteInt(int value)  { Write(&value, sizeof(value)); }
    // the word is 32-bit wide in 32-bit mode and 64-bit wide in 64-bit mode
    void WriteWord(uword_t value)  { Write(&value, sizeof(value)); }
    void WriteVarint(int value);
    void WriteUnsignedVarint32(uint32 value);
    void WriteUnsignedVarint64(uint64 value);
    // including trailing null byte
    void WriteString(const string& value) {
      Write(value.c_str(), value.length() + 1);
    }
    void WriteBuffer(const Buffer& value) {
      Write(value.Data(), value.Length());
    }
    void Prepend(const void* value, int length);
   private:
    string value_;
  };

  // Code
  // map_beg, map_end, and map_offset (if non-NULL) are set to describe where
  // the text section of the generated ELF file would be mapped in memory;
  // these values are normally found in /proc/self/map for loaded libraries
  void AddCode(const void* pc, size_t size,
               uintptr_t* map_beg, uintptr_t* map_end, int* map_offset);

  // Symbols
  int AddFunction(const string& name, const void* pc, size_t size);

  // Line info
  void AddLine(const char* file, int line, const void* pc);
  void EndLineSequence(const void* pc);

  // Write file to disk, returns true on success
  bool WriteFile(const char* filename);

 private:
  // DWARF helpers
  void AdvancePc(uintptr_t addr, bool exact);
  void AdvanceLine(int line);
  void AddDebugSections(const string& comp_unit_name, const string& comp_dir);

  // ELF helpers
  int AddString(Buffer& buf, const string& str);
  int AddSectionName(const string& str);
  int AddName(const string& str);
  void AddELFHeader(int shoff);
  void AddSectionHeader(int section, int offset);
  int PadSection(Buffer& section, int offset, int alignment);
  int WriteSection(FILE* fp, Buffer& section);

  uintptr_t text_vma_;  // text section vma
  size_t text_size_;  // text section size
  int text_padding_;  // padding preceding text section

  static const int kNumSections = 9;  // we generate 9 sections
  int section_name_[kNumSections];  // array of section name indices
  Buffer section_buf_[kNumSections];  // array of section buffers
  Buffer header_;  // ELF header buffer
  Buffer sheaders_;  // section header table buffer
  Buffer lineprog_;  // line statement program, part of '.debug_line' section

  // current state of the DWARF line info generator
  uintptr_t cur_addr_;  // current pc
  int cur_file_;  // index in file_names_ of current file
  int cur_line_;  // current line in current file
  vector<const char*>* file_names_;  // list of file names
};

} // namespace sawzall
