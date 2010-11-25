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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

#include "engine/globals.h"
#include "public/logging.h"
#include "public/hashutils.h"
#include "public/varint.h"

#include "utilities/strutils.h"
#include "utilities/timeutils.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/code.h"
#include "engine/frame.h"
#include "engine/proc.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/protocolbuffers.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/engine.h"
#include "engine/convop.h"


namespace sawzall {


static const char* ConversionOp2Description(ConversionOp op);


static const char enc_UTF8[] = "UTF-8";
static const char enc_latin_1[] = "latin-1";
static const char enc_array_literal[] = "array-literal";
static const char enc_hex[] = "hex";
static const char enc_unicode[] = "unicode";

static const char enc_fixed64_little[] = "fixed64-little";
static const char enc_fixed64_big[] = "fixed64-big";
static const char enc_fixed32_little[] = "fixed32-little";
static const char enc_fixed32_big[] = "fixed32-big";
static const char enc_varint[] = "varint";
static const char enc_zigzag[] = "zigzag";
static const char enc_szl[] = "szl";


//-----------------------------------------------------------------------------
// The CvtArgs class is used to check and store extra conversion arguments
// (radix, encoding, time zone or explicit Type*)
// This is separate from the conversion functions because it is only done
// once for array conversions, rather than once per array element.

class CvtArgs {
 public:
  explicit CvtArgs(Type* type) : type_(type)  { }
  void DefaultExtraArgs(ConversionOp op);  // for ConvertArrayToMap
  const char* GetExtraArgs(Proc* proc, ConversionOp op, Val**& sp);

 private:
  // Utility functions for checking conversion encoding parameters.
  // Locale-independent lower-casing for ASCII.
  static int LowerCaseAscii(int c);
  // Case-insensitive string compare.
  static bool IgnoreCaseEql(const char* s1, const char* s2, int n);
  // Case-insensitive compare to a string array (making use of the length).
  template<int N>
  inline bool EqualStr(const char* str, int len, const char (&enc)[N]) {
    return (len == N - 1) && IgnoreCaseEql(str, enc, N - 1);
  }

 public:
  Type* type_;       // for typecast, bytes2proto, proto2bytes, tuple2tuple
  szl_int base_;     // for str2int, str2uint, str2fpr, int2str, uint2str
  int byte_size_;    // for bytes2int, bytes2uint, int2bytes, uint2bytes
  bool swap_;
  bool varint_;
  bool zigzag_;
  enum {             // for str2bytes, bytes2str, val2str, str2array
    EncError, EncUTF8, EncLatin1, EncHex,
    EncArrayLiteral, EncUnicode, EncEmpty
  } enc_;
  char tz_[kMaxTimeStringLen + 1];  // for time2str, str2time
};


int CvtArgs::LowerCaseAscii(int c) {
  if ('A' <= c && c <= 'Z')
    return c + 'a' - 'A';
  return c;
}


bool CvtArgs::IgnoreCaseEql(const char* s1, const char* s2, int n) {
  for (int i = 0; i < n; i++)
    if (LowerCaseAscii(s1[i]) != LowerCaseAscii(s2[i]))
      return false;
  return true;
}


void CvtArgs::DefaultExtraArgs(ConversionOp op) {
  // Only called for array to map; otherwise always provided
  base_ = (op == int2str || op == uint2str) ? 10 : 0;
  byte_size_ = 8;
  swap_ = (SZL_BYTE_ORDER != SZL_BIG_ENDIAN);  // default to big endian
  varint_ = false;
  if (op == array2str || op == str2array || op == map2str || op == tuple2str)
    enc_ = EncUnicode;
  else
    enc_ = EncUTF8;
  tz_[0] = '\0';
}


const char* CvtArgs::GetExtraArgs(Proc* proc, ConversionOp op, Val**& sp) {
  const char* result = NULL;
  switch (op) {
    case str2fpr:
    case str2int:
    case str2uint: {
      base_ = Engine::pop_szl_int(sp);
      if (base_ != 0 && (base_ < 2 || 36 < base_))
        result = "illegal base";
      break;
    }

    case int2str:
    case uint2str: {
      base_ = Engine::pop_szl_int(sp);
      if (base_ < 2 || 36 < base_)
        result = "illegal base";
      break;
    }

    case str2bytes: {
      StringVal* arg = Engine::pop_string(sp);
      const char* str = arg->base();
      int len = arg->length();
      if (EqualStr(str, len, enc_UTF8))
        enc_ = EncUTF8;
      else if (EqualStr(str, len, enc_latin_1))
        enc_ = EncLatin1;
      else if (EqualStr(str, len, enc_hex))
        enc_ = EncHex;
      else
        result = "unknown encoding for string to bytes";
      arg->dec_ref();
      break;
    }

    case bytes2str: {
      StringVal* arg = Engine::pop_string(sp);
      const char* str = arg->base();
      int len = arg->length();
      if (EqualStr(str, len, enc_UTF8))
        enc_ = EncUTF8;
      else if (EqualStr(str, len, enc_latin_1))
        enc_ = EncLatin1;
      else if (EqualStr(str, len, enc_array_literal))
        enc_ = EncArrayLiteral;
      else if (EqualStr(str, len, enc_hex))
        enc_ = EncHex;
      else
        result = "unknown encoding for conversion of bytes to string";
      arg->dec_ref();
      break;
    }

    case str2array: {
      StringVal* arg = Engine::pop_string(sp);
      const char* str = arg->base();
      int len = arg->length();
      if (EqualStr(str, len, enc_unicode))
        enc_ = EncUnicode;
      else
        result = "unknown encoding converting from string to array";
      arg->dec_ref();
      break;
    }

    case array2str: {
      StringVal* arg = Engine::pop_string(sp);
      const char* str = arg->base();
      int len = arg->length();
      if (EqualStr(str, len, enc_unicode))
        enc_ = EncUnicode;
      else if (len == 0)
        enc_ = EncEmpty;
      else
        result = "unknown encoding converting to string from compound value";
      arg->dec_ref();
      break;
    }

    case map2str:
    case tuple2str: {
      StringVal* arg = Engine::pop_string(sp);
      assert(arg->length() == 0);
      enc_ = EncEmpty;
      arg->dec_ref();
      break;
    }

    case int2bytes:
    case uint2bytes:
    case bytes2int:
    case bytes2uint: {
      StringVal* arg = Engine::pop_string(sp);
      const char* str = arg->base();
      int len = arg->length();
      // Only need to set zigzag_ when varint_ is true
      if (EqualStr(str, len, enc_varint)) {
        varint_ = true;
        zigzag_ = false;
      } else if (EqualStr(str, len, enc_zigzag)) {
        varint_ = true;  // ZigZag is based on the varint encoding.
        zigzag_ = true;
      } else if (EqualStr(str, len, enc_fixed64_little)) {
        varint_ = false;
        byte_size_ = 8;
        swap_ = (SZL_BYTE_ORDER != SZL_LITTLE_ENDIAN);
      } else if (EqualStr(str, len, enc_fixed64_big) ||
                 EqualStr(str, len, enc_szl)) {
        varint_ = false;
        byte_size_ = 8;
        swap_ = (SZL_BYTE_ORDER != SZL_BIG_ENDIAN);  // szl is big endian
      } else if (EqualStr(str, len, enc_fixed32_little)) {
        varint_ = false;
        byte_size_ = 4;
        swap_ = (SZL_BYTE_ORDER != SZL_LITTLE_ENDIAN);
      } else if (EqualStr(str, len, enc_fixed32_big)) {
        varint_ = false;
        byte_size_ = 4;
        swap_ = (SZL_BYTE_ORDER != SZL_BIG_ENDIAN);
      } else {
        result = proc->PrintError("unknown encoding %.*q for conversion of %s",
                   len, str, ConversionOp2Description(op));
      }
      arg->dec_ref();
      break;
    }

    case str2time:
    case time2str: {
      // TODO: pass tz as (ptr,len)?
      // TODO: interpret time zone here instead of during conversion
      StringVal* arg = Engine::pop_string(sp);
      arg->c_str(tz_, sizeof(tz_));
      arg->dec_ref();
      break;
    }

    default:
      break;
  }
  return result;
}


//-----------------------------------------------------------------------------
// The conversion functions.
// The inline versions are called directly in ConvertBasic for maximum speed.
// They must not return errors and they must not use CvtArgs, except for
// TypeCast.

static const char* NoConv(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  // Only used by ConvertArrayToMap when one but not both of the key and value
  // types matches the array element type.
  *result = val;
  val->inc_ref();
  return NULL;
}


inline const char* TypeCast(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  BasicType* basic_type = args->type_->as_basic();
  assert(basic_type != NULL);
  uint64 x = val->basic64();
  *result = basic_type->form()->NewValBasic64(proc, args->type_, x);
  return NULL;
}


inline const char* Str2Bool(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  StringVal* s = val->as_string();
  bool b = s->length() > 0 && (s->base()[0] == 't' || s->base()[0] == 'T');
  *result = Factory::NewBool(proc, b);
  return NULL;
}


static const char* Str2Bytes(Proc* proc, CvtArgs* args, Val* val,
                             Val** result) {
  struct Local {
    // decode byte encoded as one hex nibble in ASCII
    static int hex_value(Rune c) {
      if ('0' <= c && c <= '9')
        return c - '0';
      if ('a' <= c && c <= 'f')
        return 0xa + (c - 'a');
      if ('A' <= c && c <= 'F')
        return 0xa + (c - 'A');
      return -1;
    }
  };
  StringVal* str = val->as_string();
  // convert from Rune* to char*
  // for now, only handle a few cases.

  if (args->enc_ == CvtArgs::EncUTF8) {
    // NOTE: If StringVal were a subclass if BytesVal,
    // there would be nothing to do here
    int len = str->length(); // one character goes into one byte
    *result = Factory::NewBytesInit(proc, len, str->base());

  } else if (args->enc_ == CvtArgs::EncLatin1) {
    int len = str->num_runes(); // one character goes into one byte
    BytesVal* y = Factory::NewBytes(proc, len);
    for (int i = 0; i < len; i++) {
      const Rune c = str->at(str->byte_offset(proc, i));
      if (c > 255) {
        y->dec_ref();
        return "character out of range converting to latin-1";
      }
      y->at(i) = c;
    }
    *result = y;

  } else if (args->enc_ == CvtArgs::EncHex) {
    // two characters go into one byte
    int len = str->num_runes();
    if ((len & 1) != 0)
      return "odd number of characters for hex conversion to string";
    len /= 2;
    BytesVal* y = Factory::NewBytes(proc, len);
    for (int i = 0; i < len; i++) {
      const int c0 = Local::hex_value(str->at(str->byte_offset(proc, 2*i + 0)));
      const int c1 = Local::hex_value(str->at(str->byte_offset(proc, 2*i + 1)));
      if (c0 < 0 || c1 < 0) {
        y->dec_ref();
        return "illegal hex value converting hex string to bytes";
      }
      y->at(i) = (c0 << 4) | c1;
    }
    *result = y;

  } else {
    return "internal error; unknown encoding for string to bytes";
  }
  return NULL;
}


inline const char* Fpr2Bytes(Proc* proc, CvtArgs* args, Val* val,
                             Val** result) {
  // Convert 64-bit fingerprint into 8 bytes of big endian binary data
  uint64 fpr = val->as_fingerprint()->val();
  if (SZL_BYTE_ORDER != SZL_BIG_ENDIAN)
    fpr = local_bswap_64(fpr);
  *result = Factory::NewBytesInit(proc, 8, reinterpret_cast<char*>(&fpr));
  return NULL;
}


inline const char* Int2Float(Proc* proc, CvtArgs* args, Val* val,
                             Val** result) {
  *result = Factory::NewFloat(proc,
                              static_cast<szl_float>(val->as_int()->val()));
  return NULL;
}


inline const char* UInt2Float(Proc* proc, CvtArgs* args, Val* val,
                              Val** result) {
  *result = Factory::NewFloat(proc,
                              static_cast<szl_float>(val->as_uint()->val()));
  return NULL;
}


inline const char* Bits2UInt(Proc* proc, CvtArgs* args, Val* val,
                             Val** result) {
  *result = Factory::NewUInt(proc, static_cast<szl_uint>(val->basic64()));
  return NULL;
}


inline const char* UInt2Int(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  *result = Factory::NewInt(proc, static_cast<szl_int>(val->as_uint()->val()));
  return NULL;
}


inline const char* Float2Int(Proc* proc, CvtArgs* args, Val* val,
                             Val** result) {
  *result = Factory::NewInt(proc, static_cast<szl_int>(val->as_float()->val()));
  return NULL;
}


inline const char* Float2UInt(Proc* proc, CvtArgs* args, Val* val,
                              Val** result) {
  *result = Factory::NewUInt(proc,
                             static_cast<szl_uint>(val->as_float()->val()));
  return NULL;
}


inline const char* UInt2Time(Proc* proc, CvtArgs* args, Val* val,
                             Val** result) {
  *result = Factory::NewTime(proc, val->as_uint()->val());
  return NULL;
}


inline const char* UInt2Fpr(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  *result = Factory::NewFingerprint(proc, val->as_uint()->val());
  return NULL;
}


static const char* Str2Float(Proc* proc, CvtArgs* args, Val* val,
                             Val** result) {
  StringVal* str = val->as_string();
  // Create NUL-terminated string
  char buf[64];
  char* p = str->c_str(buf, sizeof buf);
  errno = 0;
  double d = strtod(buf, &p);
  if (p == buf)
    return proc->PrintError("string %q contains no float", buf);
  // As in the scanner, catch underflow (ERANGE and value is zero)
  // and overflow (ERANGE and value is large)
  // but ignore partial underflow (ERANGE and value is small).
  if (errno == ERANGE && (d == 0.0 || d > 1.0))
    return proc->PrintError("string %q has range error "
                            "when converting to float", buf);
  if (*p != '\0')
    return proc->PrintError("string %q contains extra chars", buf);
  *result = Factory::NewFloat(proc, d);
  return NULL;
}


static const char* Str2Int(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  StringVal* str = val->as_string();
  // Create NUL-terminated string
  char buf[64];
  char* p = str->c_str(buf, sizeof buf);
  errno = 0;
  assert(args->base_ == 0 || (2 <= args->base_ && args->base_ <= 36));
  szl_int i = strtoll(buf, &p, args->base_);
  if (p == buf)
    return proc->PrintError("string %q contains no int", buf);
  if (errno != 0)    // overflow or bad base
    return proc->PrintError("string %q overflows when converting to int", buf);
  if (*p != '\0')
    return proc->PrintError("string %q contains extra chars", buf);
  *result = Factory::NewInt(proc, i);
  return NULL;
}


static const char* Str2UInt(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  StringVal* str = val->as_string();
  // Create NUL-terminated string
  char buf[64];
  char* p = str->c_str(buf, sizeof buf);
  errno = 0;
  assert(args->base_ == 0 || (2 <= args->base_ && args->base_ <= 36));
  szl_uint ui = strtoull(buf, &p, args->base_);
  if (p == buf)
    return proc->PrintError("string %q contains no uint", buf);
  if (errno != 0)    // overflow or bad base
    return proc->PrintError("string %q overflows when converting to uint", buf);
  if (*p != '\0')
    return proc->PrintError("string %q contains extra chars", buf);
  *result = Factory::NewUInt(proc, ui);
  return NULL;
}


inline const char* Bool2Str(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  bool b = val->as_bool()->val() != 0;
  const char *str = b ? "true" : "false";
  *result = Factory::NewStringC(proc, str);
  return NULL;
}


static const char* Bytes2Str(Proc* proc, CvtArgs* args, Val* val,
                             Val** result) {
  BytesVal* bytes = val->as_bytes();
  if (args->enc_ == CvtArgs::EncUTF8) {
    // UTF-8
    void* p = memchr(bytes->base(), '\0', bytes->length());
    if (p != NULL)
      return proc->PrintError("encountered 0 byte at index %d "
                              "converting from bytes to string",
             static_cast<int>(static_cast<char*>(p) - bytes->base()));
    *result = Factory::NewStringBytes(proc, bytes->length(), bytes->base());

  } else if (args->enc_ == CvtArgs::EncLatin1) {
    // Latin-1 (ISO 8859-1)
    int bytes_len = bytes->length();
    int utf8_len = 0;
    for (int i = 0; i < bytes_len; i++) {
      Rune c = bytes->at(i);
      if (c == 0)
        return proc->PrintError("encountered 0 byte at index %d "
                                "converting from bytes to string", i);
      utf8_len += runelen(c);
    }
    StringVal* s = Factory::NewString(proc, utf8_len, bytes_len);
    char* p = s->base();
    for (int i = 0; i < bytes_len; i++) {
      Rune c = bytes->at(i);
      p += runetochar(p, &c);
    }
    *result = s;
    assert((p - s->base()) == utf8_len);

  } else if (args->enc_ == CvtArgs::EncArrayLiteral) {
    // Array literal
    Fmt::State f;
    F.fmtstrinit(&f);
    bytes->Format(proc, &f);
    char* r = F.fmtstrflush(&f);
    StringVal* s = Factory::NewStringC(proc, r);
    free(r);
    *result = s;

  } else if (args->enc_ == CvtArgs::EncHex) {
    // Hex
    // TODO: this can be done more efficiently by
    // converting directly into the StringVal.
    Fmt::State f;
    F.fmtstrinit(&f);
    for (int i = 0; i < bytes->length(); i++)
      F.fmtprint(&f, "%.2x", bytes->at(i));
    char* str = F.fmtstrflush(&f);
    StringVal* s = Factory::NewStringC(proc, str);
    free(str);
    *result = s;

  } else {
    return "internal error; unknown encoding for conversion of bytes to string";
  }
  return NULL;
}


inline const char* Fpr2Str(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  szl_fingerprint fp = val->as_fingerprint()->val();
  char str[64];
  int len = F.snprint(str, sizeof str, SZL_FINGERPRINT_FMT, fp);
  // known to be ASCII, one byte per rune
  *result = Factory::NewStringBytes(proc, len, str);
  return NULL;
}


static const char* Str2Fpr(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  StringVal* str = val->as_string();
  // Create NUL-terminated string
  char buf[64];
  char* p = str->c_str(buf, sizeof buf);
  errno = 0;
  assert(args->base_ == 0 || (2 <= args->base_ && args->base_ <= 36));
  szl_fingerprint fpr = strtoull(buf, &p, args->base_);
  if (p == buf)
    return proc->PrintError("string %q contains no int", buf);
  if (errno != 0)  // overflow or bad base
    return proc->PrintError("string %q overflows when "
                            "converting to fingerprint", buf);
  if (*p != '\0')
    return proc->PrintError("string %q contains extra chars", buf);
  *result = Factory::NewFingerprint(proc, fpr);
  return NULL;
}


inline const char* Float2Str(Proc* proc, CvtArgs* args, Val* val,
                             Val** result) {
  char buf[64];
  int length = FloatToAscii(buf, val->as_float()->val());
  *result = Factory::NewStringBytes(proc, length, buf);
  return NULL;
}


static const char* Int2Str(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  szl_int x = val->as_int()->val();
  assert(2 <= args->base_ && args->base_ <= 36);
  // convert x
  static const char digit[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  const int N = 100;  // enough to represent a binary szl_int and sign
  char buf[N];
  int i = N;
  unsigned long long y = x;
  if (x < 0)
    y = -x;  // works also for most negative value (where -x == x)
  do {
    buf[--i] = digit[y % args->base_];
    y /= args->base_;
  } while (y > 0);
  if (x < 0)
    buf[--i] = '-';  // argument was negative, add a '-'
  assert(i >= 0);  // otherwise buf is too short
  *result = Factory::NewStringBytes(proc, N - i, &buf[i]);
  return NULL;
}


static const char* UInt2Str(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  szl_uint x = val->as_uint()->val();
  assert(2 <= args->base_ && args->base_ <= 36);
  // convert x
  static const char digit[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  const int N = 100;  // enough to represent a binary szl_uint
  char buf[N];
  int i = N;
  do {
    buf[--i] = digit[x % args->base_];
    x /= args->base_;
  } while (x > 0);
  assert(i >= 0);  // otherwise buf is too short
  *result = Factory::NewStringBytes(proc, N - i, &buf[i]);
  return NULL;
}


static const char* Time2Str(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  szl_time t = val->as_time()->val();
  char buf[kMaxTimeStringLen + 1];
  if (!SzlTime2Str(t, args->tz_, &buf))
    return proc->PrintError("date conversion failed: invalid time or "
                            "time zone %q was not recognized", args->tz_);
  *result = Factory::NewStringC(proc, buf);
  return NULL;
}


static const char* Str2Time(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  string s = val->as_string()->cpp_str(proc);  // TODO: more efficient code?
  szl_time time = 0;
  if (!date2uint64(s.c_str(), args->tz_, &time))
    return proc->PrintError("date conversion failed (%q is not an accepted "
        "date string) or time zone %q was not recognized",
        s.c_str(), args->tz_);
  *result = Factory::NewTime(proc, time);
  return NULL;
}


static const char* Bytes2Proto(Proc* proc, CvtArgs* args, Val* val,
                               Val** result) {
  TupleType* tuple = args->type_->as_tuple();
  assert(tuple != NULL && tuple->is_proto());
  BytesVal* b = val->as_bytes();
  TupleVal* t = NULL;
  const char* trap_info = protocolbuffers::ReadTuple(proc, tuple, &t, b);
  *result = t;  // safe to push even if trap_info != NULL
  return trap_info;
}


static const char* Proto2Bytes(Proc* proc, CvtArgs* args, Val* val,
                               Val** result) {
  TupleType* tuple = args->type_->as_tuple();
  assert(tuple != NULL && tuple->is_proto());
  TupleVal* t = val->as_tuple();
  BytesVal* b = NULL;
  const char* trap_info = protocolbuffers::WriteTuple(proc, tuple, t, &b);
  *result = b;
  return trap_info;
}


static const char* Tuple2Tuple(Proc* proc, CvtArgs* args, Val* val,
                               Val** result) {
  TupleVal* src = val->as_tuple();
  assert(src->type()->is_tuple() && args->type_->is_tuple());
  TupleType* src_type = src->type()->as_tuple();
  TupleType* dst_type = args->type_->as_tuple();
  TupleVal* dst = dst_type->form()->NewVal(proc, TupleForm::ignore_inproto);
  assert(src_type->nslots() == dst_type->nslots());
  assert(src_type->ntotal() == dst_type->ntotal());
  memmove(dst->base(), src->base(), dst_type->ntotal() * sizeof(Val*));
  // Increase the ref count on the copied elements
  int nslots = dst_type->nslots();
  for (int i = 0; i < nslots; i++)
    dst->slot_at(i)->inc_ref();
  *result = dst;
  return NULL;
}


static const char* Bytes2Fpr(Proc* proc, CvtArgs* args, Val* val,
                             Val** result) {
  // Convert bytes to fingerprint.
  // bytes variable must be exactly 8 bytes long and is treated
  // as a big-endian integer.
  BytesVal* y = val->as_bytes();
  if (y->length() != sizeof(szl_fingerprint))
    return "converting bytes to fingerprint: input "
           "must be exactly 64 bits long";
  uint64 fpr;
  memcpy(&fpr, y->u_base(), sizeof(szl_fingerprint));
  if (SZL_BYTE_ORDER != SZL_BIG_ENDIAN)
    fpr = local_bswap_64(fpr);
  *result = Factory::NewFingerprint(proc, fpr);
  return NULL;
}


static const char* Val2Str(Proc* proc, CvtArgs* args, Val* val, Val** result) {
  // If the encoding is "unicode", we're doing array of int to string
  if (args->enc_ == CvtArgs::EncUnicode) {
    ArrayVal* a = val->as_array();
    assert(a->form()->type()->IsEqual(SymbolTable::array_of_int_type(), false));
    Rune* r = new Rune[a->length()];
    int width = 0;
    for (int i = 0; i < a->length(); i++) {
      int c = a->at(i)->as_int()->val();
      if (c == 0) {
        delete [] r;
        return "integer 0 encountered converting array of int to string";
      }
      r[i] = c;
      if (r[i] != c) {
        delete [] r;
        return proc->PrintError("character value 0x%x out of range "
                                "converting array of int to string", c);
      }
      width += runelen(c);
    }
    StringVal* str = Factory::NewString(proc, width, a->length());
    RuneStr2Str(str->base(), width, r, a->length());
    delete[] r;
    *result = str;
    return NULL;
  } else if (args->enc_ == CvtArgs::EncEmpty) {
    Fmt::State f;
    F.fmtstrinit(&f);
    val->Format(proc, &f);
    char* s = F.fmtstrflush(&f);
    StringVal* str = Factory::NewStringBytes(proc, strlen(s), s);
    free(s);
    *result = str;
    return NULL;
  } else {
    return "internal error; unknown encoding converting "
           "to string from compound value";
  }
}


static const char* Str2Array(Proc* proc, CvtArgs* args, Val* val,
                             Val** result) {
  StringVal* str = val->as_string();
  // If the encoding is "unicode", we're doing string to array of int
  if (args->enc_ == CvtArgs::EncUnicode) {
    ArrayVal* a = Factory::NewIntArray(proc, str->num_runes());
    const char* s = str->base();
    for (int i = 0; i < str->num_runes(); i++) {
      Rune r;
      int w = FastCharToRune(&r, s);
      a->at(i) = Factory::NewInt(proc, r);
      s += w;
    }
    *result = a;
    return NULL;
  } else {
    return "internal error; unknown encoding converting "
            "to from string to array";
  }
}


static const char* Function2Str(Proc* proc, CvtArgs* args, Val* val,
                                Val** result) {
  ClosureVal* c = val->as_closure();
  Fmt::State f;
  F.fmtstrinit(&f);
  c->Format(proc, &f);
  char* s = F.fmtstrflush(&f);
  StringVal* str = Factory::NewStringBytes(proc, strlen(s), s);
  free(s);
  *result = str;
  return NULL;
}


static const char* Bytes2Int(Proc* proc, CvtArgs* args, Val* val,
                             Val** result) {
  BytesVal* bytes = val->as_bytes();
  if (args->varint_) {
    // Assume we will not segfault running nine bytes off the end.
    uint64 uint_val;
    const char* end = DecodeUnsignedVarint64(bytes->base(), &uint_val);
    if (end == NULL || (end - bytes->base()) != bytes->length())
      return "invalid varint value for decoding";
    if (args->zigzag_)
      uint_val = (uint_val >> 1) ^ -(uint_val & 1);
    *result = Factory::NewInt(proc, uint_val);
  } else {
    szl_int int_val;
    if (bytes->length() != args->byte_size_) {
      return proc->PrintError(
          "length of bytes value (%d) wrong for conversion to int; "
          "should be %d", bytes->length(), args->byte_size_);
    } else if (args->byte_size_ == 4) {
      uint32 uint32_val;
      memcpy(&uint32_val, bytes->base(), 4);  // inline code
      if (args->swap_)
        uint32_val = local_bswap_32(uint32_val);
      int_val = static_cast<int32>(uint32_val);
    } else if (args->byte_size_ == 8) {
      uint64 uint64_val;
      memcpy(&uint64_val, bytes->base(), 8);  // inline code
      if (args->swap_)
        uint64_val = local_bswap_64(uint64_val);
      int_val = static_cast<int64>(uint64_val);
    } else {
      return proc->PrintError(
          "internal error in bytes to int: byte_size %d should be 4 or 8",
          args->byte_size_);
    }

    *result = Factory::NewInt(proc, int_val);
  }

  return NULL;
}


static const char* Bytes2UInt(Proc* proc, CvtArgs* args, Val* val,
                              Val** result) {
  BytesVal* bytes = val->as_bytes();
  if (args->varint_) {
    // Assume we will not segfault running nine bytes off the end.
    uint64 uint_val;
    const char* end = DecodeUnsignedVarint64(bytes->base(), &uint_val);
    if (end == NULL || (end - bytes->base()) != bytes->length())
      return "invalid varint value for decoding";
    if (args->zigzag_)
      uint_val = (uint_val >> 1) ^ -(uint_val & 1);
    *result = Factory::NewUInt(proc, uint_val);
  } else {
    szl_uint uint_val;
    if (bytes->length() != args->byte_size_) {
      return proc->PrintError(
          "length of bytes value (%d) wrong for conversion to uint; "
          "should be %d", bytes->length(), args->byte_size_);
    } else if (args->byte_size_ == 4) {
      uint32 uint32_val;
      memcpy(&uint32_val, bytes->base(), 4);  // inline code
      if (args->swap_)
        uint32_val = local_bswap_32(uint32_val);
      uint_val = uint32_val;
    } else if (args->byte_size_ == 8) {
      uint64 uint64_val;
      memcpy(&uint64_val, bytes->base(), 8);  // inline code
      if (args->swap_)
        uint64_val = local_bswap_64(uint64_val);
      uint_val = uint64_val;
    } else {
      return proc->PrintError(
          "internal error in bytes to int: byte_size %d should be 4 or 8",
          args->byte_size_);
    }

    *result = Factory::NewUInt(proc, uint_val);
  }

  return NULL;
}


static const char* Int2Bytes(Proc* proc, CvtArgs* args, Val* val,
                             Val** result) {
  int64 int_val = val->as_int()->val();
  if (args->varint_) {
    uint64 uint_val = int_val;
    if (args->zigzag_)
      uint_val = (uint_val << 1) ^ -(uint_val >> 63);
    char varint[kMaxUnsignedVarint64Length];
    char* end = EncodeUnsignedVarint64(varint, uint_val);
    *result = Factory::NewBytesInit(proc, end - varint, varint);
  } else {
    if (args->byte_size_ == 4) {
      // Check that the value fits within 32 bits.
      if (int_val < kint32min || kint32max < int_val) {
        return proc->PrintError(
            "int value %lld out of range for 32-bit encoding", int_val);
      }
      uint32 uint32_val = static_cast<uint32>(int_val);
      if (args->swap_)
        uint32_val = local_bswap_32(uint32_val);
      *result = Factory::NewBytesInit(proc, 4,
                                      reinterpret_cast<char*>(&uint32_val));
    } else if (args->byte_size_ == 8) {
      uint64 uint64_val = static_cast<uint64>(int_val);
      if (args->swap_)
        uint64_val = local_bswap_64(uint64_val);
      *result = Factory::NewBytesInit(proc, 8,
                                      reinterpret_cast<char*>(&uint64_val));
    } else {
      return proc->PrintError(
          "internal error in int to bytes: byte_size %d should be 4 or 8",
          args->byte_size_);
    }
  }

  return NULL;
}


static const char* UInt2Bytes(Proc* proc, CvtArgs* args, Val* val,
                              Val** result) {
  szl_uint uint_val = val->as_uint()->val();
  if (args->varint_) {
    if (args->zigzag_)
      uint_val = (uint_val << 1) ^ -(uint_val >> 63);
    char varint[kMaxUnsignedVarint64Length];
    char* end = EncodeUnsignedVarint64(varint, uint_val);
    *result = Factory::NewBytesInit(proc, end - varint, varint);
  } else {
    if (args->byte_size_ == 4) {
      // Check that the value fits within 32 bits.
      if (uint_val > kuint32max) {
        return proc->PrintError(
            "uint value %llu out of range for 32-bit encoding", uint_val);
      }
      uint32 uint32_val = static_cast<uint32>(uint_val);
      if (args->swap_)
        uint32_val = local_bswap_32(uint32_val);
      *result = Factory::NewBytesInit(proc, 4,
                                      reinterpret_cast<char*>(&uint32_val));
    } else if (args->byte_size_ == 8) {
      uint64 uint64_val = static_cast<uint64>(uint_val);
      if (args->swap_)
        uint64_val = local_bswap_64(uint64_val);
      *result = Factory::NewBytesInit(proc, 8,
                                      reinterpret_cast<char*>(&uint64_val));
    } else {
      return proc->PrintError(
          "internal error in int to bytes: byte_size %d should be 4 or 8",
          args->byte_size_);
    }
  }

  return NULL;
}


//-----------------------------------------------------------------------------
// For each conversion we need the function and, when converting to an array,
// the result array type.  Since we have a table, we also keep a description
// of the conversion for error messages, the name for code annotation,
// and a flag as to whether it can fail, for code optimization.

struct ConversionAttributes {
  typedef const char* (*Convert)(Proc* proc, CvtArgs* args, Val* val,
                                 Val** result);
  // Method for performing this conversion.
  const char* (*convert)(Proc* proc, CvtArgs* args, Val* val, Val** result);
  // Whether allowed as part of an array to array conversion.
  bool array_to_array;
  // Whether allowed as part of an array to map conversion.
  bool array_to_map;
  // Whether it can fail
  bool can_fail;
  // SymbolTable static function to get result type for array conversions.
  ArrayType* (*get_array_type)();
  // A description of the op for error messages.
  // TODO: make more use of this; requires minor changes to make
  // error messages more consistent and updates to the regression tests.
  const char* description;
  // The name of the op.
  const char* name;
  // To confirm that the array is set up correctly.
  ConversionOp op;
};


#define OP(op, convert, array, map, can_fail, result_type, description) { \
  &convert, \
  array, \
  map, \
  can_fail, \
  &SymbolTable::array_of_##result_type##_type, \
  description, \
  #op, \
  op   \
}


// For typecast, bytes2proto and tuple2tuple the actual result type for
// array to array conversions is supplied in the ConvertArray call.
// For conversions not allowed as part of array-to-array or array-to-map,
// the result type is not used (and for str2array is set to "int" because
// there is no "array of array of int" in SymbolTable).
static ConversionAttributes conversion_attributes[] = {
// opcode       function     array? map?   fail?  result type description
// ------       --------     ------ ----   -----  ----------- -----------
OP(noconv,      NoConv,      false, true,  false, int,        NULL),
OP(typecast,    TypeCast,    true,  true,  false, int,        NULL),
OP(str2bool,    Str2Bool,    true,  true,  false, bool,       NULL),
OP(fpr2bytes,   Fpr2Bytes,   true,  true,  false, bytes,      NULL),
OP(str2bytes,   Str2Bytes,   true,  true,  true,  bytes,      NULL),
OP(int2bytes,   Int2Bytes,   true,  false, true,  bytes,      "int to bytes"),
OP(uint2bytes,  UInt2Bytes,  true,  false, true,  bytes,      "uint to bytes"),
OP(str2fpr,     Str2Fpr,     true,  true,  true,  fingerprint,NULL),
OP(uint2fpr,    UInt2Fpr,    true,  true,  false, fingerprint,NULL),
OP(bytes2fpr,   Bytes2Fpr,   true,  true,  true,  fingerprint,NULL),
OP(int2float,   Int2Float,   true,  true,  false, float,      NULL),
OP(str2float,   Str2Float,   true,  true,  true,  float,      NULL),
OP(uint2float,  UInt2Float,  true,  true,  false, float,      NULL),
OP(float2int,   Float2Int,   true,  true,  false, int,        NULL),
OP(str2int,     Str2Int,     true,  true,  true,  int,        NULL),
OP(uint2int,    UInt2Int,    true,  true,  false, int,        NULL),
OP(bytes2int,   Bytes2Int,   true,  false, true,  int,        "bytes to int"),
OP(bool2str,    Bool2Str,    true,  true,  false, string,     NULL),
OP(bytes2str,   Bytes2Str,   true,  true,  true,  string,     NULL),
OP(float2str,   Float2Str,   true,  true,  false, string,     NULL),
OP(int2str,     Int2Str,     true,  true,  true,  string,     NULL),
OP(time2str,    Time2Str,    true,  true,  true,  string,     NULL),
OP(uint2str,    UInt2Str,    true,  true,  true,  string,     NULL),
OP(fpr2str,     Fpr2Str,     true,  true,  false, string,     NULL),
OP(array2str,   Val2Str,     false, false, true,  string,     NULL),
OP(map2str,     Val2Str,     false, false, false, string,     NULL),
OP(tuple2str,   Val2Str,     false, false, false, string,     NULL),
OP(function2str,Function2Str,false, false, false, string,     NULL),
OP(str2array,   Str2Array,   false, false, true,  int,        NULL),
OP(str2time,    Str2Time,    true,  true,  true,  time,       NULL),
OP(uint2time,   UInt2Time,   true,  true,  false, time,       NULL),
OP(float2uint,  Float2UInt,  true,  true,  false, uint,       NULL),
OP(bits2uint,   Bits2UInt,   true,  true,  false, uint,       NULL),
OP(str2uint,    Str2UInt,    true,  true,  true,  uint,       NULL),
OP(bytes2uint,  Bytes2UInt,  true,  false, true,  uint,       "bytes to uint"),
OP(bytes2proto, Bytes2Proto, true,  false, true,  int,        NULL),
OP(proto2bytes, Proto2Bytes, true,  false, true,  bytes,      NULL),
OP(tuple2tuple, Tuple2Tuple, true,  false, false, int,        NULL)
};

#undef OP


inline ConversionAttributes*
GetAttributes(ConversionOp op) {
  assert(op >= noconv && op <= tuple2tuple);
  ConversionAttributes* attributes = &conversion_attributes[op];
  assert(attributes->op == op);
  return attributes;
}


// Used for code annotation.
const char* ConversionOp2String(ConversionOp op) {
  return GetAttributes(op)->name;
}


// Used for code generation
bool ConversionCanFail(ConversionOp op) {
  return GetAttributes(op)->can_fail;
}


// Used for error messages.
static const char* ConversionOp2Description(ConversionOp op) {
  return GetAttributes(op)->description;
}


// Used for semantic checking and in an assert in code generation.
bool ImplementedArrayToArrayConversion(ConversionOp op) {
  return GetAttributes(op)->array_to_array;
}


// Used for semantic checking and in an assert in code generation.
bool ImplementedArrayToMapConversion(ConversionOp op) {
  return GetAttributes(op)->array_to_map;
}


//-----------------------------------------------------------------------------
// The conversions called by executing code.

const char* ConvOp::ConvertBasic(Proc* proc, ConversionOp op, Val**& sp,
                                 Type* type) {
  Val* val = Engine::pop(sp);
  Val* result = NULL;
  const char* error;

  // Special-case inline versions.
  // None of these functions may take an extra parameter or fail,
  // (But TypeCast does use our "type" parameter passed using CvtArgs).
  switch (op) {
    case typecast: {
      CvtArgs args(type);
      error = TypeCast(proc, &args, val, &result);
      assert(error == NULL && !GetAttributes(op)->can_fail);
      break;
    }

#define CONVCASE(op, fct) \
    case op: \
      error = fct(proc, NULL, val, &result); \
      assert(error == NULL && !GetAttributes(op)->can_fail); \
      break;

    CONVCASE(int2float,  Int2Float);
    CONVCASE(uint2float, UInt2Float);
    CONVCASE(bits2uint,  Bits2UInt);
    CONVCASE(uint2int,   UInt2Int);
    CONVCASE(float2int,  Float2Int);
    CONVCASE(float2uint, Float2UInt);
    CONVCASE(uint2time,  UInt2Time);
    CONVCASE(uint2fpr,   UInt2Fpr);
    CONVCASE(str2bool,   Str2Bool);
    CONVCASE(bool2str,   Bool2Str);
    CONVCASE(fpr2bytes,  Fpr2Bytes);
    CONVCASE(fpr2str,    Fpr2Str);
    CONVCASE(float2str,  Float2Str);

#undef CONVCASE

    default:
      ConversionAttributes* attributes = GetAttributes(op);
      CvtArgs args(type);
      error = args.GetExtraArgs(proc, op, sp);
      if (error == NULL)
        error = (*attributes->convert)(proc, &args, val, &result);
      assert(error == NULL || attributes->can_fail);
      break;
  }

  val->dec_ref();
  Engine::push(sp, result);
  return error;
}


const char* ConvOp::ConvertArray(Proc* proc, ConversionOp op, Val**& sp,
                                 ArrayType* type) {
  ArrayVal* a = Engine::pop_array(sp);
  const int len = a->length();
  // If this is the only reference to the array, decrement the refcounts on
  // the elements immediately; otherwise the memory for the elements may not
  // be reclaimed until the refcount of the array is decremented.  This is
  // important when the array is very large and we are nearly out of memory.
  const bool free_elements = a->is_unique();

  assert(op != noconv);  // otherwise would need to set up the type
  ConversionAttributes* attributes = GetAttributes(op);
  Type* type_arg = (type != NULL) ? type->elem_type() : NULL;
  ArrayType* result_type =
      (op == typecast || op == bytes2proto || op == tuple2tuple) ?
      type : (*attributes->get_array_type)();
  CvtArgs args(type_arg);
  const char* error = args.GetExtraArgs(proc, op, sp);
  if (error != NULL) {
    a->dec_ref();
    return error;
  }

  ArrayVal* result = result_type->form()->NewVal(proc, len);
  error = NULL;
  ConversionAttributes::Convert convert = attributes->convert;
  // Make sure every element is set, even if just to undefined.
  for (int i = 0; i < len; i++) {
    Val*& element = a->at(i);
    if (!error)
      error = (*convert)(proc, &args, element, &result->at(i));
    if (error)
      result->at(i) = NULL;
    assert(error == NULL || attributes->can_fail);
    if (free_elements) {
      element->dec_ref();
      element = NULL;
    }
  }

  a->dec_ref();
  Engine::push(sp, result);
  return error;
}


const char* ConvOp::ConvertArrayToMap(Proc* proc, MapType* map_type,
      ConversionOp key_op, ConversionOp value_op, Val**& sp) {
  ArrayVal* a = Engine::pop_array(sp);
  int len = a->length();
  if (len & 1) {  // won't work unless we have key:value pairs
    a->dec_ref();
    return "odd number of array elements in map conversion";
  }
  len /= 2;  // now number of map elements to create
  bool free_elements = a->is_unique();  // see the note in ConvertArray.

  ConversionAttributes* key_attributes = GetAttributes(key_op);
  CvtArgs key_args(map_type->index_type());
  key_args.DefaultExtraArgs(key_op);
  ConversionAttributes* value_attributes = GetAttributes(value_op);
  CvtArgs value_args(map_type->elem_type());
  value_args.DefaultExtraArgs(value_op);

  MapVal* m = map_type->form()->NewValInit(proc, len, true);
  Map* map = m->map();
  const char* error = NULL;
  ConversionAttributes::Convert key_convert = key_attributes->convert;
  ConversionAttributes::Convert value_convert = value_attributes->convert;

  for (int i = 0; i < len; i++) {
    const int key_i = 2*i;  // key is first of pair
    const int value_i = key_i + 1;  // value is second of pair

    Val*& key_src = a->at(key_i);
    Val* key = NULL;
    error = (*key_convert)(proc, &key_args, key_src, &key);
    assert(error == NULL || key_attributes->can_fail);
    if (free_elements) {
      key_src->dec_ref();
      key_src = NULL;
    }

    if (error != NULL)
      break;
    int index = map->InsertKey(key);

    Val*& value_src = a->at(value_i);
    Val* value = NULL;
    error = (*value_convert)(proc, &value_args, value_src, &value);
    assert(error == NULL || value_attributes->can_fail);
    if (free_elements) {
      value_src->dec_ref();
      value_src = NULL;
    }

    if (error != NULL)
      break;
    map->SetValue(index, value);
  }

  a->dec_ref();
  Engine::push(sp, m);
  return error;
}

}  // namespace sawzall
