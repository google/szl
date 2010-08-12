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

// Defines intrinsic functions and types:
//   zlibcompress
//   zlipuncompress
//   gzip
//   gunzip
//


#include <assert.h>
#include <string>

#include "engine/globals.h"
#include "public/logging.h"

#include "utilities/strutils.h"
#include "utilities/zlibwrapper.h"
#include "utilities/gzipwrapper.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/frame.h"
#include "engine/proc.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/engine.h"
#include "engine/intrinsic.h"

typedef unsigned long uLong;


namespace sawzall {

static const char zlibuncompress_doc[] =
  "Uncompresses the zipped data using zlib, and returns the uncompressed data. "
  "Extra data past the valid zlib data results in an error. "
  "Optional parameter gives intermediate buffer size for decompression "
  "in bytes (default 8192)";

static const char* zlibuncompress(Proc* proc, Val**& sp) {
  BytesVal* bytes_val = Engine::pop_bytes(sp);

  bool no_header_mode = Engine::pop_szl_bool(sp);
  szl_int buf_size = Engine::pop_szl_int(sp);

  string uncompressed;
  int res = ZLibUncompress(no_header_mode, buf_size, &uncompressed,
                           bytes_val->u_base(), bytes_val->length());
  bytes_val->dec_ref();
  const char* error = NULL;
  if (res == Z_OK) {
    BytesVal* result = Factory::NewBytesInit(proc, uncompressed.size(),
                                             uncompressed.data());
    Engine::push(sp, result);
  } else {
    error = proc->PrintError("zlib error: %d", res);
  }
  return error;
}


static const char zlibcompress_doc[] =
  "Compresses the zipped data using zlib, and returns the compressed data.";

static const char* zlibcompress(Proc* proc, Val**& sp) {
  BytesVal* bytes_val = Engine::pop_bytes(sp);

  bool no_header_mode = Engine::pop_szl_bool(sp);

  uLongf bufsize = ZLibMinCompressbufSize(bytes_val->length());
  unsigned char* compressed_array = new unsigned char[bufsize];
  int dest_len = 0;
  int res = ZLibCompress(no_header_mode, compressed_array, bufsize, &dest_len,
                         bytes_val->u_base(), bytes_val->length());
  bytes_val->dec_ref();
  const char* error = NULL;
  if (res == Z_OK) {
    BytesVal* compressed = Factory::NewBytes(proc, dest_len);
    char* output_data = compressed->base();
    memcpy(output_data, compressed_array, dest_len);
    Engine::push(sp, compressed);
  } else {
    error = proc->PrintError("zlib error: %d", res);
  }
  delete [] compressed_array;
  return error;
}


static const char gunzip_doc[] =
  "Decompress gzip compressed data. The data must contain a valid gzip header "
  "and footer (as in a .gz file), but data after the footer is ignored.";

static const char* gunzip(Proc* proc, Val**& sp) {
  BytesVal* argument = Engine::pop_bytes(sp);
  string uncompressed;
  const char* error = NULL;
  if (GunzipString(argument->u_base(), argument->length(), &uncompressed)) {
    BytesVal* result = Factory::NewBytesInit(proc, uncompressed.length(),
                                             uncompressed.c_str());
    Engine::push(sp, result);
  } else {
    error = proc->PrintError("gunzip: failed to decompress data");
  }

  argument->dec_ref();
  return error;
}


static const char gzip_doc[] = "Compress data using gzip.";

static void gzip(Proc* proc, Val**& sp) {
  BytesVal* argument = Engine::pop_bytes(sp);
  string compressed;
  GzipString(argument->u_base(), argument->length(), &compressed);
  argument->dec_ref();

  BytesVal* result = Factory::NewBytesInit(proc, compressed.length(),
                                           compressed.c_str());
  Engine::push(sp, result);
}

static void Initialize() {
  assert(SymbolTable::is_initialized());

  Proc* proc = Proc::initial_proc();

  Type* bytes_type = SymbolTable::bytes_type();
  Type* bool_type = SymbolTable::bool_type();

  // This dangling pointer will exist for the lifetime of the program.
  Literal* int_8192 = Literal::NewInt(proc, SymbolTable::init_file_line(),
                                      NULL, 8192);

#define DEF(name, type, attribute) \
  SymbolTable::RegisterIntrinsic(#name, type, name, name##_doc, attribute)

  { FunctionType* t =
      FunctionType::New(proc)
        ->par("compressed_data", bytes_type)
        ->par("skip_header", bool_type)
        ->opt(int_8192)
        ->res(bytes_type);
    DEF(zlibuncompress, t, Intrinsic::kNormal | Intrinsic::kThreadSafe);
  }
  { FunctionType* t =
      FunctionType::New(proc)
        ->par("uncompressed_data", bytes_type)
        ->par("skip_header", bool_type)
        ->res(bytes_type);
    DEF(zlibcompress, t, Intrinsic::kNormal | Intrinsic::kThreadSafe);
  }
  { FunctionType* unzip_type =
      FunctionType::New(proc)->par("compressed_data", bytes_type)->
      res(bytes_type);
    DEF(gunzip, unzip_type, Intrinsic::kNormal | Intrinsic::kThreadSafe);
  }
  { FunctionType* zip_type =
      FunctionType::New(proc)->par("uncompressed_data", bytes_type)->
      res(bytes_type);
    DEF(gzip, zip_type, Intrinsic::kNormal | Intrinsic::kThreadSafe);
  }
}
}  // namespace sawzall

REGISTER_MODULE_INITIALIZER(
  SawzallZipLib,
  { REQUIRE_MODULE_INITIALIZED(Sawzall);
    sawzall::Initialize();
  }
);
