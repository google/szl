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
#include <stdio.h>

#include "public/porting.h"
#include "public/logging.h"
#include "public/varint.h"

#include "engine/elfgen.h"


namespace sawzall {

// -----------------------------------------------------------------------------
// Implementation of ELFGen
//
// Specification documents:
//   http://refspecs.freestandards.org
//
//   ELF generic ABI:
//     http://refspecs.freestandards.org/elf/gabi4+/contents.html
//   ELF processor-specific supplement for X86_64:
//     http://refspecs.freestandards.org/elf/x86_64-SysV-psABI.pdf
//   DWARF 2.0:
//     http://refspecs.freestandards.org/dwarf/dwarf-2.0.0.pdf

enum {
  // various constant sizes for ELF files
  kAddrSize = sizeof(void*),
  kPageSize = 4*1024,  // memory mapping page size
  kTextAlign = 16,
  kELFHeaderSize = 40 + 3*kAddrSize,
  kProgramHeaderEntrySize = 8 + 6*kAddrSize,
  kSectionHeaderEntrySize = 16 + 6*kAddrSize,
  kSymbolSize = 8 + 2*kAddrSize,

  // our own layout of sections
  kUndef = 0,   // undef section
  kText,        // text section
  kShStrtab,    // section header string table
  kStrtab,      // string table
  kSymtab,      // symbol table
  kDbgAranges,  // debug address ranges
  kDbgInfo,     // debug info
  kDbgAbbrev,   // debug abbrev
  kDbgLine,     // debug line numbers

  kNumSections,  // num of section header entries in section header table

  // various ELF constants
  kELFCLASS32 = 1,
  kELFCLASS64 = 2,
  kELFDATA2LSB = 1,
  kEM_386 = 3,
  kEM_X86_64 = 62,
  kEV_CURRENT = 1,
  kET_EXEC = 2,
  kET_DYN = 3,
  kSHT_PROGBITS = 1,
  kSHT_SYMTAB = 2,
  kSHT_STRTAB = 3,
  kSHF_WRITE = 1,
  kSHF_ALLOC = 2,
  kSHF_EXECINSTR = 4,
  kSTB_LOCAL = 0,
  kSTT_FUNC = 2,

  // various DWARF constants
  kDW_VERSION = 2,
  kDW_TAG_compile_unit = 0x11,
  kDW_CHILDREN_no = 0,

  kDW_AT_name = 0x03,
  kDW_AT_stmtlist = 0x10,
  kDW_AT_low_pc = 0x11,
  kDW_AT_high_pc = 0x12,
  kDW_AT_comp_dir = 0x1b,

  kDW_FORM_addr = 0x01,
  kDW_FORM_data4 = 0x06,
  kDW_FORM_string = 0x08,

  kDW_LNS_copy = 1,
  kDW_LNS_advance_pc = 2,
  kDW_LNS_advance_line = 3,
  kDW_LNS_set_file = 4,
  kDW_LNS_set_column = 5,
  kDW_LNS_negate_stmt = 6,
  kDW_LNS_set_basic_block = 7,
  kDW_LNS_const_add_pc = 8,
  kDW_LNS_fixed_advance_pc = 9,

  kDW_LNE_end_sequence = 1,
  kDW_LNE_set_address = 2,
  kDW_LNE_define_file = 3,

  // our own definitions of DWARF parameters
  kMinimum_instruction_length = 1,
  kDefault_is_statement = 1,
  kLine_base = 0,
  kLine_range = 10,
  kOpcode_base = 10,
  kConst_pc_inc = (255 - kOpcode_base)/kLine_range*kMinimum_instruction_length,
};


// ELF and DWARF constants
static const char* kEI_MAG0_MAG3 = "\177ELF";
static const uint8 kSpecialOpcodeLengths[] = { 0, 1, 1, 1, 1, 0, 0, 0, 1 };


// Section attributes
// The field names correspond to the field names of Elf32_Shdr and Elf64_Shdr
static const struct {
  int shndx;  // section header index (only used to check correct section order)
  const string name;  // sh_name will be the index of name inserted in shstrtab
  int sh_type;
  int sh_flags;
  int sh_link;
  int sh_addralign;
  int sh_entsize;
} section_attr[kNumSections + 1] = {
  { kUndef,     "",               0,             0,                         0,       0,           0           },
  { kText,      ".text",          kSHT_PROGBITS, kSHF_ALLOC|kSHF_EXECINSTR, 0,       kTextAlign,  0           },
  { kShStrtab,  ".shstrtab",      kSHT_STRTAB,   0,                         0,       1,           0           },
  { kStrtab,    ".strtab",        kSHT_STRTAB,   0,                         0,       1,           0           },
  { kSymtab,    ".symtab",        kSHT_SYMTAB,   0,                         kStrtab, kAddrSize,   kSymbolSize },
  { kDbgAranges,".debug_aranges", kSHT_PROGBITS, 0,                         0,       2*kAddrSize, 0           },
  { kDbgInfo,   ".debug_info",    kSHT_PROGBITS, 0,                         0,       1,           0           },
  { kDbgAbbrev, ".debug_abbrev",  kSHT_PROGBITS, 0,                         0,       1,           0           },
  { kDbgLine,   ".debug_line",    kSHT_PROGBITS, 0,                         0,       1,           0           },
  // sentinel to pad the last section for proper alignment of section header table
  { 0,          "",               0,             0,                         0,       kAddrSize,   0           }
};


// convenience function aligning an integer
inline uintptr_t Align(uintptr_t x, size_t size) {
  // size is a power of 2
  assert((size & (size-1)) == 0);
  return (x + (size-1)) & ~(size-1);
}

void ELFGen::Buffer::WriteVarint(int value) {
  while (true) {
    uint8 byte = value & 0x7f;
    value >>= 7;  // arithmetic shift (with sign extension)
    if ((value == 0 && (byte & 0x40) == 0) ||
        (value == -1 && (byte & 0x40) != 0)) {
      WriteByte(byte);
      break;
    }
    WriteByte(byte | 0x80);
  }
}

void ELFGen::Buffer::WriteUnsignedVarint32(uint32 value) {
  char varint[kMaxUnsignedVarint32Length];
  char* end = EncodeUnsignedVarint32(varint, value);
  Write(varint, end - varint);
}

void ELFGen::Buffer::WriteUnsignedVarint64(uint64 value) {
  char varint[kMaxUnsignedVarint64Length];
  char* end = EncodeUnsignedVarint64(varint, value);
  Write(varint, end - varint);
}


// very inefficient, but not likely to be critical path
void ELFGen::Buffer::Prepend(const void* value, int length) {
  string end;
  end.swap(value_);
  value_.assign(reinterpret_cast<const char*>(value), length);
  value_.append(end);
}


// Constructor
ELFGen::ELFGen() : text_vma_(0), text_size_(0), text_padding_(0) {
  for (int i = 0; i < kNumSections; i++) {
    assert(section_attr[i].shndx == i);  // verify layout of sections
    section_name_[i] = AddSectionName(section_attr[i].name);
  }
  // section header string table always starts with an empty string, which is
  // the name of the kUndef section
  assert(section_attr[0].name.empty() && section_name_[0] == 0);

  // string table always starts with an empty string
  AddName("");
  assert(section_buf_[kStrtab].Length() == 1);

  // symbol at index 0 in symtab is always STN_UNDEF (all zero):
  Buffer& symtab = section_buf_[kSymtab];
  while (symtab.Length() < kSymbolSize)
    symtab.WriteInt(0);
  assert(symtab.Length() == kSymbolSize);

  // initial state for generation of DWARF line info
  file_names_ = new vector<const char*>;
  file_names_->push_back("");  // sentinel at index 0, first DWARF file index is 1
  cur_addr_ = 0;  // default pc
  cur_file_ = 0;  // file not set yet, point to sentinel
  cur_line_ = 1;  // default line
}


// Destructor
ELFGen::~ELFGen() {
  delete file_names_;
}


int ELFGen::AddString(Buffer& buf, const string& str) {
  const int str_index = buf.Length();
  buf.WriteString(str);
  return str_index;
}


int ELFGen::AddSectionName(const string& str) {
  return AddString(section_buf_[kShStrtab], str);
}


int ELFGen::AddName(const string& str) {
  return AddString(section_buf_[kStrtab], str);
}


void ELFGen::AddCode(const void* pc, size_t size,
                     uintptr_t* map_beg, uintptr_t* map_end, int* map_offset) {
  text_vma_ = reinterpret_cast<uintptr_t>(pc);
  text_size_ = size;
  // We pad the text section in the file to align absolute code addresses with
  // corresponding file offsets as if the code had been loaded by memory mapping
  if (text_vma_ % kPageSize < kELFHeaderSize)
    text_padding_ = text_vma_ % kPageSize + kPageSize - kELFHeaderSize;
  else
    text_padding_ = text_vma_ % kPageSize - kELFHeaderSize;

  section_buf_[kText].Write(pc, size);

  // map_beg is the address of the first mapped page
  if (map_beg != NULL)
    *map_beg = Align(text_vma_ - kPageSize + 1, kPageSize);

  // map_end is the address past the last mapped page
  if (map_end != NULL)
    *map_end = Align(text_vma_ + size, kPageSize);

  // map_offset is the file offset of the first mapped page
  if (map_offset != NULL)
   *map_offset = (kELFHeaderSize + text_padding_)/kPageSize*kPageSize;
}


int ELFGen::AddFunction(const string& name, const void* pc, size_t size) {
  assert(text_vma_ != 0);  // code must have been added
  Buffer& symtab = section_buf_[kSymtab];
  const int beg = symtab.Length();
  symtab.WriteInt(AddName(name));  // st_name
#if defined(__x86_64__)
  symtab.WriteShort((kSTB_LOCAL << 4) + kSTT_FUNC);  // st_info + (st_other<<8)
  symtab.WriteShort(kText);  // st_shndx
#endif
  symtab.WriteWord(reinterpret_cast<uword_t>(pc));  // st_value
  symtab.WriteWord(size);  // st_size
#if defined(__i386__)
  symtab.WriteShort((kSTB_LOCAL << 4) + kSTT_FUNC);  // st_info + (st_other<<8)
  symtab.WriteShort(kText);  // st_shndx
#endif
  assert(symtab.Length() - beg == kSymbolSize);
  return beg / kSymbolSize;  // symbol index in symtab
}


void ELFGen::AddLine(const char* file, int line, const void* pc) {
  uintptr_t addr = reinterpret_cast<uintptr_t>(pc);
  if (file != file_names_->at(cur_file_)) {
    // either first line in the sequence (cur_file_ == 0)
    // or first line in a different file (cur_file_ > 0)
    // search for this file in the list of files we have seen already
    int i = file_names_->size() - 1;
    // file at index 0 is not used
    while (i > 0 && file != file_names_->at(i))
      i--;

    if (i == 0) {
      // not found, add new file
      file_names_->push_back(file);
      cur_file_ = file_names_->size() - 1;
    } else {
      cur_file_ = i;
    }
    // emit statement setting the new file
    lineprog_.WriteByte(kDW_LNS_set_file);
    // same encoding as DWARF unsigned LEB128
    lineprog_.WriteUnsignedVarint32(cur_file_);
  }
  assert(file == file_names_->at(cur_file_) && cur_file_ > 0);

  // generate statements advancing to line and pc
  while (true) {
    if (line == cur_line_ && addr == cur_addr_) {
      lineprog_.WriteByte(kDW_LNS_copy);
      break;
    }
    // calculate and generate a special opcode if possible
    const int line_delta = line - cur_line_ - kLine_base;
    if (0 <= line_delta && line_delta < kLine_range) {
      assert(addr >= cur_addr_);  // pc cannot decrease in a line sequence
      const uintptr_t pc_delta = addr - cur_addr_;
      if (pc_delta <= kConst_pc_inc) {
        const int opcode = line_delta + kLine_range*pc_delta + kOpcode_base;
        if (opcode <= 255) {
          lineprog_.WriteByte(opcode);
          break;
        }
      }
      // pc delta is too large for a special opcode
      // use a standard opcode to advance pc first and try again
      AdvancePc(addr, false);
      continue;
    }
    // line delta is out of range for a special opcode
    // use a standard opcode to advance line first and try again
    AdvanceLine(line);
  }
  cur_addr_ = addr;
  cur_line_ = line;
}


void ELFGen::EndLineSequence(const void* pc) {
  // advance current pc addr to exactly pc
  AdvancePc(reinterpret_cast<uintptr_t>(pc), true);

  // emit extended opcode to end sequence
  lineprog_.WriteByte(0);  // extended opcode prefix
  lineprog_.WriteByte(1);  // extended opcode length
  lineprog_.WriteByte(kDW_LNE_end_sequence);
  cur_addr_ = 0;
  cur_file_ = 0;
  cur_line_ = 1;
}


// AdvancePc does not guarantee cur_addr_ == addr, unless exact == true
void ELFGen::AdvancePc(uintptr_t addr, bool exact) {
  assert(kMinimum_instruction_length == 1);  // code does not scale address increments
  const uintptr_t pc_delta = addr - cur_addr_;
  // use kDW_LNS_const_add_pc when possible to reduce opcode size
  if (kConst_pc_inc <= pc_delta && pc_delta < 2*kConst_pc_inc &&
      (!exact || kConst_pc_inc == pc_delta)) {
    lineprog_.WriteByte(kDW_LNS_const_add_pc);
    cur_addr_ += kConst_pc_inc;
    return;
  }
  lineprog_.WriteByte(kDW_LNS_advance_pc);
  // In 64-bit mode, pc_delta may not fit in 32 bits, use 64-bit version
  // (it yields the same result for small values).
  // same encoding as DWARF unsigned LEB128
  lineprog_.WriteUnsignedVarint64(pc_delta);
  cur_addr_ = addr;
}


void ELFGen::AdvanceLine(int line) {
  lineprog_.WriteByte(kDW_LNS_advance_line);
  int delta = line - cur_line_;
  // Note that this one place we use signed varint, not unsigned
  lineprog_.WriteVarint(delta);
  cur_line_ = line;
}


void ELFGen::AddDebugSections(const string& comp_unit_name, const string& comp_dir) {
  // debug address ranges section
  Buffer& aranges = section_buf_[kDbgAranges];

  // length prepended later
  aranges.WriteShort(kDW_VERSION);  // version
  aranges.WriteInt(0);  // offset into .debug_info
  aranges.WriteByte(kAddrSize);  // size of address
  aranges.WriteByte(0);  // size of segment, 0 == flat address space
  // each address tuple must be aligned in the ELF file
  // the beginning of the section is aligned
  PadSection(aranges, sizeof(uint32), 2*kAddrSize);
  aranges.WriteWord(text_vma_);
  aranges.WriteWord(text_size_);
  aranges.WriteWord(0);  // terminating tuple (addr part)
  aranges.WriteWord(0);  // terminating tuple (size part)
  const uint32 aranges_length = aranges.Length();
  aranges.Prepend(&aranges_length, sizeof(uint32));


  // debug info section
  Buffer& info = section_buf_[kDbgInfo];

  // compilation unit header
  // length prepended later
  info.WriteShort(kDW_VERSION);  // version
  info.WriteInt(0);  // offset into .debug_abbrev
  info.WriteByte(kAddrSize);  // size of address

  // compilation unit
  info.WriteUnsignedVarint32(1);  // our only abbreviation tag
  info.WriteInt(0);  // DW_AT_stmtlist: offset into .debug_line section
  const uintptr_t high_pc = text_vma_ + text_size_;
  info.WriteWord(high_pc);  // DW_AT_high_pc
  info.WriteWord(text_vma_);  // DW_AT_low_pc
  info.WriteString(comp_unit_name); // DW_AT_name
  info.WriteString(comp_dir);  // DW_AT_comp_dir
  const uint32 info_length = info.Length();
  info.Prepend(&info_length, sizeof(uint32));


  // debug abbrev section
  Buffer& abbrev = section_buf_[kDbgAbbrev];

  abbrev.WriteUnsignedVarint32(1);  // first and only abbreviation tag we define
  abbrev.WriteUnsignedVarint32(kDW_TAG_compile_unit);
  abbrev.WriteByte(kDW_CHILDREN_no);

  abbrev.WriteUnsignedVarint32(kDW_AT_stmtlist);
  abbrev.WriteUnsignedVarint32(kDW_FORM_data4);

  abbrev.WriteUnsignedVarint32(kDW_AT_high_pc);
  abbrev.WriteUnsignedVarint32(kDW_FORM_addr);

  abbrev.WriteUnsignedVarint32(kDW_AT_low_pc);
  abbrev.WriteUnsignedVarint32(kDW_FORM_addr);

  abbrev.WriteUnsignedVarint32(kDW_AT_name);
  abbrev.WriteUnsignedVarint32(kDW_FORM_string);

  abbrev.WriteUnsignedVarint32(kDW_AT_comp_dir);
  abbrev.WriteUnsignedVarint32(kDW_FORM_string);

  abbrev.WriteUnsignedVarint32(0);  // terminating name
  abbrev.WriteUnsignedVarint32(0);  // terminating form


  // debug line section
  Buffer& line = section_buf_[kDbgLine];

  // write statement program prologue
  // skip total_length, version, and prologue_length fields for now, prepended later
  line.WriteShort(kMinimum_instruction_length + 256*kDefault_is_statement);
  line.WriteShort(kLine_base + 256*kLine_range);
  line.WriteByte(kOpcode_base);
  assert(sizeof(kSpecialOpcodeLengths) == kOpcode_base - 1);
  line.Write(kSpecialOpcodeLengths, sizeof(kSpecialOpcodeLengths));
  line.WriteByte(0);  // end of include_directories
  // list file names, skipping sentinel at index 0
  for (int i = 1; i < file_names_->size(); i++) {
    const char* file_name = file_names_->at(i);
    line.Write(file_name, strlen(file_name) + 1);  // including terminating '\0'
    line.WriteUnsignedVarint32(0);  // directory index
    line.WriteUnsignedVarint32(0);  // time stamp unknown
    line.WriteUnsignedVarint32(0);  // file length unknown
  }
  line.WriteByte(0);  // end of file_names
  const uint32 prologue_length = line.Length();
  line.Prepend(&prologue_length, sizeof(uint32));
  const uint16 version = kDW_VERSION;
  line.Prepend(&version, sizeof(uint16));  // DWARF version
  const uint32 total_length = line.Length() + lineprog_.Length();
  line.Prepend(&total_length, sizeof(uint32));
  line.WriteBuffer(lineprog_);
  assert(line.Length() == sizeof(uint32) + total_length);
}


void ELFGen::AddELFHeader(int shoff) {
  assert(text_vma_ != 0);  // code must have been added
  header_.Write(kEI_MAG0_MAG3, 4);  // EI_MAG0..EI_MAG3
 #if defined(__i386__)
  header_.WriteByte(kELFCLASS32);  // EI_CLASS
#elif defined(__x86_64__)
  header_.WriteByte(kELFCLASS64);  // EI_CLASS
#endif
  header_.WriteByte(kELFDATA2LSB);  // EI_DATA
  header_.WriteByte(kEV_CURRENT);  // EI_VERSION
  header_.WriteByte(0);  // EI_PAD
  header_.WriteInt(0);  // EI_PAD
  header_.WriteInt(0);  // EI_PAD
  header_.WriteShort(kET_DYN);  // e_type, fake a shared object
#if defined(__i386__)
  header_.WriteShort(kEM_386);  // e_machine
#elif defined(__x86_64__)
  header_.WriteShort(kEM_X86_64);  // e_machine
#endif
  header_.WriteInt(kEV_CURRENT);  // e_version
  header_.WriteWord(0);  // e_entry: none
  header_.WriteWord(0);  // e_phoff: no program header table
  header_.WriteWord(shoff);  // e_shoff: section header table offset
  header_.WriteInt(0);  // e_flags: no flags
  header_.WriteShort(kELFHeaderSize);  // e_ehsize: header size
  header_.WriteShort(kProgramHeaderEntrySize);  // e_phentsize
  header_.WriteShort(0);  // e_phnum: no entries program header table
  header_.WriteShort(kSectionHeaderEntrySize);  // e_shentsize
  header_.WriteShort(kNumSections);  // e_shnum: number of section header entries
  header_.WriteShort(kShStrtab);  // e_shstrndx: index of shstrtab
  assert(header_.Length() == kELFHeaderSize);
}


void ELFGen::AddSectionHeader(int section, int offset) {
  sheaders_.WriteInt(section_name_[section]);
  sheaders_.WriteInt(section_attr[section].sh_type);
  sheaders_.WriteWord(section_attr[section].sh_flags);
  sheaders_.WriteWord((section == kText) ? text_vma_ : 0);  // sh_addr: abs addr
  sheaders_.WriteWord(offset);  // sh_offset: section file offset
  sheaders_.WriteWord(section_buf_[section].Length());
  sheaders_.WriteInt(section_attr[section].sh_link);
  sheaders_.WriteInt(0);
  sheaders_.WriteWord(section_attr[section].sh_addralign);
  sheaders_.WriteWord(section_attr[section].sh_entsize);
  assert(sheaders_.Length() == kSectionHeaderEntrySize * (section + 1));
}


// Pads the given section with zero bytes for the given aligment, assuming the
// section starts at given file offset; returns file offset after padded section
int ELFGen::PadSection(Buffer& section, int offset, int alignment) {
  offset += section.Length();
  int aligned_offset = Align(offset, alignment);
  while (offset++ < aligned_offset)
    section.WriteByte(0);  // one byte padding
  return aligned_offset;
}


// write given section to file and return written size
int ELFGen::WriteSection(FILE* fp, Buffer& section) {
  int size = section.Length();
  CHECK(fwrite(section.Data(), 1, size, fp) == size);
  return size;
}


bool ELFGen::WriteFile(const char* filename) {
  FILE* fp = fopen(filename, "w");
  if (fp == NULL)
    return false;

  // add a terminating symbol expected by pprof
  AddFunction("_end", reinterpret_cast<void*>(text_vma_ + text_size_), 0);

  // dump the accumulated line information and file names into debug sections
  AddDebugSections("", "");  // works without specifying current dir name

  // Align all sections before writing the ELF header in order to calculate the
  // file offset of the section header table, which is needed in the ELF header.
  // Pad each section as required by the aligment constraint of the immediately
  // following section, except the ELF header section, which requires special
  // padding (text_padding_) to align the text_ section.
  int offset = kELFHeaderSize + text_padding_;
  for (int i = kText; i < kNumSections; i++)
    offset = PadSection(section_buf_[i], offset, section_attr[i+1].sh_addralign);

  const int shoff = offset;  // section header table offset

  // write elf header
  AddELFHeader(shoff);
  offset = WriteSection(fp, header_);

  // pad file before writing text section in order to align vma with file offset
  for (int i = 0; i < text_padding_; i++)
    CHECK(fwrite("\0", 1, 1, fp) == 1);

  offset += text_padding_;
  assert((text_vma_ - offset) % kPageSize == 0);

  // section header at index 0 in section header table is always SHN_UNDEF:
  for (int i = 0; i < kNumSections; i++) {
    AddSectionHeader(i, offset);
    offset += WriteSection(fp, section_buf_[i]);
  }
  // write section header table
  assert(offset == shoff);
  offset += WriteSection(fp, sheaders_);
  assert(offset == shoff + kNumSections * kSectionHeaderEntrySize);

  fclose(fp);

  return true;
}

} // namespace sawzall
