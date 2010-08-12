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

// Typically, these routines will all be os, and possibly processor,
// specific.  Every routine should thus be protected by ifdefs so
// that programs won't compile if these routines are run on a
// processor/OS that haven't been supported yet.

#include <config.h>  // for OS_LINUX

// We need PRId32 and PRId64, which are only defined if we explicitly ask for it
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <string>

// These includes may be incomplete for non-linux systems.
#if defined OS_FREEBSD || defined OS_MACOSX
#include <sys/sysctl.h>       // how fbsd and os x figure things out
#elif defined OS_LINUX
#include <linux/kernel.h>     // for sysinfo(2)
#include <linux/unistd.h>
#endif

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/szlmutex.h"
#include "utilities/sysutils.h"


namespace sawzall {


const int kMaxProcFileSize = 1024;  // arbitrary, but currently OK

// ----------------------------------------------------------------------
// PhysicalMem()
//    The amount of physical memory (RAM) a machine has.
//    Returns 0 if it couldn't figure out the memory.
// ----------------------------------------------------------------------

static uint64 PhysicalMemInternal(void) {
#if defined OS_FREEBSD || defined OS_MACOSX
  int mib[2] = {CTL_HW, HW_PHYSMEM};
  uint64 cchMem; /* 8 bytes supported on little endian machines */
  size_t cchMemLen = sizeof(cchMem);
  if ( sysctl(mib, sizeof(mib)/sizeof(*mib),
              &cchMem, &cchMemLen, NULL, 0) != 0 )
    return 0;
  else
    return cchMem;
#elif defined OS_WINDOWS || defined OS_CYGWIN
  MEMORYSTATUS stat;
  GlobalMemoryStatus(&stat);
  return stat.dwTotalPhys;
#elif defined OS_LINUX
  // Maybe this should use sysinfo().
  const char kFieldName[] = "MemTotal:";
  int fd = open("/proc/meminfo", O_RDONLY);
  CHECK_GE(fd, 0);
  char buffer[kMaxProcFileSize];
  CHECK_GT(read(fd, buffer, sizeof(buffer)), 0);
  char* p = strstr(buffer, kFieldName);
  CHECK(p != NULL);
  uint64 psize;
  CHECK_EQ(sscanf(p + sizeof(kFieldName) - 1, "%"PRIu64" kB", &psize), 1);
  return psize * 1024;
#else
  long physical_pages = sysconf( _SC_PHYS_PAGES );
  long physical_page_size = sysconf( _SC_PAGE_SIZE);
  if ( physical_pages <= 0 ) {
    LOG(ERROR) << "Physical number of pages could not be obtained";
    return 0;
  }
  if ( physical_page_size <= 0 ) {
    LOG(ERROR) << "PhysicalMem", "Physical page size could not be obtained";
    return 0;
  }
  return (static_cast<uint64>(physical_pages)) * physical_page_size;
#endif
}


uint64 PhysicalMem() {
  static SzlMutex physical_mem_lock;
  static uint64 physical_mem = 0;
  static bool physical_mem_initialized = false;
  SzlMutexLock l(&physical_mem_lock);
  if (!physical_mem_initialized) {
    physical_mem = PhysicalMemInternal();
    physical_mem_initialized = true;
  }
  return physical_mem;
}


// ----------------------------------------------------------------------
// VirtualMemorySize()
//    Returns the virtual memory size of this process.
//    Returns -1 on error.
//    We get this information from /proc/self/stat
// ----------------------------------------------------------------------

int64 VirtualProcessSize() {
#ifdef OS_LINUX
  static int fd = -1;
  {
    static SzlMutex lock;
    SzlMutexLock fdlock(&lock);
    if (fd < 0) {
      fd = open("/proc/self/stat", O_RDONLY);
      CHECK_GE(fd, 0);
    }
  }
  lseek(fd, 0, SEEK_SET);
  char buffer[kMaxProcFileSize];
  CHECK_GT(read(fd, buffer, sizeof(buffer)), 0);
  char* p = buffer;
  for (int i = 0; i < 22; i++) {
    p = strchr(p, ' ');
    CHECK(p != NULL);
    p++;
  }
  int64 vsize;
  if (sscanf(p, "%lld", &vsize) < 1) {
    LOG(ERROR) << "error reading virtual memory size";
    return -1;
  } else {
    return vsize;
  }
#else
  return -1;
#endif
}



// ----------------------------------------------------------------------------
// System.

bool RunCommand(const char* command, string* stdout_contents) {
  stdout_contents->clear();
  FILE* pipe = popen(command, "r");
  if (pipe == NULL) {
    fprintf(stderr, "Error running %s", command);
    abort();
  }

  char buf[4096];
  while (true) {
    int bytes_read = fread(buf, 1, sizeof(buf), pipe);
    if (bytes_read <= 0)
      break;
    stdout_contents->append(buf, bytes_read);
  }

  return (pclose(pipe) == 0);
}


uint64 CycleClockNow() {
#ifdef __i386
  uint64 val;
  __asm__ __volatile__("rdtsc" : "=A"(val));
  return val;
#elif defined(__x86_64__)
  int64 low, high;
  __asm__ volatile ("rdtsc\n"
      "shl $0x20, %%rdx"
      : "=a" (low), "=d" (high));
    return high | low;
#else
    return Microseconds();
#endif
}


}  // namespace sawzall
