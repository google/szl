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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <assert.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>


#include "engine/globals.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "utilities/strutils.h"
#include "utilities/timeutils.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/frame.h"
#include "engine/intrinsic.h"
#include "engine/proc.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"
#include "engine/engine.h"
#include "engine/ir.h"

namespace sawzall {

// Intrinsics' tuple types
static TupleType* resourcestats_type = NULL;

// fields of the resourcestats_type
static TupleField rs_f[] = {
  { "initializedavailablemem",    TypeInt },
  { "initializedallocatedmem",    TypeInt },
  { "initializedusertime",        TypeTime },
  { "initializedsystemtime",      TypeTime },
  { "availablemem",               TypeInt },
  { "allocatedmem",               TypeInt },
  { "usertime",                   TypeTime },
  { "systemtime",                 TypeTime },
};
static const int rs_field_count = sizeof(rs_f)/sizeof(rs_f[0]);
static int rs_ind[sizeof(rs_f)/sizeof(rs_f[0])];


// Support for tuple-valued intrinsics
Type* type_of[MaxTypeID];


// helper function to create a tuple field
static void add_field(Proc* proc, Scope* scope, const char* fname, Type* ftype) {
  Field* f = Field::New(proc, SymbolTable::init_file_line(), fname, ftype);
  scope->InsertOrDie(f);
}


static size_t GetSlotIndexForField(TupleType* type, szl_string field_name) {
  Field* field = type->scope()->Lookup(field_name)->AsField();
  assert(field != NULL);
  return field->slot_index();
}


// helper function to create all the fields in a tuple
TupleType* define_tuple(Proc* proc, const char* name, const TupleField* tuplefield, int* index, const int n) {
  Scope* scope = Scope::New(proc);
  for (int i = 0; i < n; i++)
    add_field(proc, scope, tuplefield[i].name, type_of[tuplefield[i].id]);
  TupleType* t = TupleType::New(proc, scope, false, false, true);
  // set up the indices
  for(int i = 0; i < n; i++)
    index[i] = GetSlotIndexForField(t, tuplefield[i].name);
  SymbolTable::RegisterType(name, t);
  return t;
}


// various helper functions for tuple field writing

void WriteIntSlot(Proc* proc, TupleVal* t, int index, szl_int value) {
  t->slot_at(index) = Factory::NewInt(proc, value);
}


void WriteFloatSlot(Proc* proc, TupleVal* t, int index, szl_float value) {
  t->slot_at(index) = Factory::NewFloat(proc, value);
}


void WriteTimeSlot(Proc* proc, TupleVal* t, int index, szl_time value) {
  t->slot_at(index) = Factory::NewTime(proc, value);
}


void WriteBoolSlot(Proc* proc, TupleVal* t, int index, bool value) {
  t->slot_at(index) = Factory::NewBool(proc, value);
}


void WriteStringSlot(Proc* proc, TupleVal* t, int index, szl_string value) {
  t->slot_at(index) = Factory::NewStringC(proc, value);
}


void WriteBytesSlot(Proc* proc, TupleVal* t, int index, szl_string value) {
  t->slot_at(index) = Factory::NewBytesC(proc, value);
}


void WriteArrayOfIntSlot(Proc* proc, TupleVal* t, int index, const int* a, int len) {
  ArrayVal* array = Factory::NewIntArray(proc, len);
  for(int i = 0; i < len; i++)
    array->at(i) = Factory::NewInt(proc, a[i]);
  t->slot_at(index) = array;
}

// Cuckoo-hashed regexp cache.
// Cuckoo hashing has significantly lower collision rates
// than standard hashing, and we also change the hash seeds
// if we see too many collisions.
//
// See http://en.wikipedia.org/wiki/Cuckoo_hashing

class RECacheEntry {
 public:
  void *compiled() { return compiled_; }
  const string& pattern() { return *re_; }

 private:
  friend class RECache;

  RECacheEntry(uint32 hash1, uint32 hash2, const string& re, void* compiled);
  ~RECacheEntry() {}

  bool Match(uint32 hash1, uint32 hash2, StringVal* pat);

  int32 ref_;
  int32 used_;
  uint32 hash1_;
  uint32 hash2_;
  string* re_;
  void *compiled_;
};

RECacheEntry::RECacheEntry(uint32 hash1, uint32 hash2, const string& re, void* compiled)
  : ref_(0),
    hash1_(hash1),
    hash2_(hash2),
    re_(new string(re)),
    compiled_(compiled) {
}

bool RECacheEntry::Match(uint32 hash1, uint32 hash2, StringVal* pat) {
  return hash1_ == hash1 &&
         hash2_ == hash2 &&
         re_->size() == pat->length() &&
         memcmp(&(*re_)[0], pat->base(), pat->length()) == 0;
}

class RECache : public IntrinsicCache {
 public:
  RECache();
  virtual ~RECache();
  RECacheEntry* Lookup(StringVal* pat, const char** errorp);
  void Release(RECacheEntry*);

 private:
  static const int kREMaxCache = 1<<10;
  bool Insert(RECacheEntry*);

  uint32 Hash1(char *p, int n);
  uint32 Hash2(char *p, int n);
  uint32 hash1seed_;
  uint32 hash2seed_;

  RECacheEntry** cache_;
  int ncache_;  // power of 2
  int nentry_;  // number of non-NULL cache_ slots
  int nflush_;
};

RECache *GetRECache(const szl_string& name, Proc *proc) {
  RECache *cache = static_cast<RECache*>(proc->Lookup(name));
  if (cache == NULL) {
    cache = new RECache;
    proc->Update(name, cache);
  }
  return cache;
}

RECache::RECache()
  : hash1seed_(kHashSeed32),
    hash2seed_(kHashSeed64),
    ncache_(64),
    nentry_(0),
    nflush_(0) {
  cache_ = new RECacheEntry*[ncache_];
  for (int i = 0; i < ncache_; i++)
    cache_[i] = NULL;
}

RECache::~RECache() {
  for (int i = 0; i < ncache_; i++)
    if (cache_[i] != NULL)
      Release(cache_[i]);
  delete[] cache_;
}

uint32 RECache::Hash1(char *p, int n) {
  return Hash32StringWithSeed(p, n, hash1seed_);
}

uint32 RECache::Hash2(char *p, int n) {
  return Hash32StringWithSeed(p, n, hash2seed_);
}

// Add "entry" to cache.
// Returns whether it was possible to add entry.
// Does incref on success.
bool RECache::Insert(RECacheEntry* entry) {
  // Add to cache.  Keep displacing other cache entries
  // until we hit a NULL entry or have looped too much.
  RECacheEntry* add = entry;
  entry->ref_++;
  int addpos = add->hash1_ & (ncache_ - 1);
  for (int i = 0; i < ncache_; i++) {
    RECacheEntry* e = cache_[addpos];
    cache_[addpos] = add;

    if (e == NULL) {
      // Wrote to an empty slot.
      nentry_++;
      return true;
    }

    // Displaced e.  Determine new pos.
    add = e;
    if (addpos == (add->hash1_ & (ncache_ - 1)))
      addpos = add->hash2_ & (ncache_ - 1);
    else
      addpos = add->hash1_ & (ncache_ - 1);
  }

  // Throw away the entry we're holding.
  Release(add);
  return false;
}

void RECache::Release(RECacheEntry* entry) {
  if (--entry->ref_ == 0) {
    FreeRegexp(entry->compiled_);
    delete entry->re_;
    delete entry;
  }
}

RECacheEntry* RECache::Lookup(StringVal* pat, const char** errorp) {
  // Look it up under both hash functions.
  uint32 hash1 = Hash1(pat->base(), pat->length());
  uint32 hash2 = Hash2(pat->base(), pat->length());
  RECacheEntry* entry1 = cache_[hash1 & (ncache_ - 1)];
  if (entry1 != NULL && entry1->Match(hash1, hash2, pat)) {
    entry1->used_ = 1;
    entry1->ref_++;
    return entry1;
  }
  RECacheEntry* entry2 = cache_[hash2 & (ncache_ - 1)];
  if (entry2 != NULL && entry2->Match(hash1, hash2, pat)) {
    entry1->used_ = 1;
    entry2->ref_++;
    return entry2;
  }

  // Not in cache.  Compile.
  string p(pat->base(), pat->length());
  void* compiled = CompileRegexp(p.c_str(), errorp);
  if (compiled == NULL)
    return NULL;
  RECacheEntry* retval = new RECacheEntry(hash1, hash2, p, compiled);
  retval->ref_ = 1;  // for caller
  retval->used_ = 1;

  if (Insert(retval))
    return retval;

  // We hit a cycle while adding to the cache.  Grow/rehash.
  // If there aren't that many entries in the cache, just rehash.
  int newsize;
  if (nentry_ < ncache_/2) {
    newsize = ncache_;
  } else {
    // Grow, because cache is at least half full.
    // Evict entries that were not used since the past K flushes.
    static const int K = 2;
    for (int i = 0; i < ncache_; i++) {
      RECacheEntry* e = cache_[i];
      if (e == NULL)
        continue;
      if (!e->used_) {
        Release(e);
        cache_[i] = NULL;
        nentry_--;
        continue;
      }
      if (++e->used_ > K)
        e->used_ = 0;
    }

    // If cache is small enough after eviction, don't grow.
    // The cutoff condition must be < ncache_/2, or more restrictive.
    // Otherwise you can end up in a steady state where,
    // say, the cache has 8 entries, you fill to 16,
    // evict back down to 8, etc., never growing.
    if (nentry_ < ncache_/2 || ncache_ >= kREMaxCache)
      newsize = ncache_;
    else
      newsize = ncache_ * 2;
  }

  // Log about regular expression cache for performance debugging.
  if (newsize > ncache_) {
    VLOG(1) << "Regular expression cache: grow to size=" << newsize;
    nflush_ = 0;
  } else {
    nflush_++;
    if ((nflush_ & (nflush_ - 1)) == 0) {  // log powers of two
      LOG(ERROR) << "Regular expression cache: size=" << ncache_
                 << " flush count=" << nflush_;
    }
  }

  // Grow/rehash.
  RECacheEntry** oldcache = cache_;
  int oldsize = ncache_;

  cache_ = new RECacheEntry*[newsize];
  ncache_ = newsize;
  nentry_ = 0;
  for (int i = 0; i < ncache_; i++)
    cache_[i] = NULL;

  hash1seed_ += 1000000007;  // arbitrary numbers
  hash2seed_ += 2000000011;

  for (int i = 0; i < oldsize; i++) {
    RECacheEntry* e = oldcache[i];
    if (e == NULL)
      continue;

    // Rehash and reinsert.
    // If the insert fails, just throw it away.
    e->hash1_ = Hash1(&(*e->re_)[0], e->re_->size());
    e->hash2_ = Hash2(&(*e->re_)[0], e->re_->size());
    Insert(e);
    Release(e);  // Insert took a ref if it wanted one.
  }
  delete[] oldcache;

  return retval;
}


// The intrinsics themselves

static const char abs_doc[] =
  "Return the absolute value of the argument. The type must be one of "
  "int or float.";

static void absint(Proc* proc, Val**& sp) {
  szl_int x = Engine::pop_szl_int(sp);
  if (x < 0)
    x = -x;
  Engine::push_szl_int(sp, proc, x);
}


static void absfloat(Proc* proc, Val**& sp) {
  szl_float x = Engine::pop_szl_float(sp);
  if (x < 0.0)
    x = -x;
  Engine::push_szl_float(sp, proc, x);
}


static const char szl_assert_doc[] =
  "If condition is false, print \"assertion failed\" to the standard "
  "error and then exit.  If a second string 'message' parameter is "
  "present, it is also printed to the standard error.";

static const char* szl_assert(Proc* proc, Val**& sp) {
  bool cond = Engine::pop_szl_bool(sp);
  StringVal* str = Engine::pop_string(sp);
  if (! cond) {
    if (str->length() > 0) {
      int n = str->length() + 1;
      char* s = (char*)malloc(n);
      str->c_str(s, n);
      F.fprint(2, "assertion failed: %s\n", s);
      free(s);
    } else
      F.fprint(2, "assertion failed\n");
    str->dec_ref();
    // terminate execution
    proc->set_error();
    return "assertion failed";
  }
  str->dec_ref();
  return NULL;
}



// TODO: probably should do some sanity checking on args & results of the add* intrinsics
static const char addday_doc[] =
  "Return the time n days after t. The value of n may be negative, "
  "or n may be absent altogether (addday(t)), in which case n defaults "
  "to 1. "
  "An optional third argument, a string, names a time zone.";

static const char* addday(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  szl_int delta = Engine::pop_szl_int(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  int microsec;
  if (!SzlTimeToLocalTime(time, tz, &tm, &microsec, NULL))
    return proc->PrintError("addday: invalid time or time zone %q "
                            "was not recognized", tz);
  tm.tm_mday += delta;
  szl_time t;
  if (!LocalTimeToSzlTime(tm, microsec, tz, false, &t))
    return proc->PrintError("addday: result time was out of range");
  Engine::push(sp, Factory::NewTime(proc, t));
  return NULL;
}


static const char addmonth_doc[] =
  "Like addday, but for months";

static const char* addmonth(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  szl_int delta = Engine::pop_szl_int(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  int microsec;
  if (!SzlTimeToLocalTime(time, tz, &tm, &microsec, NULL))
    return proc->PrintError("addmonth: invalid time or time zone %q "
                            "was not recognized", tz);
  tm.tm_mon += delta;
  szl_time t;
  if (!LocalTimeToSzlTime(tm, microsec, tz, false, &t))
    return proc->PrintError("addmonth: result time was out of range");
  Engine::push(sp, Factory::NewTime(proc, t));
  return NULL;
}


static const char addweek_doc[] =
  "Like addday, but for weeks.";

static const char* addweek(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  szl_int delta = Engine::pop_szl_int(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  int microsec;
  if (!SzlTimeToLocalTime(time, tz, &tm, &microsec, NULL))
    return proc->PrintError("addweek: invalid time or time zone %q "
                            "was not recognized", tz);
  tm.tm_mday += 7 * delta;
  szl_time t;
  if (!LocalTimeToSzlTime(tm, microsec, tz, false, &t))
    return proc->PrintError("addweek: result time was out of range");
  Engine::push(sp, Factory::NewTime(proc, t));
  return NULL;
}


static const char addyear_doc[] =
  "Like addday, but for years.";

static const char* addyear(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  szl_int delta = Engine::pop_szl_int(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  int microsec;
  if (!SzlTimeToLocalTime(time, tz, &tm, &microsec, NULL))
    return proc->PrintError("addyear: invalid time or time zone %q "
                            "was not recognized", tz);
  tm.tm_year += delta;
  szl_time t;
  if (!LocalTimeToSzlTime(tm, microsec, tz, false, &t))
    return proc->PrintError("addyear: result time was out of range");
  Engine::push(sp, Factory::NewTime(proc, t));
  return NULL;
}


static const char dayofweek_doc[] =
  "The numeric day of the week, from Monday=1 to Sunday=7. "
  "An optional second argument, a string, names a time zone.";

static const char* dayofweek(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  if (!SzlTimeToLocalTime(time, tz, &tm, NULL, NULL))
    return proc->PrintError("dayofweek: invalid time or time zone %q "
                            "was not recognized", tz);
  int day = tm.tm_wday;
  // Sunday is day 7
  if (day == 0)
    day = 7;
  Engine::push_szl_int(sp, proc, day);
  return NULL;
}


static const char dayofmonth_doc[] =
  "The numeric day of the month. "
  "An optional second argument, a string, names a time zone.";

static const char* dayofmonth(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  if (!SzlTimeToLocalTime(time, tz, &tm, NULL, NULL))
    return proc->PrintError("dayofmonth: invalid time or time zone %q "
                            "was not recognized", tz);
  Engine::push_szl_int(sp, proc, tm.tm_mday); // already 1-indexed
  return NULL;
}


static const char dayofyear_doc[] =
  "The numeric day of the year. January 1 is day 1. "
  "An optional second argument, a string, names a time zone.";

static const char* dayofyear(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  if (!SzlTimeToLocalTime(time, tz, &tm, NULL, NULL))
    return proc->PrintError("dayofyear: invalid time or time zone %q "
                            "was not recognized", tz);
  Engine::push_szl_int(sp, proc, tm.tm_yday + 1);
  return NULL;
}


static const char fingerprintof_doc[] =
  "Return the 64-bit fingerprint of the argument, which may be of any type. "
  "For a fingerprint of a string which is consistent with the C++ mechanisms, "
  "the string must first be converted from unicode to bytes.";

static void fingerprintof(Proc* proc, Val**& sp) {
  Val* v = Engine::pop(sp);
  szl_fingerprint fpr = v->Fingerprint(proc);
  v->dec_ref();
  Engine::push(sp, Factory::NewFingerprint(proc, fpr));
}


static const char format_doc[] =
  "Return a string containing the arguments formatted according to the "
  "format string fmt. The syntax of the format string is essentially that of "
  "ANSI C with the following differences:\n"
  "- %b prints a boolean, \"true\" or \"false\".\n"
  "- %c prints a (u)int as a Unicode character in UTF-8.\n"
  "- %k like %c with single quotes and backslash escapes for special characters.\n"
  "- %s prints a Sawzall string as UTF-8.\n"
  "- %q like %s with double quotes and backslash escapes for special characters.\n"
  "- %p prints a fingerprint, in the format 0x%.16x, which might change.\n"
  "- %t prints a time, in the format of the Unix function ctime without a newline.\n"
  "- %T prints a Sawzall type of the argument; %#T expands user-defined types.\n"
  "- %d/%i/%o/%u/%x/%X apply to a Sawzall (u)int and have no 'l' or 'h' modifiers.\n"
  "- %e/%f/%g/%E/%G apply to a Sawzall float and have no 'l' or 'h' modifiers.\n"
  "- does not support format verbs 'n' and '*'.\n";

static void format(Proc* proc, Val**& sp) {
  StringVal* afmt = Engine::pop_string(sp);
  Fmt::State f;
  F.fmtstrinit(&f);
  sp = Engine::Print(&f, afmt->base(), afmt->length(), proc, sp);
  char* s = F.fmtstrflush(&f);
  int len = strlen(s);
  StringVal* v = Factory::NewStringBytes(proc, len, s);
  free(s);
  afmt->dec_ref();
  // push string return result
  Engine::push(sp, v);
}


static const char haskey_doc[] =
  "Return a boolean reporting whether the key is present in the map.";

static void haskey(Proc* proc, Val**& sp) {
  MapVal* m = Engine::pop_map(sp);
  Map* map = m->map();
  Val *v = Engine::pop(sp);
  int index = map->Lookup(v);
  v->dec_ref();
  m->dec_ref();
  Engine::push_szl_bool(sp, proc, index >= 0);
}


static const char clearproto_doc[] =
  "The clearproto function clears a field in the proto buffer converted "
  "into the proto tuple containing the field f."
  "f must be of the form proto_tuple_var.field_name. Consequently, "
  "clearproto must only be applied to fields of proto tuples. "
  "clearproto will make a subsequent inproto() on the same field return false. "
  "However, the memory for this field will not be freed "
  "until the whole protocol buffer goes out of scope.";


static const char inproto_doc[] =
  "The inproto function tests whether the field f was present "
  "in the proto buffer converted into the proto tuple containing the field "
  "f. f must be of the form proto_tuple_var.field_name. Consequently, "
  "inproto must only be applied to fields of proto tuples. If the "
  "proto tuple field was set explicitly (e.g. via an assignment to that "
  "field) or by conversion from a proto buffer that contains an explicit "
  "value for that field, inproto returns true.";


static const char undefine_doc[] =
  "The ___undefine function undefines the variable provided as argument.";


static const char getenv_doc[] =
  "Return the entire contents of the named environment variable as an "
  "uninterpreted byte stream. Returns undef if the variable does not exist.";

static const char* getenv(Proc* proc, Val**& sp) {
  // First try to get the environment value from the Proc environment map,
  // if not present, get it from the global environment. This allows us to
  // set per thread environment variables - e.g. multiple mapper thread each
  // working on its own input file which is the value of SZL_INPUT environment
  // variable.
  StringVal* a = Engine::pop_string(sp);
  const char* name = proc->PrintError("%.*s", a->length(), a->base());
  const char* value = proc->env_value(name);
  if (value == NULL)
    value = ::getenv(name);
  a->dec_ref();
  // push the value, if any
  if (value != NULL) {
    Engine::push(sp, Factory::NewStringC(proc, value));
    return NULL;
  } else {
    return proc->PrintError("getenv: environment variable %.*q undefined",
                            a->length(), a->base());
  }
}


static const char highbit_doc[] =
  "Return an integer representing the bit position of the highest one bit "
  "in n. If n is zero, the result is 0; if n is 1, the result is 1, if n is 15, "
  "the result is 4, etc.";

static void highbit(Proc* proc, Val**& sp) {
  uint64 x = Engine::pop_szl_int(sp);
  int bit;
  if (!x)
    bit = 0;
  else {
    int32 v;
    if (x >= (1LL << 32)) {
      v = x >> 32;
      bit = 33;
    } else {
      v = x;
      bit = 1;
    }
    for (int i = 16; i > 0; i >>= 1) {
      if (v >> i) {
        v >>= i;
        bit += i;
      }
    }
  }
  Engine::push_szl_int(sp, proc, bit);
}


static const char hourof_doc[] =
  "The numeric hour of the day, from 0 to 23. Midnight is 0, 1AM is 1, etc. "
  "An optional second argument, a string, names a time zone.";

static const char* hourof(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  if (!SzlTimeToLocalTime(time, tz, &tm, NULL, NULL))
    return proc->PrintError("hourof: invalid time or time zone %q "
                            "was not recognized", tz);
  Engine::push_szl_int(sp, proc, tm.tm_hour);
  return NULL;
}


static const char keys_doc[] =
  "Return an array holding, in no particular order, the set of keys present "
  "in the map m.";

static void keys(Proc* proc, Val**& sp) {
  MapVal* m = Engine::pop_map(sp);
  ArrayType* keys_type = m->type()->as_map()->key_array_type();
  Map* map = m->map();
  const int num_keys = map->occupancy();
  ArrayVal* key_array = keys_type->form()->NewVal(proc, num_keys);
  map->GetKeys(key_array);
  m->dec_ref();
  Engine::push(sp, key_array);
}


static const char len_doc[] =
  "Return the number of elements in v, which must be an array or map or of "
  "type string or bytes. If string, the value is the number of Unicode "
  "characters in the string; if bytes, the number of bytes. If a map, the "
  "value is the number of distinct keys present.";

static void lenindexable(Proc* proc, Val**& sp) {
  IndexableVal* x = Engine::pop_indexable(sp);
  int length;
  if (x->is_string()) {
    length = x->as_string()->num_runes();
  } else {
    length = x->length();
  }
  x->dec_ref();  // must happen before push (the push will overwrite *x)
  Engine::push_szl_int(sp, proc, length);
}


static void lenmap(Proc* proc, Val**& sp) {
  MapVal* m = Engine::pop_map(sp);
  const int length = m->occupancy();
  m->dec_ref();  // must happen before push (the push will overwrite *m)
  Engine::push_szl_int(sp, proc, length);
}


// Check whether it's okay to read the specified file in this security context.
// If so return NULL; if not return an error string.
static const char* CheckFileReadPermissions(Proc* proc, const char* name) {
  if (proc->mode() & Proc::kSecure) {
    const vector<string>& disallowed_paths = proc->get_disallowed_read_paths();
    if (disallowed_paths.size() == 0) {
      // The default: block all loads.
      return proc->PrintError("file reads are disallowed in this context");
    }
    for (vector<string>::const_iterator iter = disallowed_paths.begin();
         iter != disallowed_paths.end();
         iter++) {
      if (strstr(name, iter->c_str()) != NULL) {
        return proc->PrintError(
            "file paths containing %q may not be read in this context",
            iter->c_str());
      }
    }
  }
  return NULL;
}


// Read entire contents of file.  Return value is error string.
const char* FileContents(Proc* proc, const char* name, string* contents) {
  const char* security_error = CheckFileReadPermissions(proc, name);
  if (security_error != NULL)
    return security_error;
  FILE* file = fopen(name, "r");
  if (file == NULL)
    return proc->PrintError("can't open %s: %r", name);
  struct stat status;
  if (fstat(fileno(file), &status) != 0)
    return proc->PrintError("can't stat %s: %r", name);
  off_t size = status.st_size;
  char* buffer = new char[size];
  off_t nbytes = fread(buffer, 1, size, file);
  fclose(file);
  if (nbytes != size) {
    delete [] buffer;
    return proc->PrintError("short read on %s: expected %lld; read %lld\n",
                       name, size, nbytes);
  }
  contents->assign(buffer, nbytes);
  delete [] buffer;
  return NULL;
}


static const char load_doc[] =
  "Return the entire contents of the named file as an uninterpreted byte "
  "stream. Returns undef if the file cannot be opened or read";

static const char* load(Proc* proc, Val**& sp) {
  string filename = Engine::pop_cpp_string(proc, sp);
  string contents;
  const char* err = FileContents(proc, filename.c_str(), &contents);
  if (err != NULL)
    return err;
  BytesVal *v = SymbolTable::bytes_form()->NewValInit(proc, contents.size(),
                                                      contents.c_str());
  Engine::push(sp, v);
  return NULL;
}


static const char lookup_doc[] =
  "Return the element of the map indexed by the key or, if there "
  "is no such element, the specified default value. Assuming the "
  "map, key, and value are defined, equivalent to (using C ?: "
  "notation): def(m[key])? m[key] : value, but more efficient.";

static void lookup(Proc* proc, Val**& sp) {
  MapVal* m = Engine::pop_map(sp);
  Map* map = m->map();
  Val* key = Engine::pop(sp);
  // get the index of the key
  int index = map->Lookup(key);
  key->dec_ref();
  // now do the extraction; we must always pop the value
  Val* val = Engine::pop(sp);
  if (index >= 0) {
    val->dec_ref();
    val = map->Fetch(index);
    val->inc_ref();
  }
  m->dec_ref();
  Engine::push(sp, val);
}


// Changing the case of a character may change the size of its
// UTF-8 representation.  As of Unicode 2.0, the culprits were:
// code  lowersize uppersize
//  130: 1         2
//  131: 2         1
//  17f: 2         1
// 1fbe: 3         2
// 2126: 2         3
// 212a: 1         3
// 212b: 2         3
// Coding style forbids putting the UTF-8 text in this file;
// a readable version of the table appears in
//   testdata/base/upperlower.szl
// Notice the change can go either way.
//
// This helper routine processes a string, changing case, and reports the
// lengths actually consumed and processed. It stops at the end of its input
// buffer, even though there may be more to process.  Client routines can use
// the information to retry if necessary.  The return value is the number of
// bytes necessary to process the complete input.

static int UpperLower(char* out, int out_len, int* out_processed,
                      const char* in, int in_len, int* in_processed,
                      Rune (*changecase)(Rune)) {
  // Run fast until near end of output buffer.
  const char* s = in;
  const char* in_end = in + in_len;
  char* d = out;
  char* out_end = out + out_len - (UTFmax - 1);
  while (s < in_end && d < out_end) {
    Rune r;
    int inw = FastCharToRune(&r, s);
    r = (*changecase)(r);
    int outw = runetochar(d, &r);
    s += inw;
    d += outw;
  }
  // Room for more in buffer? Approach end carefully.
  out_end = out + out_len;
  while (s < in_end && d < out_end) {
    Rune r;
    int inw = FastCharToRune(&r, s);
    r = (*changecase)(r);
    int outw = runelen(r);
    if (d + outw > out_end)
      break;
    runetochar(d, &r);
    s += inw;
    d += outw;
  }
  // This is as far as we can get; report results.
  *in_processed = s - in;
  *out_processed = d - out;
  int required = d - out;
  // Output buffer full, but there may be more to process.
  while (s < in_end) {
    Rune r;
    int inw = FastCharToRune(&r, s);
    r = (*changecase)(r);
    required += runelen(r);
    s += inw;
  }
  return required;
}


// Wrapper that puts it all together.
static void upperlowercase(Proc* proc, Val**& sp, Rune (*changecase)(Rune)) {
  StringVal* src = Engine::pop_string(sp);
  StringVal* res = Factory::NewString(proc, src->length(), src->num_runes());
  int in_processed;
  int out_processed;
  int required = UpperLower(res->base(), res->length(), &out_processed,
                            src->base(), src->length(), &in_processed,
                            changecase);
  if (required > out_processed) {
    StringVal* b = Factory::NewString(proc, required, src->num_runes());
    memmove(b->base(), res->base(), out_processed);
    res->dec_ref();
    res = b;
    int in_extra;
    int out_extra;
    out_processed += UpperLower(res->base() + out_processed,
                                res->length() - out_processed,
                                &out_extra,
                                src->base() + in_processed,
                                src->length() - in_processed,
                                &in_extra,
                                changecase);
    assert(in_processed + in_extra == src->length());
    assert(out_processed == required);
  } else {
    res->SetSubrange(proc, 0, required, src->num_runes());  // text may have shrunk
  }
  src->dec_ref();
  // push string return result
  Engine::push(sp, res);
}


static const char lowercase_doc[] =
  "Return the string s with all characters converted to lower case, "
  "as defined by Unicode. (Note: the results may not be what might "
  "be expected for characters high in the Unicode value set (FF10 to FFEE)).";

static void lowercase(Proc* proc, Val**& sp) {
  upperlowercase(proc, sp, tolowerrune);
}


// Common code for regex matching.
// TODO: use a better caching method; attach to literal?

// Count number of substrings needed to store return result.
// Usually the number will be small and we can use a local
// array, but if it's big, we'll allocate an array big enough and
// free it when we're done.  This method usually avoids the
// cost of allocation, while never hitting PCRE's problem of
// returning zero for a match if the return vector is too small.
static int NumRESubstr(StringVal* pat) {
  int n = 1;  // one for outer match.
  int len = pat->length();
  char* r = pat->base();
  for (int i = 0; i < len; i++)
    if (r[i] == '(')
      n++;
  return n;
}


static const int kNsub = 20;

// Return boolean
const char match_doc[] =
  "Search for a match of the regular expression r within s, and return "
  "a boolean value indicating whether a match was found. "
  "(The regular expression syntax is that of RE2.)";

const char* Intrinsics::Match(Proc* proc, Val**& sp, void* pattern) {
  RECache *re_cache = GetRECache("match", proc);
  StringVal* pat = Engine::pop_string(sp);
  StringVal* str = Engine::pop_string(sp);
  RECacheEntry* entry = NULL;
  if (pattern == NULL) {
    // lookup the regular expression
    const char* error;
    entry = re_cache->Lookup(pat, &error);
    if (entry == NULL)
      return proc->PrintError("match: compilation error in regular expression: %q", error);
    pattern = entry->compiled();
  }
  assert(pattern != NULL);
  int result = SimpleExecRegexp(pattern, str->base(), str->length());
  if (entry != NULL)
    re_cache->Release(entry);
  if (result < 0) {  // a internal regexp engine error occured
    return proc->PrintError(
      "match: internal regexp engine error: %d\n (pattern = %.*q, string = %.*q)",
      result, pat->length(), pat->base(), str->length(), str->base());
  }
  pat->dec_ref();
  str->dec_ref();
  assert(result == 0 || result == 1);
  Engine::push_szl_bool(sp, proc, result != 0);
  return NULL;
}


// Return array of ints
const char matchposns_doc[] =
  "Search for a match of the regular expression r within s, and return "
  "an array consisting of character positions within s defined by the match. "
  "Positions 0 and 1 of the array report the location of the match of the "
  "entire expression, subsequent pairs report the location of matches of "
  "successive parenthesized subexpressions.";

const char* Intrinsics::Matchposns(Proc* proc, Val**& sp, void* pattern) {
  RECache *re_cache = GetRECache("matchposns", proc);
  StringVal* pat = Engine::pop_string(sp);
  StringVal* str = Engine::pop_string(sp);
  RECacheEntry* entry = NULL;
  if (pattern == NULL) {
    // lookup the regular expression
    const char* error;
    entry = re_cache->Lookup(pat, &error);
    if (entry == NULL)
      return proc->PrintError("matchposns: compilation error in regular expression: %q", error);
    pattern = entry->compiled();
  }
  assert(pattern != NULL);
  int nvec = 2 * NumRESubstr(pat);  // 2* because we get pairs of positions.
  // Avoid allocation if possible.  We need an offset array for both runes and bytes.
  int rvec[kNsub];
  int bvec[kNsub];
  int* vecp = rvec;
  int* byte_vecp = bvec;
  if (nvec > kNsub) {
    vecp = new int[nvec];
    byte_vecp = new int[nvec];
  }
  // Make DualString so we can recover Rune offsets.
  DualString dual(str->base(), str->length(), str->num_runes());
  nvec = DualExecRegexp(pattern, &dual, vecp, byte_vecp, nvec);
  if (entry != NULL)
    re_cache->Release(entry);
  if (nvec < 0) {  // a internal regexp engine error occured
    return proc->PrintError(
      "matchposns: internal regexp engine error: %d\n (pattern = %.*q, string = %.*q)",
      nvec, pat->length(), pat->base(), str->length(), str->base());
  }
  ArrayVal* posns = Factory::NewIntArray(proc, nvec);
  for (int i = 0; i < nvec; i++)
    posns->at(i) = TaggedInts::MakeVal(vecp[i]);
  pat->dec_ref();
  str->dec_ref();
  Engine::push(sp, posns);
  // free if we allocated
  if (vecp != rvec)
    delete[] vecp;
  if (byte_vecp != bvec)
    delete[] byte_vecp;
  return NULL;
}


// Return array of strings
const char matchstrs_doc[] =
  "Search for a match of the regular expression r within s, and return an "
  "array of strings consisting of matched substrings of s. The 0th string "
  "is the entire match; following elements of the array hold matches of "
  "successive parenthesized subexpressions. This function is equivalent "
  "to using matchposns to find successive locations of matches and "
  "created array slices of s with the indices returned.";

const char* Intrinsics::Matchstrs(Proc* proc, Val**& sp, void* pattern) {
  RECache *re_cache = GetRECache("matchstrs", proc);
  StringVal* pat = Engine::pop_string(sp);
  StringVal* str = Engine::pop_string(sp);
  RECacheEntry* entry = NULL;
  if (pattern == NULL) {
    // lookup the regular expression
    const char* error;
    entry = re_cache->Lookup(pat, &error);
    if (entry == NULL)
      return proc->PrintError("matchstrs: compilation error in regular expression: %q", error);
    pattern = entry->compiled();
  }
  assert(pattern != NULL);
  int nvec = 2 * NumRESubstr(pat);
  // Avoid allocation if possible.  We need an offset array for both runes and bytes.
  int rvec[kNsub];
  int bvec[kNsub];
  int* vecp = rvec;
  int* byte_vecp = bvec;
  if (nvec > kNsub) {
    vecp = new int[nvec];
    byte_vecp = new int[nvec];
  }
  // Make DualString so we can recover Rune offsets.
  DualString dual(str->base(), str->length(), str->num_runes());
  nvec = DualExecRegexp(pattern, &dual, vecp, byte_vecp, nvec);
  if (entry != NULL)
    re_cache->Release(entry);
  if (nvec < 0) { // a internal regexp engine error occured
    return proc->PrintError(
      "matchstrs: internal regexp engine error: %d\n (pattern = %.*q, string = %.*q)",
      nvec, pat->length(), pat->base(), str->length(), str->base());
  }
  int nmatch = nvec / 2;
  ArrayVal* strs = Factory::NewStringArray(proc, nmatch);
  Val** ap = strs->base();
  for (int i = 0; i < nmatch; i++) {
    int rlen = vecp[2*i+1] - vecp[2*i];
    int blen = byte_vecp[2*i+1] - byte_vecp[2*i];
    // strange behavior of pcre: for null matches, the actual values in
    // the vector may be huge, but their difference will be zero.
    if (rlen == 0) {
      StringVal* a = Factory::NewString(proc, 0, 0);
      *ap++ = a;
    } else {
      assert(rlen > 0 && byte_vecp[2*i+1] <= str->length() && vecp[2*i+1] <= str->num_runes());
      str->inc_ref();  // NewSlice() calls dec_ref(), compensate for that.
      StringVal* a = SymbolTable::string_form()->NewSlice(proc, str, byte_vecp[2*i], blen, rlen);
      *ap++ = a;
    }
  }
  pat->dec_ref();
  str->dec_ref();
  Engine::push(sp, strs);
  // free if we allocated
  if (vecp != rvec)
    delete[] vecp;
  if (byte_vecp != bvec)
    delete[] byte_vecp;
  return NULL;
}


static const char minuteof_doc[] =
  "The numeric minute of the hour, from 0 to 59. "
  "An optional second argument, a string, names a time zone.";

static const char* minuteof(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  if (!SzlTimeToLocalTime(time, tz, &tm, NULL, NULL))
    return proc->PrintError("minuteof: invalid time or time zone %q "
                            "was not recognized", tz);
  Engine::push_szl_int(sp, proc, tm.tm_min);
  return NULL;
}


static const char monthof_doc[] =
  "The numeric month of the year.  January is 1. "
  "An optional second argument, a string, names a time zone.";

static const char* monthof(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  if (!SzlTimeToLocalTime(time, tz, &tm, NULL, NULL))
    return proc->PrintError("monthof: invalid time or time zone %q "
                            "was not recognized", tz);
  Engine::push_szl_int(sp, proc, tm.tm_mon + 1);
  return NULL;
}


static const char now_doc[] =
  "Return the current time at the moment of execution";

static void now(Proc* proc, Val**& sp) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  szl_time t = tv.tv_sec * 1000000LL + tv.tv_usec;
  Engine::push(sp, Factory::NewTime(proc, t));
}


static const char formattime_doc[] =
  "Return a string containing the time argument formatted according to the "
  "format string fmt. The syntax of the format string is the same as in "
  "ANSI C strftime. An optional third argument, a string, names a time zone.";

static const char* formattime(Proc* proc, Val**& sp) {
  string afmt = Engine::pop_cpp_string(proc, sp);
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));

  struct tm ttm;
  if (!SzlTimeToLocalTime(time, tz, &ttm, NULL, NULL))
    return proc->PrintError("formattime: invalid time or time zone %q "
                            "was not recognized", tz);

  char result[200];
  size_t len = strftime(result, sizeof(result) - 1, afmt.c_str(), &ttm);
  if (len == 0)
    return proc->PrintError("formattime: result too long");
  StringVal* v = Factory::NewStringBytes(proc, len, result);
  // push string return result
  Engine::push(sp, v);
  return NULL;
}


static const char getresourcestats_doc[] =
  "Return a tuple of type resourcestats containing resource "
  "usage statistics.  The first set of numbers reports the "
  "statistics after static initialization.  The second set "
  "reports the values consumed by processing the current "
  "input record.  The availablemem figure reports total size "
  "of the heap; allocatedmem is the amount in use on the heap.  "
  "Memory is measured in bytes, and time is measured in microseconds.";

static void getresourcestats(Proc* proc, Val**& sp) {
  // create a tuple
  TupleVal* t = resourcestats_type->form()->NewVal(proc, TupleForm::set_inproto);

  ResourceStats* init_r = proc->initialized_stats();
  ResourceStats* current_r = proc->current_stats();
  ResourceStats r(proc);

  // Get the values from immediately after initialization
  WriteIntSlot(proc, t, rs_ind[0], init_r->available_mem());
  WriteIntSlot(proc, t, rs_ind[1], init_r->allocated_mem());
  WriteTimeSlot(proc, t, rs_ind[2], init_r->user_time());
  WriteTimeSlot(proc, t, rs_ind[3], init_r->system_time());

  // Mem values for this record are just the available/allocated values.
  // The counters were reset after initialization.
  WriteIntSlot(proc, t, rs_ind[4], r.available_mem());
  WriteIntSlot(proc, t, rs_ind[5], r.allocated_mem());

  // Time values are delta from current record baseline
  WriteTimeSlot(proc, t, rs_ind[6], r.user_time() - current_r->user_time());
  WriteTimeSlot(proc, t, rs_ind[7], r.system_time() - current_r->system_time());

  // push the Tuple on the stack
  Engine::push(sp, t);
}


static const char secondof_doc[] =
  "The numeric second of the minute, from 0 to 59. "
  "An optional second argument, a string, names a time zone.";

static const char* secondof(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  if (!SzlTimeToLocalTime(time, tz, &tm, NULL, NULL))
    return proc->PrintError("secondof: invalid time or time zone %q "
                            "was not recognized", tz);
  Engine::push_szl_int(sp, proc, tm.tm_sec);
  return NULL;
}


static const char strfind_doc[] =
  "Search for the first occurrence of the literal string p within s and return "
  "the integer index of its first character, or -1 if it does not occur.";

static void strfind(Proc* proc, Val**& sp) {
  StringVal* lit_array = Engine::pop_string(sp);
  StringVal* str_array = Engine::pop_string(sp);
  int lit_len = lit_array->length();
  int str_len = str_array->length();
  if (lit_len > str_len) {
    lit_array->dec_ref();
    str_array->dec_ref();
    Engine::push_szl_int(sp, proc, -1);
    return;
  }
  int match_pos = 0;  // this is correct for empty string

  if (lit_len > 0) {
    match_pos = -1;  // -1 => no match found
    char* lit = lit_array->base();
    char* str = str_array->base();

    char* lit_end = &lit[lit_len];
    char* str_stop = &str[str_len - lit_len];  // last possible match position

    Rune first;
    lit += FastCharToRune(&first, lit);
    lit_len = lit_end - lit;
    // lit is now split into first char and rest

    for (int pos = 0; str <= str_stop; pos++) {
      Rune r;
      str += FastCharToRune(&r, str);
      if (r == first) {
        // first char matches; check the rest of the literal
        if (strncmp(str, lit, lit_len) == 0) {
          // match found
          match_pos = pos;
          break;
        }
      }
    }
  }

  lit_array->dec_ref();
  str_array->dec_ref();
  Engine::push_szl_int(sp, proc, match_pos);
}

static const char bytesfind_doc[] =
  "Search for the first occurrence of the literal bytes p within b and return "
  "the integer index of its first character, or -1 if it does not occur.";

static void bytesfind(Proc* proc, Val**& sp) {
  BytesVal* lit_array = Engine::pop_bytes(sp);
  BytesVal* bytes_array = Engine::pop_bytes(sp);
  int lit_len = lit_array->length();
  int bytes_len = bytes_array->length();
  if (lit_len > bytes_len) {
    lit_array->dec_ref();
    bytes_array->dec_ref();
    Engine::push_szl_int(sp, proc, -1);
    return;
  }
  int match_pos = 0;  // this is correct for empty bytes

  if (lit_len > 0) {
    match_pos = -1;  // -1 => no match found
    char* lit = lit_array->base();
    char* bytes = bytes_array->base();

    for (int pos = 0; pos <= bytes_len - lit_len; ++pos) {
      // Gotta use memcmp since either bytes might contain the \x00
      if (memcmp(bytes + pos, lit, lit_len) == 0) {
        // match found
        match_pos = pos;
        break;
      }
    }
  }

  lit_array->dec_ref();
  bytes_array->dec_ref();
  Engine::push_szl_int(sp, proc, match_pos);
}


// Find last rune in string by examining bit pattern at top of byte.
// If the top two bits are 10 this is a continuation byte and cannot
// start a rune.
static int LastRune(char* beg, char* end, char** position) {
  char* p = end;
  do
    p--;
  while (p > beg && (*p & 0xC0) == 0x80);
  *position = p;
  Rune r = *reinterpret_cast<unsigned char*>(p);
  if (r >= Runeself) {
    if (!fullrune(p, end-p))  // Only necessary if we can have bad UTF-8.
      return Runeerror;
    FastCharToRune(&r, p);
  }
  return r;
}


static const char strrfind_doc[] =
  "Search for the last occurrence of the literal string p within s and return"
  "the integer index of its first character, or -1 if it does not occur.";

static void strrfind(Proc* proc, Val**& sp) {
  StringVal* lit_array = Engine::pop_string(sp);
  StringVal* str_array = Engine::pop_string(sp);
  int lit_len = lit_array->length();
  int str_len = str_array->length();
  if (lit_len > str_len) {
    lit_array->dec_ref();
    str_array->dec_ref();
    Engine::push_szl_int(sp, proc, -1);
    return;
  }
  int match_pos = str_len;  // this is correct for empty string

  if (lit_len > 0) {
    match_pos = -1;  // -1 => no match found
    char* lit = lit_array->base();
    char* str = str_array->base();

    char* lit_end = &lit[lit_len];
    char* str_end = &str[str_len];
    char* str_stop = &str[lit_len];

    Rune last = LastRune(lit, lit_end, &lit_end);  // lit is split into last char and rest
    lit_len = lit_end - lit;

    // Even though we're scanning backwards, to keep the code simple
    // we can, after finding a match for the *last rune*, do a forward
    // comparison of the rest of the string starting from the *first byte*.
    for (int num_runes = 0; str_end >= str_stop; num_runes++) {
      Rune r = LastRune(str, str_end, &str_end);
      if (r == last) {
        if (strncmp(str_end - lit_len, lit, lit_len) == 0) {
          // match found
          match_pos = str_array->num_runes() - lit_array->num_runes() - num_runes;
          break;
        }
      }
    }
  }

  lit_array->dec_ref();
  str_array->dec_ref();
  Engine::push_szl_int(sp, proc, match_pos);
}

static const char bytesrfind_doc[] =
  "Search for the last occurrence of the literal bytes p within b and return"
  "the integer index of its first character, or -1 if it does not occur.";

static void bytesrfind(Proc* proc, Val**& sp) {
  BytesVal* lit_array = Engine::pop_bytes(sp);
  BytesVal* bytes_array = Engine::pop_bytes(sp);
  int lit_len = lit_array->length();
  int bytes_len = bytes_array->length();
  if (lit_len > bytes_len) {
    lit_array->dec_ref();
    bytes_array->dec_ref();
    Engine::push_szl_int(sp, proc, -1);
    return;
  }
  int match_pos = bytes_len;  // this is correct for empty bytes

  if (lit_len > 0) {
    match_pos = -1;  // -1 => no match found
    char* lit = lit_array->base();
    char* bytes = bytes_array->base();

    for (int pos = bytes_len - lit_len; pos >= 0; --pos) {
      // Gotta use memcmp since either bytes might contain the \x00
      if (memcmp(bytes + pos, lit, lit_len) == 0) {
        // match found
        match_pos = pos;
        break;
      }
    }
  }

  lit_array->dec_ref();
  bytes_array->dec_ref();
  Engine::push_szl_int(sp, proc, match_pos);
}


// Find locations of substrings p inside string of runes s.
// All non-overlapping starting positions are appended to v if find_all
// is true. If find_all is false, only the first position is appended.
// Returned indexes are byte offsets, not character offsets.
static const void FindSubstrings(StringVal* s, StringVal* p, vector<int> *v,
                                 bool find_all) {
  int str_len = s->length();
  int ptr_len = p->length();
  char* str = s->base();  // each search starts from str
  char* str_start = str;
  char* str_stop = &str[str_len - ptr_len + 1];  // no need to look further
  char* pattern = p->base();

  while (str < str_stop) {
    if (strncmp(str, pattern, ptr_len) == 0) {
      v->push_back(str - str_start);
      str += ptr_len;
      if (!find_all)
        break;
    } else {
      str++;
    }
  }
}


// strreplace(str: string, lit: string,
//            rep: string, replace_all: bool): string
// Return a copy of string str with non-overlapping instances of substring
// lit in str replaced by rep. If replace_all is false, then only the first
// found instance is replaced.
static const char strreplace_doc[] =
  "Return a copy of string str, with non-overlapping instances of substring "
  "lit in str replaced by rep. If replace_all is false, then only the first "
  "found instance is replaced.";

static void strreplace(Proc* proc, Val**& sp) {
  StringVal* str_array = Engine::pop_string(sp);
  StringVal* old_array = Engine::pop_string(sp);
  StringVal* new_array = Engine::pop_string(sp);
  bool replace_all = Engine::pop_szl_bool(sp);
  int str_len = str_array->length();
  int old_len = old_array->length();
  int new_len = new_array->length();

  if (old_len > 0 && str_len >= old_len) {
    vector<int> v;
    FindSubstrings(str_array, old_array, &v, replace_all);

    if (v.size() > 0) {
      int nlen = str_len + v.size()*(new_len - old_len);
      int num_runes = str_array->num_runes() + v.size()*(new_array->num_runes() - old_array->num_runes());
      StringVal* result =
          SymbolTable::string_type()->string_form()->NewVal(proc, nlen, num_runes);

      char* str_start = str_array->base();
      char* str_pos = str_start;  // current position in the str segment
      char* res_pos = result->base();  // current position in the result
      char* new_start = new_array->base();
      for (int i = 0; i < v.size(); i++) {
        nlen = v[i] - (str_pos - str_start);
        if (nlen > 0) {
          memmove(res_pos, str_pos, nlen);
          res_pos += nlen;
          str_pos += nlen;
        }
        memmove(res_pos, new_start, new_len);
        res_pos += new_len;
        str_pos += old_len;
      }
      nlen = str_len - (str_pos - str_start);
      if (nlen > 0)
        memmove(res_pos, str_pos, nlen);

      Engine::push(sp, result);
      str_array->dec_ref();
    } else {
      Engine::push(sp, str_array);  // do not copy string if old not found
    }
  } else {
    Engine::push(sp, str_array);
  }
  old_array->dec_ref();
  new_array->dec_ref();
}

static const char trunctoday_doc[] =
  "Truncate t to the zeroth microsecond of the day. Useful when "
  "creating variables indexed to a particular day, since all times in the day "
  "truncated with trunctoday will fold to the same value, which is the first "
  "time value in that day. "
  "An optional second argument, a string, names a time zone.";

static const char* trunctoday(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  if (!SzlTimeToLocalTime(time, tz, &tm, NULL, NULL))
    return proc->PrintError("trunctoday: invalid time or time zone %q "
                            "was not recognized", tz);
  tm.tm_sec = 0;
  tm.tm_min = 0;
  tm.tm_hour = 0;
  szl_time t;
  if (!LocalTimeToSzlTime(tm, 0, tz, false, &t))
    return proc->PrintError("trunctoday: result time was out of range");
  Engine::push(sp, Factory::NewTime(proc, t));
  return NULL;
}


static const char trunctohour_doc[] =
  "Like trunctoday, but truncate to the start of the hour.";

static const char* trunctohour(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  if (!SzlTimeToLocalTime(time, tz, &tm, NULL, NULL))
    return proc->PrintError("trunctohour: invalid time or time zone %q "
                            "was not recognized", tz);
  tm.tm_sec = 0;
  tm.tm_min = 0;
  szl_time t;
  if (!LocalTimeToSzlTime(tm, 0, tz, false, &t))
    return proc->PrintError("trunctohour: result time was out of range");
  Engine::push(sp, Factory::NewTime(proc, t));
  return NULL;
}


static const char trunctominute_doc[] =
  "Like trunctoday, but truncate to the start of the minute.";

static const char* trunctominute(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  if (!SzlTimeToLocalTime(time, tz, &tm, NULL, NULL))
    return proc->PrintError("trunctominute: invalid time or time zone %q "
                            "was not recognized", tz);
  tm.tm_sec = 0;
  szl_time t;
  if (!LocalTimeToSzlTime(tm, 0, tz, false, &t))
    return proc->PrintError("trunctominute: result time was out of range");
  Engine::push(sp, Factory::NewTime(proc, t));
  return NULL;
}


static const char trunctomonth_doc[] =
  "Like trunctoday, but truncate to the start of the month.";

static const char* trunctomonth(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  if (!SzlTimeToLocalTime(time, tz, &tm, NULL, NULL))
    return proc->PrintError("trunctomonth: invalid time or time zone %q "
                            "was not recognized", tz);
  tm.tm_sec = 0;
  tm.tm_min = 0;
  tm.tm_hour = 0;
  tm.tm_mday = 1;
  szl_time t;
  if (!LocalTimeToSzlTime(tm, 0, tz, false, &t))
    return proc->PrintError("trunctomonth: result time was out of range");
  Engine::push(sp, Factory::NewTime(proc, t));
  return NULL;
}


static const char trunctosecond_doc[] =
  "Like trunctoday, but truncate to the start of the second.";

static const char* trunctosecond(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  Engine::push(sp, Factory::NewTime(proc, time - time%1000000LL));
  return NULL;
}


static const char trunctoyear_doc[] =
  "Like trunctoday, but truncate to the start of the year.";

static const char* trunctoyear(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  if (!SzlTimeToLocalTime(time, tz, &tm, NULL, NULL))
    return proc->PrintError("trunctoyear: invalid time or time zone %q "
                            "was not recognized", tz);
  tm.tm_sec = 0;
  tm.tm_min = 0;
  tm.tm_hour = 0;
  tm.tm_mday = 1;
  tm.tm_mon = 0;
  szl_time t;
  if (!LocalTimeToSzlTime(tm, 0, tz, false, &t))
    return proc->PrintError("trunctoyear: result time was out of range");
  Engine::push(sp, Factory::NewTime(proc, t));
  return NULL;
}


static const char uppercase_doc[] =
  "Return the string s with all characters converted to upper case, "
  "as defined by Unicode.";

static void uppercase(Proc* proc, Val**& sp) {
  upperlowercase(proc, sp, toupperrune);
}


static const char yearof_doc[] =
  "The numeric year value, such as 2003. "
  "An optional second argument, a string, names a time zone.";

static const char* yearof(Proc* proc, Val**& sp) {
  szl_time time = Engine::pop_szl_time(sp);
  char tz[kMaxTimeZoneStringLen + 2];  // +2: prevent accidental matches
  Engine::pop_c_str(proc, sp, tz, sizeof(tz));
  struct tm tm;
  if (!SzlTimeToLocalTime(time, tz, &tm, NULL, NULL))
    return proc->PrintError("yearof: invalid time or time zone %q "
                            "was not recognized", tz);
  Engine::push_szl_int(sp, proc, tm.tm_year + 1900);
  return NULL;
}


static const char min_doc[] =
  "Return the minimum of v1 and v2. The type must be one of "
  "int, time, string, or float";

static void minint(Proc* proc, Val**& sp) {
  IntVal* x = Engine::pop(sp)->as_int();
  IntVal* y = Engine::pop(sp)->as_int();
  if (x->val() < y->val()) {
    y->dec_ref();
    Engine::push(sp, x);
  } else {
    x->dec_ref();
    Engine::push(sp, y);
  }
}


static const char max_doc[] =
  "Return the maximum of v1 and v2. The type must be one of "
  "int, time, string, or float";

static void maxint(Proc* proc, Val**& sp) {
  IntVal* x = Engine::pop(sp)->as_int();
  IntVal* y = Engine::pop(sp)->as_int();
  if (x->val() < y->val()) {
    x->dec_ref();
    Engine::push(sp, y);
  } else {
    y->dec_ref();
    Engine::push(sp, x);
  }
}


static void minfloat(Proc* proc, Val**& sp) {
  FloatVal* x = Engine::pop(sp)->as_float();
  FloatVal* y = Engine::pop(sp)->as_float();
  if (x->val() < y->val()) {
    y->dec_ref();
    Engine::push(sp, x);
  } else {
    x->dec_ref();
    Engine::push(sp, y);
  }
}


static void maxfloat(Proc* proc, Val**& sp) {
  FloatVal* x = Engine::pop(sp)->as_float();
  FloatVal* y = Engine::pop(sp)->as_float();
  if (x->val() < y->val()) {
    x->dec_ref();
    Engine::push(sp, y);
  } else {
    y->dec_ref();
    Engine::push(sp, x);
  }
}


static void mintime(Proc* proc, Val**& sp) {
  TimeVal* x = Engine::pop(sp)->as_time();
  TimeVal* y = Engine::pop(sp)->as_time();
  if (x->val() < y->val()) {
    y->dec_ref();
    Engine::push(sp, x);
  } else {
    x->dec_ref();
    Engine::push(sp, y);
  }
}


static void maxtime(Proc* proc, Val**& sp) {
  TimeVal* x = Engine::pop(sp)->as_time();
  TimeVal* y = Engine::pop(sp)->as_time();
  if (x->val() < y->val()) {
    x->dec_ref();
    Engine::push(sp, y);
  } else {
    y->dec_ref();
    Engine::push(sp, x);
  }
}

// String comparison helper
// TODO: this routine is close to the one in engine.cc - share code!

static int cmp_string(StringVal* x, StringVal* y) {
  int lx = x->length();
  int ly = y->length();
  int cmp = strncmp(x->base(), y->base(), min(lx, ly));
  if (cmp != 0)
    return cmp;
  else
    return lx - ly;
}


static void minstring(Proc* proc, Val**& sp) {
  StringVal* x = Engine::pop_string(sp);
  StringVal* y = Engine::pop_string(sp);
  if (cmp_string(x, y) < 0) {
    y->dec_ref();
    Engine::push(sp, x);
  } else {
    x->dec_ref();
    Engine::push(sp, y);
  }
}


static void maxstring(Proc* proc, Val**& sp) {
  StringVal* x = Engine::pop_string(sp);
  StringVal* y = Engine::pop_string(sp);
  if (cmp_string(x, y) > 0) {
    y->dec_ref();
    Engine::push(sp, x);
  } else {
    x->dec_ref();
    Engine::push(sp, y);
  }
}


// Compute the range of a quantifier index by calculating
// the intersection of the ranges of its n uses.
// The arguments are:
//   n  [ > 0 ]
//   min_1 max_1
//   ...
//   min_n max_n
static void CombineRange(Proc* proc, Val**& sp) {
  assert(sizeof(szl_int) == 8);  // Code depends on this property
  static const int kMax = 0x7FFFFFFF;
  int n = Engine::pop_szl_int(sp);
  assert(n > 0);
  // These variables are szl_int not int so we can shift them without a cast.
  szl_int min = 0;
  szl_int max = kMax;
  do {
    int m;
    m = Engine::pop_szl_int(sp);
    if (m > min)  // Choose highest minimum
      min = m;
    m = Engine::pop_szl_int(sp);
    if (m < max)  // Choose lowest maximum
      max = m;
  } while (--n > 0);
  assert(0 <= min && min < kMax);
  assert(0 <= max && max < kMax);
  Engine::push_szl_int(sp, proc, (max << 32) | min);
}


// A comparator for sorting map keys.  We use fingerprints because
// they avoid the need to write comparators for all possible Val types
// and all we need is uniqueness and a true order.
struct ValCompare : public binary_function<Val*, Val*, bool> {
  ValCompare(Proc* proc) : proc_(proc) {}
  bool operator()(Val* a, Val* b) {
    return a->Fingerprint(proc_) < b->Fingerprint(proc_);
  }

  Proc* proc_;
};


// Return an array of the set union of the keys of the n argument maps.
// Duplicates are discarded.
// The arguments are:
//   num_maps  [ > 0 ]
//   map_1
//   ...
//   map_n
static void CombineKeys(Proc* proc, Val**& sp) {
  // There are num_maps maps on the stack; the count is first.
  int num_maps = Engine::pop_szl_int(sp);
  assert(num_maps > 0);
  // Pop the maps off the stack and store in a temporary array.
  MapVal** maps = new MapVal*[num_maps];  // could avoid this by using the stack itself -TODO?
  int num_keys = 0;
  for (int i = 0; i < num_maps; i++) {
    maps[i] = Engine::pop(sp)->as_map();  // ref count unchanged for now; just moving the ptr
    assert(maps[i] != NULL);
    num_keys += maps[i]->occupancy();
  }
  // Create the memory for the resulting array of key values.
  // We assume the maps all have the same key type.
  ArrayType* key_type = maps[0]->type()->as_map()->key_array_type();
  ArrayVal* key_array = key_type->form()->NewVal(proc, num_keys);
  // Load the keys into the array.
  int base = 0;
  for (int i = 0; i < num_maps; i++) {
    MapVal* m = maps[i];
    for (int j = 0; j < m->occupancy(); j++) {
      Val* v = m->map()->GetKeyByIndex(j);
      key_array->at(base++) = v;
      v->inc_ref();
    }
    m->dec_ref();  // keys are copied; done with the map now
  }
  // Now sort them.  From a computing complexity standpoint it
  // would be better to sort them separately and merge them, but
  // that requires another round of allocation, so we just sort it all.
  sort(key_array->base(), key_array->end(), ValCompare(proc));
  // There may be duplicates, so cast them out.
  int from = 0;  // destination location of the copy pass
  int to = 0;  // source location of the copy pass
  while (from < num_keys) {
    // Invariants:
    //  - 'to' needs to be placed at 'from' (although it may already be there)
    //  - there is >= 1 equal values sequentially at 'from'
    //  - 'from' is the first such value
    //  - only one will be transferred
    if (from == to) {  // only copy if not the same location
      to++;
    } else {
      key_array->at(to)->dec_ref();
      key_array->at(to++) = key_array->at(from);
    }
    szl_fingerprint fprint = key_array->at(from++)->Fingerprint(proc);
    while(from < num_keys && fprint == key_array->at(from)->Fingerprint(proc))
      from++;
  }
  if (to < num_keys) {
    // The rest of the array has been copied down, so NULL out the tail of the array.
    for (int i = to; i < num_keys; i++)
      key_array->at(i) = NULL;
    // slice will overwrite key_array
    key_array = key_type->form()->NewSlice(proc, key_array, 0, to);
  }
  // Free temporary
  delete[] maps;
  Engine::push(sp, key_array);
}


// Used by when.cc to iterate over maps
static void GetKeyByIndex(Proc* proc, Val**& sp) {
  MapVal* m = Engine::pop_map(sp);
  int n = Engine::pop_szl_int(sp);
  Val* v = m->map()->GetKeyByIndex(n);
  v->inc_ref();
  m->dec_ref();
  Engine::push(sp, v);
}


// Companion to GetKeyByIndex.  Not actually used yet, but should be;
// it's efficient to use this method if you know the key offset.
static void GetValueByIndex(Proc* proc, Val**& sp) {
  MapVal* m = Engine::pop_map(sp);
  int n = Engine::pop_szl_int(sp);
  Val* v = m->map()->GetValueByIndex(n);
  v->inc_ref();
  m->dec_ref();
  Engine::push(sp, v);
}


// yuck-pooey.  a hack to allow segv under program controls
// to allow testing of recovery from segv
static const char* YUCK___raise_segv_doc =
  "The function ___raise_segv raises a SIGSEGV.  It is for testing only.";
static const char* YUCK___raise_segv(Proc* proc, Val**& sp) {
  raise(SIGSEGV);
  return NULL;  // shut up compiler
}


// Another hack to allow a regression test to get the Val* of a value
// so that it can verify that memory compaction is working correctly.
static const char addressof_doc[] =
  "The ___addressof function returns the address or smi of a value.";
static void addressof(Proc* proc, Val**& sp) {
  Val* v = Engine::pop(sp);
  v->dec_ref();
  Engine::push_szl_int(sp, proc, reinterpret_cast<szl_int>(v));
}


// Heap check, for debugging purposes only.
static const char heapcheck_doc[] =
  "The ___heapcheck function verifies the integrity of the execution heap.";
static void heapcheck(Proc* proc, Val**& sp) {
  proc->heap()->Check();
}


// -----------------------------------------------------------------------------
// Implementation of Intrinsics


void Intrinsics::Initialize() {
  // make sure the SymbolTable is initialized
  assert(SymbolTable::is_initialized());
  Proc* proc = Proc::initial_proc();

  // shortcuts for predefined types
  Type* bool_type = SymbolTable::bool_type();
  Type* bytes_type = SymbolTable::bytes_type();
  Type* fingerprint_type = SymbolTable::fingerprint_type();
  Type* float_type = SymbolTable::float_type();
  Type* int_type = SymbolTable::int_type();
  Type* string_type = SymbolTable::string_type();
  Type* time_type = SymbolTable::time_type();
  Type* void_type = SymbolTable::void_type();
  Type* array_of_int_type = SymbolTable::array_of_int_type();
  Type* array_of_float_type = SymbolTable::array_of_float_type();
  Type* array_of_string_type = SymbolTable::array_of_string_type();
  Type* incomplete_type = SymbolTable::incomplete_type();

  // fill in the type table
  type_of[TypeArrayOfInt] = array_of_int_type;
  type_of[TypeArrayOfFloat] = array_of_float_type;
  type_of[TypeBool] = bool_type;
  type_of[TypeBytes] = bytes_type;
  type_of[TypeFloat] = float_type;
  type_of[TypeInt] = int_type;
  type_of[TypeString] = string_type;
  type_of[TypeTime] = time_type;

  // shortcuts for some constants
  Literal* int_1 = SymbolTable::int_1();
  Literal* empty_string = SymbolTable::empty_string();

  // register tuple types
  resourcestats_type = define_tuple(proc, "resourcestats", rs_f, rs_ind, rs_field_count);

  // internal intrinsics

  // signature: (...): int (parameter type is not used)
  { FunctionType* t = FunctionType::New(proc)->res(int_type);
    SymbolTable::RegisterIntrinsic("$combinerange", t, CombineRange,
                                           SymbolTable::dummy_doc,
                                           Intrinsic::kNormal);
  }

  // signature: (...): KeyOrValueOfMapType (parameter type is not used)
  { FunctionType* t = FunctionType::New(proc)->res(incomplete_type);
    SymbolTable::RegisterIntrinsic("$getkeybyindex", t, GetKeyByIndex,
                                           SymbolTable::dummy_doc,
                                           Intrinsic::kNormal);
    SymbolTable::RegisterIntrinsic("$getvaluebyindex", t,
                                           GetValueByIndex,
                                           SymbolTable::dummy_doc,
                                           Intrinsic::kNormal);
  }

  // signature: (map, ...): array of KeyOrValueOfMapType
  { FunctionType* t = FunctionType::New(proc)->res(incomplete_type);
    SymbolTable::RegisterIntrinsic("$combinekeys", t, CombineKeys,
                                           SymbolTable::dummy_doc,
                                           Intrinsic::kNormal);
  }

  SymbolTable::RegisterIntrinsic("fingerprintof",
                                         Intrinsic::FINGERPRINTOF,
                                         fingerprint_type, fingerprintof,
                                         fingerprintof_doc,
                                         Intrinsic::kCanFold);
  SymbolTable::RegisterIntrinsic("format", Intrinsic::FORMAT,
                                         string_type,
                                         format, format_doc,
                                         Intrinsic::kCanFold);
  SymbolTable::RegisterIntrinsic("len", Intrinsic::LEN, int_type,
                                         SymbolTable::dummy_intrinsic_nofail,
                                         len_doc, Intrinsic::kCanFold);
  SymbolTable::RegisterIntrinsic("haskey", Intrinsic::HASKEY, bool_type,
                                         haskey, haskey_doc,
                                         Intrinsic::kNormal);
  SymbolTable::RegisterIntrinsic("inproto", Intrinsic::INPROTO,
                                         bool_type,
                                         SymbolTable::dummy_intrinsic_nofail,
                                         inproto_doc, Intrinsic::kNormal);
  SymbolTable::RegisterIntrinsic("clearproto", Intrinsic::CLEARPROTO,
                                         void_type,
                                         SymbolTable::dummy_intrinsic_nofail,
                                         clearproto_doc, Intrinsic::kNormal);
  SymbolTable::RegisterIntrinsic("___undefine", Intrinsic::UNDEFINE,
                                         void_type,
                                         SymbolTable::dummy_intrinsic_nofail,
                                         undefine_doc, Intrinsic::kNormal);

  // these intrinsics return variant types; incomplete_type is just a placeholder
  SymbolTable::RegisterIntrinsic("abs", Intrinsic::ABS, incomplete_type,
                                         SymbolTable::dummy_intrinsic_nofail,
                                         abs_doc, Intrinsic::kCanFold);
  SymbolTable::RegisterIntrinsic("keys", Intrinsic::KEYS,
                                         incomplete_type,
                                         keys, keys_doc, Intrinsic::kNormal);
  SymbolTable::RegisterIntrinsic("lookup", Intrinsic::LOOKUP,
                                         incomplete_type,
                                         lookup, lookup_doc,
                                         Intrinsic::kNormal);

  // general intrinsics
#define DEF(name, type, attr) \
  SymbolTable::RegisterIntrinsic(#name, type, name, name##_doc, attr)

  // signature: (bool, string = "assertion failed")
  { FunctionType* t = FunctionType::New(proc)->par("condition", bool_type)->
      opt(empty_string);
    SymbolTable::RegisterIntrinsic("assert", t, szl_assert,
                                           szl_assert_doc, Intrinsic::kNormal);
  }

  // min/max(float, float): float
  { FunctionType * t = FunctionType::New(proc)
        ->par("a", float_type)->par("b", float_type)
        ->res(float_type);
    SymbolTable::RegisterIntrinsic(
        "min", t, minfloat, min_doc, Intrinsic::kCanFold);
    SymbolTable::RegisterIntrinsic(
        "max", t, maxfloat, max_doc, Intrinsic::kCanFold);
  }

  // min/max(int, int): int
  { FunctionType * t = FunctionType::New(proc)
        ->par("a", int_type)->par("b", int_type)
        ->res(int_type);
    SymbolTable::RegisterIntrinsic(
        "min", t, minint, min_doc, Intrinsic::kCanFold);
    SymbolTable::RegisterIntrinsic(
        "max", t, maxint, max_doc, Intrinsic::kCanFold);
  }

  // min/max(time, time): time
  { FunctionType * t = FunctionType::New(proc)
        ->par("a", time_type)->par("b", time_type)
        ->res(time_type);
    SymbolTable::RegisterIntrinsic(
        "min", t, mintime, min_doc, Intrinsic::kCanFold);
    SymbolTable::RegisterIntrinsic(
        "max", t, maxtime, max_doc, Intrinsic::kCanFold);
  }

  // min/max(string, string): string
  { FunctionType * t = FunctionType::New(proc)
        ->par("a", string_type)->par("b", string_type)
        ->res(string_type);
    SymbolTable::RegisterIntrinsic(
        "min", t, minstring, min_doc, Intrinsic::kCanFold);
    SymbolTable::RegisterIntrinsic(
        "max", t, maxstring, max_doc, Intrinsic::kCanFold);
  }

  // signature: (time, int = 1, string = ""): time
  { FunctionType* t = FunctionType::New(proc)->par("t", time_type)->
      opt(int_1)->opt(empty_string)->res(time_type);
    DEF(addday, t, Intrinsic::kCanFold);
    DEF(addmonth, t, Intrinsic::kCanFold);
    DEF(addweek, t, Intrinsic::kCanFold);
    DEF(addyear, t, Intrinsic::kCanFold);
  }

  // signature: (time, string = ""): int
  { FunctionType* t = FunctionType::New(proc)->par("t", time_type)->
      opt(empty_string)->res(int_type);
    DEF(dayofmonth, t, Intrinsic::kCanFold);
    DEF(dayofweek, t, Intrinsic::kCanFold);
    DEF(dayofyear, t, Intrinsic::kCanFold);
    DEF(hourof, t, Intrinsic::kCanFold);
    DEF(minuteof, t, Intrinsic::kCanFold);
    DEF(monthof, t, Intrinsic::kCanFold);
    DEF(secondof, t, Intrinsic::kCanFold);
    DEF(yearof, t, Intrinsic::kCanFold);
  }

  // signature: (time, string = ""): time
  { FunctionType* t = FunctionType::New(proc)->par("t", time_type)->
      opt(empty_string)->res(time_type);
    DEF(trunctoday, t, Intrinsic::kCanFold);
    DEF(trunctohour, t, Intrinsic::kCanFold);
    DEF(trunctominute, t, Intrinsic::kCanFold);
    DEF(trunctomonth, t, Intrinsic::kCanFold);
    DEF(trunctosecond, t, Intrinsic::kCanFold);  // TZ not actually checked for now
    DEF(trunctoyear, t, Intrinsic::kCanFold);
  }

  // signature: (): time
  DEF(now, FunctionType::New(proc)->res(time_type), Intrinsic::kNormal);

  // signature: (string, time, string = ""): string
  { FunctionType* t = FunctionType::New(proc)->par("format", string_type)->
      par("t", time_type)->opt(empty_string)->res(string_type);
    DEF(formattime, t, Intrinsic::kCanFold);
  }

  // signature: (int): int
  { FunctionType* t = FunctionType::New(proc)->par("n", int_type)->
      res(int_type);
    DEF(highbit, t, Intrinsic::kCanFold);
  }

  // signature: (string): bytes
  { FunctionType* t = FunctionType::New(proc)->par("variable", string_type)->
      res(bytes_type);
    DEF(load, t, Intrinsic::kNormal);
  }

  // signature: (string): string
  { FunctionType* t = FunctionType::New(proc)->par("s", string_type)->
      res(string_type);
    DEF(getenv, t, Intrinsic::kNormal);
    DEF(lowercase, t, Intrinsic::kCanFold);
    DEF(uppercase, t, Intrinsic::kCanFold);
  }

  // signature: (string, string): bool
  { FunctionType* t = FunctionType::New(proc)->par("r", string_type)->
      par("s", string_type)->res(bool_type);
    SymbolTable::RegisterIntrinsic("match", Intrinsic::MATCH, t,
                                           SymbolTable::dummy_intrinsic,
                                           match_doc, Intrinsic::kCanFold);
  }

  // signature: (string, string): int
  { FunctionType* t = FunctionType::New(proc)->par("p", string_type)->
                      par("s", string_type)->
                      res(int_type);
    DEF(strfind, t, Intrinsic::kCanFold);
    DEF(strrfind, t, Intrinsic::kCanFold);
  }

  // signature: (bytes, bytes): int
  { FunctionType* t = FunctionType::New(proc)->par("p", bytes_type)->
                      par("b", bytes_type)->
                      res(int_type);
    DEF(bytesfind, t, Intrinsic::kCanFold);
    DEF(bytesrfind, t, Intrinsic::kCanFold);
  }

  // signature: (string, string, string, bool): string
  { FunctionType* t = FunctionType::New(proc)->par("str", string_type)->
                      par("lit", string_type)->
                      par("rep", string_type)->
                      par("replace_all", bool_type)->
                      res(string_type);
    DEF(strreplace, t, Intrinsic::kCanFold);
  }

  // signature: (string, string): array of int
  { FunctionType* t = FunctionType::New(proc)->par("r", string_type)->
      par("s", string_type)->res(array_of_int_type);
    SymbolTable::RegisterIntrinsic(
        "matchposns", Intrinsic::MATCHPOSNS, t,
        SymbolTable::dummy_intrinsic,
        matchposns_doc, Intrinsic::kNormal);
  }

  // signature: (string, string): array of string
  { FunctionType* t = FunctionType::New(proc)->par("r", string_type)->
      par("s", string_type)->res(array_of_string_type);
    SymbolTable::RegisterIntrinsic(
        "matchstrs", Intrinsic::MATCHSTRS, t,
        SymbolTable::dummy_intrinsic,
        matchstrs_doc, Intrinsic::kNormal);
  }

  // signature: (): resourcestats
  DEF(getresourcestats, FunctionType::New(proc)->res(resourcestats_type),
      Intrinsic::kNormal);


  // yuck.  raise a segv for testing.  see the function's comment.
  // Can't legally define a C++ symbol ___raise_segv() since all ids
  // beginning with __ are reserved for implementations.
  SymbolTable::RegisterIntrinsic(
      "___raise_segv", FunctionType::New(proc),
      YUCK___raise_segv, YUCK___raise_segv_doc,
      Intrinsic::kNormal);

  // Another hack for testing.
  SymbolTable::RegisterIntrinsic(
      "___addressof", Intrinsic::ADDRESSOF,
      int_type, SymbolTable::dummy_intrinsic_nofail,
      addressof_doc, Intrinsic::kNormal);

  // Heap check for debugging.
  SymbolTable::RegisterIntrinsic(
      "___heapcheck", Intrinsic::HEAPCHECK,
      FunctionType::New(proc), heapcheck,
      heapcheck_doc, Intrinsic::kNormal);

#undef DEF

}

// map variable type intrinsics to target functions
Intrinsic::CFunction Intrinsics::TargetFor(Proc* proc, Intrinsic* fun,
                                           const List<Expr*>* args) {
  // figure out the intrinsic based on the types of the arguments, if any
  Intrinsic::CFunctionCannotFail result = NULL;
  if (args->length() > 0) {
    Type* t = args->at(0)->type();
    switch (fun->kind()) {
      case Intrinsic::ABS:
        if (t->is_int())
          result = absint;
        else if (t->is_float())
          result = absfloat;
        break;
      case Intrinsic::ADDRESSOF:
        result = addressof;
        break;
      case Intrinsic::FINGERPRINTOF:
        result = fingerprintof;
        break;
      case Intrinsic::LEN:
        if (t->is_indexable())
          result = lenindexable;
        else if (t->is_map())
          result = lenmap;
        break;
      case Intrinsic::INTRINSIC: {
        // Check whether overloads match argument list
        // NOTE: currently only min()/max() supports this
        // TODO: support other built-in intrinsics also
        // Should only get into this case if:
        //   1.  the matching intrinsic is known to exist (e.g., "min")
        //   2.  intrinsic call has parsed successfully, i.e., arguments match
        //       function signature exactly.
        Intrinsic* match = NULL;
        for (Intrinsic* i = fun; i != NULL; i = i->next_overload()) {
          // candidate must be an Expr with type FunctionType
          FunctionType* ftype = i->type()->as_function();
          if (IR::IsMatchingFunctionArgList(proc, ftype, args)) {
            assert(match == NULL); // Multiple matches indicates ambiguous call.
            // First match found.
            // Continue loop to verify this is not an ambiguous call.
            match = i;
          }
        }
        if (match != NULL)
          fun = match;
        break;
      }
      default:
        break;
    }
  }

  if (result != NULL) {
    // We have a new target, make sure the Intrinsic properly described whether
    // the function can fail. Currently, none of these functions can fail.
    assert(!fun->can_fail());
    return (Intrinsic::CFunction) result;
  } else {
    assert(fun->function() != NULL);
    return fun->function();
  }
}

const char* Intrinsics::Saw(Proc* proc, Val**& sp, int regex_count, void** cache) {
  // cache is the address of a void* field that can be used for a regex cache -
  // currently it is initialized to NULL.
  // Warning: Since the cache is shared by all executables care must be taken
  // if we start caching dynamically-changing expressions (we don't at the
  // moment.)

  // Get the arguments
  // The stack looks like this:
  // saw_count (at top of stack)
  // flag[0]
  // flag[1]
  // ...
  // flag[regex_count-1]
  // regex[0]
  // regex[1]
  // ...
  // regex[regex_count-1]
  // str (will be overwritten)
  // result array (will be overwritten)
  //
  // When we return from Saw(), the stack is unchanged except that
  // the str and result array have been updated in place.

  RECache *re_cache = GetRECache("saw", proc);

  int64 saw_count = sp[0]->as_int()->val();
  IntVal** flag = reinterpret_cast<IntVal**>(&sp[1]);
  StringVal** regexp = reinterpret_cast<StringVal**>(&sp[1 + regex_count]);
  StringVal** strp = reinterpret_cast<StringVal**>(&sp[1 + regex_count + regex_count]);
  StringVal* str = *strp;  // Will be updated when we update *strp
  ArrayVal** resultp = reinterpret_cast<ArrayVal**>(&sp[1 + regex_count + regex_count + 1]);
  assert((*resultp)->is_unique());
  ArrayVal* result = *resultp;  // Will be updated when we update *resultp
  const char* rerror = NULL;

  // variables used in allocation
  const int input_nbytes = str->length(); // number of bytes in input
  int consumed_nbytes = 0;  // total consumed so far.
  int result_length = result->length();  // number of result strings so far
  int result_size = result_length;  // available space
  int new_elements = 0;  // number of new array elements produced
  // We should never reallocate more than once, so keep a bool
  // to check.
  bool reallocated = false;

  // Create efficient dual representation of the string to be sawn
  DualString dual(str->base(), str->length(), str->num_runes());

  // If *cache is 0, the regexes are dynamic and should not be cached.
  // If *cache is 1, the regexes are static and can be cached.
  // Any other value of *cache -> it points to the cached compiled exprs.
  RECacheEntry** entry = NULL;
  // TODO: for now we disable the inline cache!
  // This guarantees no other user of re_cache will free a pointer we hold.
  *cache = NULL;
  if (*cache == NULL || *cache == reinterpret_cast<void*>(1)) {
    // Compile the regular expressions
    entry = new RECacheEntry*[regex_count];  // explicitly deallocated
    for (int i = 0; i < regex_count; i++) {
      const char* error;
      entry[i] = re_cache->Lookup(regexp[i], &error);
      if (entry[i] == NULL) {
        for (int j = i - 1; j >= 0; j--)
          re_cache->Release(entry[j]);
        delete[] entry;
        return proc->PrintError(
            "saw: compilation error in regular expression `%.*q`: %s",
            regexp[i]->length(), regexp[i]->base(), error);
      }
    }
    // record the compiled expressions and cache if appropriate.
    if (*cache != NULL)
      *cache = static_cast<void*>(entry);
  } else {
    entry = reinterpret_cast<RECacheEntry**>(*cache);
  }

  // Calculate the maximum number of substrings we need
  // (they are memset() by underlying code, so stay small when possible).
  // We need one for outer string, one more for each parenthesized subexpr.
  // TODO: we could cache this.
  int nsubstr = 0;
  for (int i = 0; i < regex_count; i++) {
    int nparen = NumRESubstr(regexp[i]);
    if (nsubstr < nparen)
      nsubstr = nparen;
  }
  // allocate the return vector.  saw is expensive enough we might as well
  // always allocate it.
  int nvec = 2*nsubstr;
  int* vec = new int[nvec];  // explicitly deallocated
  int* byte_vec = new int[nvec];  // explicitly deallocated;

  // Run the nested loop
  char* pos = str->base();
  char* prev_pos = NULL;  // permit null match at start of string (=> make sure prev_pos != pos)
  for (int i = 0; i < saw_count; i++) {
    const char* start_pos = pos;
    assert(start_pos == str->base());
    for (int j = 0; j < regex_count; j++) {
  Retry:
      // initialize match array and report end of string to search
      int nsub = DualExecRegexp(entry[j]->compiled(), &dual, vec, byte_vec,
                                nvec);
      if (nsub < 0) { // a internal regexp engine error occured

        rerror = proc->PrintError(
          "saw: internal regexp engine error: %d\n",
          nsub);
        goto Exit;
      }
      if (nsub < 2) // we are completely done
        goto Exit;
      dual.Advance(byte_vec[1], vec[1]);
      pos = str->base() + byte_vec[1];
      if (pos == prev_pos && regex_count == 1) {
        // regex made no progress and we have only one pattern.
        // avoid generating another empty match. advance str (by overwriting)
        if (str->length() == 0)
          goto Exit;
        int w = dual.Advance(1);
        consumed_nbytes += w;
        pos += w;
        str = SymbolTable::string_form()->NewSlice(
            proc, str, w, str->length() - w, str->num_runes() - 1);
        goto Retry;
      } else if (flag[j]->val() == Saw::NONE || flag[j]->val() == Saw::SUBMATCH) {
        // If the result is a single match, use that.
        // If it has parenthesized submatches and asks for them, use those instead.
        // The vector is an array of pairs of indices delimiting the matches.
        // Zeroth pair is match to whole expression; subsequent are submatches.
        int i = 0;
        if (flag[j]->val() == Saw::SUBMATCH && nsub / 2 > 1)
          i += 2;  // skip the zeroth match
        else
          nsub = 2;  // take only the zeroth match
        for (; i < nsub; i += 2) {
          // create a result substring
          int match_len = byte_vec[i + 1] - byte_vec[i];
          str->inc_ref();  // NewSlice() calls dec_ref(), compensate for that.
          StringVal *a = SymbolTable::string_form()->NewSlice(proc, str, byte_vec[i], match_len, vec[i + 1] - vec[i]);
          // Append it to the result array.  if lucky, we already have room.
          // If not, we must grow the result array first.  We decide how big
          // to grow it by looking at the progress so far and using that as a
          // guide to what the total size will be.  The goal is to do only one
          // reallocation, to minimize wasted memory.
          if (result_length >= result_size) {
            int free_length;  // amount to add to array in this reallocation
            if (new_elements == 0) {
              // First time through loop; just pick a value.  Until we've been
              // through the loop a few times, we won't be able to pick a good
              // estimate, so we choose a value large enough to give us a
              // good chance to generate an accurate estimate when we do
              // need to resize.  If the result array is being reused because
              // Saw() is being called repeatedly (e.g.: saw(s, "a", rest x, "b")),
              // this will waste space by growing the array on each call, but
              // the only solution is to know the allocated size. This requires
              // significant changes to the calling convention and memory
              // allocator.  In any case, this only happens for non-looping
              // instances so we don't worry about it and just say: TODO.
              free_length = 500;
            } else if (reallocated || consumed_nbytes == 0) {
              // Complain; we shouldn't do this and our algorithm is failing
              F.fprint(2, "Warning: sawzall reallocation predictor failed\n");
              assert(result_size > 0);
              free_length = result_size;  // double the array size
            } else {
              // Estimate how much size we need by using average rate of
              // production of result strings per character of input, scaled by
              // the amount of input remaining, including the current piece.
              double estimate = (static_cast<double>(new_elements) /
                                static_cast<double>(consumed_nbytes))
                                * (input_nbytes - consumed_nbytes);
              // Use estimate plus 10% to be sure we don't do this more than once.
              // And then we add one final element just in case estimate == 0,
              // which can happen if we're adding a final element that matches a
              // null string at the end of the input.
              free_length = static_cast<int>(estimate * 1.1) + 1;
              reallocated = true;
            }
            result_size += free_length;
            result = Factory::NewStringArray(proc, result_size);
            memmove(result->base(), (*resultp)->base(),
                    result_length * sizeof(StringVal*));
            memset((*resultp)->base(), '\0',
                   result_length * sizeof(StringVal*));
            (*resultp)->dec_ref();  // account for abandoning this reference
            *resultp = result;
          }
          result->SetRange(result->origin(), result_length + 1);
          result->at(result_length) = a;
          result_length++;
          new_elements++;
        }
      }
      // advance str (by overwriting)
      prev_pos = pos;
      str = SymbolTable::string_form()->NewSlice(proc, str, byte_vec[1], str->length() - byte_vec[1], str->num_runes() - vec[1]);
      consumed_nbytes += byte_vec[1];
    }
    if (regex_count > 1 && start_pos == pos) {
      // made no progress this iteration; throw away the empty strings
      for (int k = 0; k < regex_count; k++)
        result->at(result->length()-k-1)->dec_ref();
      result->SetRange(result->origin(), result->length() - regex_count);
      break;
    }
  }
Exit:
  // free compiled regular expressions.
  // remove regexes, flags, and count
  // str and results have correct ref counts.
  // decrement ref on all the regexes.
  if (*cache == NULL) {
    // free regexes not being cached
    for (int i = 0; i < regex_count; i++) {
      re_cache->Release(entry[i]);
      regexp[i]->dec_ref();
    }
    delete[] entry;
  }
  delete[] vec;
  delete[] byte_vec;
  sp += regex_count + regex_count + 1;
  *strp = str;
  return rerror;
}


}  // namespace sawzall
