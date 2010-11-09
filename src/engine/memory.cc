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
#include <time.h>
#include <config.h>

#ifdef HAVE_MALLINFO
#include <malloc.h>   // for mallinfo()
#endif

#include "engine/globals.h"
#include "public/commandlineflags.h"
#include "public/logging.h"

#include "utilities/sysutils.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/proc.h"
#include "engine/frame.h"
#include "engine/gctrigger.h"

// These symbols enable debugging code.
// #define SZL_MEMORY_DEBUG
// #define OVERWRITE_ON_FREE


DEFINE_bool(sawzall_mm_checks, true, "enable additional memory manager checks");


namespace sawzall {


// -----------------------------------------------------------------------------

// Implementation of Chunk

class Memory::Chunk {
 public:
  explicit Chunk(size_t size, int alignment_offset);
  ~Chunk();

  void* Alloc(size_t size);
  void SetMark() { mark_ = top_; }
  void ReleaseToMark() { top_ = mark_; }
  char* top() const { return top_; }
  char* mark() const { return mark_; }
  size_t size() const { return size_; }
  char* data() const { return data_; }
  char* end() const { return data_ + size_; }
  void set_top(char* top) { top_ = top; }
  size_t free() const  { return data_ + size_ - top_; }

  bool empty() const { return top_ == data_; }

  // for temporary use during compaction
  int move_block_count() const { return move_block_count_; }
  void set_move_block_count(int count) { move_block_count_ = count; }
  size_t* block_sizes() const { return block_sizes_; }
  void set_block_sizes( size_t* sizes) { block_sizes_ = sizes; }

 private:
  size_t size_;           // total size available for allocation
  char* allocated_data_;  // unaligned data (see destructor)
  char* data_;            // aligned s.t. (block + alignment_offset) is aligned
  char* top_;             // next byte to allocate
  char* mark_;            // saved value of top_ for Mark/Release

  int move_block_count_;  // count of allocated blocks to move, for compaction
  size_t* block_sizes_;   // sizes of allocated blocks to move, for compaction
};


Memory::Chunk::Chunk(size_t size, int alignment_offset) {
  // Note that we will get a bit less than "size" free space because
  // of alignment.  We do not want to increase the size for alignment because
  // the requested chunk size should already be a multiple of the
  // allocator page size and increasing it would waste an entire page.
  // Skip leading bytes so that the byte at offset alignment_offset into the
  // block will be aligned.
  size_t skip = Align(alignment_offset, kAllocAlignment) - alignment_offset;
  assert(size > skip + kAllocAlignment);  // see unsigned subtracts below
  size_ = size - skip;

  // Individual block lengths are rounded up to the alignment so that
  // consecutive blocks have the same alignment offset.  So we round up the
  // total size (not counting the initial skip) to the alignment.
  size_ = Align(size_ - kAllocAlignment + 1, kAllocAlignment);

  // Allocate the chunk and apply the alignment offset.
  assert(skip + size_ <= size);
  allocated_data_ = new char[skip + size_];  // explicitly deallocated

  // We require that the allocator aligns for us.  If this ever changes
  // then we must align here, being careful to decrease the effective
  // allocation (see above note about not increasing the size).
  assert(Align(reinterpret_cast<uintptr_t>(allocated_data_), kAllocAlignment) ==
         reinterpret_cast<uintptr_t>(allocated_data_));
  data_ = allocated_data_ + skip;

  // make memory available
  top_ = data_;
  mark_ = data_;  // so GC works on static data prior to SetMark()
}


Memory::Chunk::~Chunk() {
  delete[] allocated_data_;  // must use the original pointer from new[].
}


void* Memory::Chunk::Alloc(size_t size) {
  void* p = NULL;
  assert(size == Align(size, kAllocAlignment));  // caller must align
  if (size <= free()) {
    p = top_;
    top_ = top_ + size;
  }
  return p;
}


// -----------------------------------------------------------------------------

// SmallBlock, LargeBlock and SmallBlockFreeList.

// SmallBlock and LargeBlock are the headers of blocks allocated from Chunks
// and from malloc, respectively.  When a block is allocated the appropriate
// header is used.  Large blocks are freed using free().  Small blocks are
// just marked unallocated and remain in a chunk until the chunk is compacted.
// The free list is a cache for locating unallocated blocks in the chunks;
// it is discarded and rebuilt periodically when additional blocks are marked
// unallocated because their reference counts are zero.

// Note that it must be possible to access the members of SmallBlock and
// LargeBlock when the *end* of the object is aligned correctly in memory.
// The size_and_flags members must be the same distance from the end of the
// object in both cases so we can use the flags to distinguish the two kinds
// of blocks.  The size of of LargeBlock must be an alignment multiple.
// We use "uintptr_t" for size_and_flags so that the above requirements
// are satisfied on both 32-bit and 64-bit machines.

// Indicates a small or large block is refcounted.
const uintptr_t kRefCountFlag = (1 << 0);
// Indicates a small or large block is free.
const uintptr_t kAllocatedFlag = (1 << 1);
// All flags.
const uintptr_t kAllFlags = (kRefCountFlag | kAllocatedFlag);

struct Memory::SmallBlock {        // header of a small block
  uintptr_t size_and_flags;
  bool allocated() { return (size_and_flags & kAllocatedFlag) != 0; }
  bool ref_counted() { return (size_and_flags & kRefCountFlag) != 0; }
  bool allocated_and_refcounted() {
    return (size_and_flags & (kAllocatedFlag | kRefCountFlag)) ==
                             (kAllocatedFlag | kRefCountFlag);
  }
  void clear_allocated() { size_and_flags &=
                             ~(kAllocatedFlag | kRefCountFlag); }
  ptrdiff_t size() { return size_and_flags & ~kAllFlags; }
  void set_size(ptrdiff_t size) {
    assert((size & kAllFlags) == 0);
    size_and_flags = size | (size_and_flags & kAllFlags);
  }
};


struct Memory::FreeSmallBlock : SmallBlock {    // small block in free list
  FreeSmallBlock* next;
};


struct Memory::LargeBlock {        // header of a large block
  LargeBlock* next;
  uintptr_t size_and_flags;
  bool allocated() { return (size_and_flags & kAllocatedFlag) != 0; }
  bool ref_counted() { return (size_and_flags & kRefCountFlag) != 0; }
  bool allocated_and_refcounted() {
    return (size_and_flags & (kAllocatedFlag | kRefCountFlag)) ==
                             (kAllocatedFlag | kRefCountFlag);
  }
  void clear_allocated() { size_and_flags &=
                             ~(kAllocatedFlag | kRefCountFlag); }
  size_t size() { return size_and_flags & ~kAllFlags; }
};


class Memory::SmallBlockFreeList {
 public:
  SmallBlockFreeList(size_t min_block_size, size_t max_block_size) :
      min_block_size_(min_block_size),
      max_block_size_(max_block_size) {
    // There must be room for the "next" value in each block.
    assert(sizeof(SmallBlock) + min_block_size >= sizeof(FreeSmallBlock));
    num_buckets_ = bits(max_block_size) + 1;
    lists_ = new FreeSmallBlock*[num_buckets_];
    Clear();
  }
  ~SmallBlockFreeList() { delete[] lists_; }

  void Clear() { memset(lists_, '\0', num_buckets_ * sizeof(lists_[0])); }
  void AddFreeBlock(FreeSmallBlock* block);
  void* Alloc(size_t* size);
 private:
  static int bits(size_t x) {
    int count = 0;
    while (x != 0) {
      count++;
      x >>= 1;
    }
    return count;
  }
  FreeSmallBlock** lists_;
  int min_bits_;
  int num_buckets_;
  const size_t min_block_size_;
  const size_t max_block_size_;
};


void Memory::SmallBlockFreeList::AddFreeBlock(FreeSmallBlock* block) {
  int bucket = bits(block->size());
  assert(bucket >= 0 && bucket < num_buckets_);
  FreeSmallBlock*& list = lists_[bucket];
  block->next = list;
  list = block;
}


void* Memory::SmallBlockFreeList::Alloc(size_t* size) {
  int bucket = bits(*size);
  assert(bucket >= 0 && bucket < num_buckets_);

  // Start with the list for the next larger bucket size, so that all blocks in
  // the list are guaranteed to be sufficiently large.  We skip the list for 
  // the same bucket size because it is possible for the list to grow very
  // long and still not contain a sufficiently large block, making allocation
  // extremely slow.
  // If there is enough excess space the block is split so the excess becomes
  // available for smaller allocations.
  for (bucket++; bucket < num_buckets_; bucket++) {
    FreeSmallBlock*& list = lists_[bucket];
    if (list) {
      FreeSmallBlock* small = list;
      list = list->next;
      assert(small->size() >= *size);
      if ((small->size() - *size) >= min_block_size_) {
        // Unused part sufficiently large, split it and put remainder back.
        void* result =
          reinterpret_cast<char*>(small) + small->size() - *size;
        small->size_and_flags -= *size;  // both flags are false anyway
        AddFreeBlock(small);
        return result;
      } else {
        // No split; adjust caller's block size - holes are not allowed.
        *size = small->size();
        return small;
      }
    }
  }

  return NULL;  // no block found
}

// -----------------------------------------------------------------------------

//  Implementation of Memory


Memory::Memory(Proc* proc)
  : proc_(proc),
    post_mark_(false),
    mark_count_(0),
    large_premark_blocks_(NULL),
    large_postmark_blocks_(NULL),
    using_free_list_(false),
    small_alloc_since_last_free_(0),
    large_alloc_since_last_free_(0),
    max_process_size_(0),
    gctrigger_(NULL) {

  set_memory_limit(0);  // default to machine's physical memory size
  ResetCounters();

  // Determine the allocation chunk size.
  if ((proc->mode() & Proc::kPersistent) != 0) {
    // Compilation uses the minimum chunk size to prevent wasting space,
    // especially in the event that the initialization is incidental.
    chunk_size_ = kMinHeapChunkSize * 1024;
  } else {
    // Execution chunk size is adjustable by command line flag.
    if (FLAGS_heap_chunk_size < kMinHeapChunkSize) {
      FLAGS_heap_chunk_size = kMinHeapChunkSize;
      LOG(ERROR) << "--heap_chunk_size adjusted up to " << kMinHeapChunkSize;
    }
    if (FLAGS_heap_chunk_size > kMaxHeapChunkSize) {
      FLAGS_heap_chunk_size = kMaxHeapChunkSize;
      LOG(ERROR) << "--heap_chunk_size adjusted down to " << kMaxHeapChunkSize;
    }
    chunk_size_ = FLAGS_heap_chunk_size * 1024;
  }

  // Limit small block size to minimize waste in chunks.
  max_small_block_size_ = chunk_size_ / 16;
  min_small_block_size_ =
      Align(sizeof(SmallBlock) + kMinAllocSize, kAllocAlignment);

  AllocateChunk();  // we assume at least one chunk at all times
  allocating_chunk_ = 0;
  free_list_ = new SmallBlockFreeList(min_small_block_size_, chunk_size_);

  CHECK(Align(sizeof(LargeBlock), kAllocAlignment) == sizeof(LargeBlock));
  CHECK(min_small_block_size_ >= sizeof(FreeSmallBlock));
}


Memory::~Memory() {
  for (int i = chunk_.size(); --i >= 0; )
    delete chunk_[i];
  delete free_list_;
  for (LargeBlock *large = large_premark_blocks_, *next; large; large = next) {
    next = large->next;
    free(large);
  }
  for (LargeBlock *large = large_postmark_blocks_, *next; large; large = next) {
    next = large->next;
    free(large);
  }
#ifdef SZL_MEMORY_DEBUG
  VLOG(1) << "Destroying heap, max virtual process size = " <<
             ((max_process_size_ + (1<<19)) >> 20) << " MB";
#endif
}


void Memory::set_memory_limit(int64 memory_limit_MB) {
  // Our parameter is in megabytes, but we use unscaled bytes.
  int64 infinity = kint64max / 100;
  if (memory_limit_MB == -1) {
    memory_limit_ = infinity;
  } else if (memory_limit_MB == 0) {
    memory_limit_ = static_cast<int64>(PhysicalMem());
    if (memory_limit_ == 0) // RAM size could not be detected
      memory_limit_ = infinity;
  } else {
    memory_limit_ = memory_limit_MB << 20;
  }
  // Set initial threshold.
  gc_threshold_ = memory_limit_ * kInitialGCThresholdPercentage / 100;
}


void* Memory::AllocCompileTime(size_t size) {
  CHECK(proc_->status() == Proc::TERMINATED);
  return Alloc(size, false);
}


void Memory::ResetCounters() {
  total_available_ = 0;
  total_allocated_ = 0;
}


void* Memory::Alloc(size_t size, bool ref_counted) {
  // See if it qualifies as a small block.
  size_t alloc_size = Align(size +  sizeof(SmallBlock), kAllocAlignment);
  if (alloc_size <= max_small_block_size_) { 
    // Yes, take it from a chunk.  Take care that this case remains fast.
    void* p = chunk_[allocating_chunk_]->Alloc(alloc_size);
    if (p == NULL) {
      while (p == NULL && allocating_chunk_ != chunk_.size() - 1)
        p = chunk_[++allocating_chunk_]->Alloc(alloc_size);
      if (p == NULL) {
        // Cannot allocate from existing chunks.
        if (!using_free_list_) {
          // If allocating a chunk would put us over the GC threshold,
          // set up GC and populate the free list and try allocating from that.
          CheckGCThreshold(sizeof(Chunk) + chunk_size_);
          if (using_free_list_)
            p = free_list_->Alloc(&alloc_size);
        } else {
          // We're already using a free list, try to use that, including
          // repopulating it, even if we could have allocated a chunk now.
          p = free_list_->Alloc(&alloc_size);
          if (p == NULL) {
            // But don't do this too often - else we can thrash.
            if (small_alloc_since_last_free_ >
                gc_threshold_ * kMinFreePercentAfterGC / 100) {
              FreeUnusedLargeBlocks();
              FreeUnusedSmallBlocks(true, false);
              p = free_list_->Alloc(&alloc_size);
            }
          }
        }
        if (p == NULL) {
          // Not at GC limit or unable to use free list, allocate chunk.
          AllocateChunk();
          allocating_chunk_ = chunk_.size() - 1;
          p = chunk_[allocating_chunk_]->Alloc(alloc_size);
          assert(p != NULL);
        }
      }
    }
    small_alloc_since_last_free_ += alloc_size;
    SmallBlock* small = static_cast<SmallBlock*>(p);
    total_allocated_ += alloc_size;
    small->size_and_flags = alloc_size | kAllocatedFlag;
    if (ref_counted)
      small->size_and_flags |= kRefCountFlag;
    return small + 1;
  } else {
    // No, use malloc().  Assume the allocator aligns.
    // Note that sizeof(LargeBlock) must be an alignment multiple.
#ifdef SZL_MEMORY_DEBUG
    int64 old_vps = VirtualProcessSize();
#endif
    size_t alloc_size = Align(size +  sizeof(LargeBlock), kAllocAlignment);
    // The size must fit in ptrdiff_t (sign bit must be zero).
    assert(implicit_cast<ptrdiff_t>(alloc_size) >= 0);
    // Periodically free unused blocks so that memory does not get filled
    // with unused large blocks forcing unnecessary GC for small blocks.
    if (large_alloc_since_last_free_ >
        gc_threshold_ * kMinFreePercentAfterGC / 100) {
      FreeUnusedSmallBlocks(true, false);
      FreeUnusedLargeBlocks();
    }
    large_alloc_since_last_free_ += alloc_size;
    // Check GC threshold; free blocks and/or adjust threshold if needed.
    CheckGCThreshold(alloc_size);
    // Allocate with malloc.
    LargeBlock* large = static_cast<LargeBlock*>(malloc(alloc_size));
    CHECK(large != NULL) << ": allocating " <<
                                 alloc_size << " bytes: out of memory";
    assert(Align(reinterpret_cast<uintptr_t>(large), kAllocAlignment) ==
           reinterpret_cast<uintptr_t>(large));
#ifdef SZL_MEMORY_DEBUG
    int64 new_vps = VirtualProcessSize();
    if (new_vps > max_process_size_)
      max_process_size_ = new_vps;
    VLOG(1) << "Allocated " << alloc_size << " vps " <<
               (old_vps>>20) << " => " << (new_vps>>20);
#endif
    // link it into the appropriate list
    LargeBlock** link = post_mark_ ? &large_postmark_blocks_
                                   : &large_premark_blocks_;
    large->next = *link;
    *link = large;
    total_available_ += alloc_size;
    total_allocated_ += alloc_size;
    large->size_and_flags = alloc_size | kAllocatedFlag;
    if (ref_counted)
      large->size_and_flags |= kRefCountFlag;
    return large + 1;
  }
}


void Memory::Free(void* p) {
  assert(p != NULL);
  // The size_and_flags value must immediately precede "p" regardless of
  // whether this is a small block or a large block.  Just mark as free.
  SmallBlock* block = static_cast<SmallBlock*>(p) - 1;
  assert(block->allocated());
  block->clear_allocated();
  total_allocated_ -= block->size();

#ifdef OVERWRITE_ON_FREE
  int data_size;
  if (block->size() <= max_small_block_size_)
    data_size = block->size() - sizeof(SmallBlock);
  else
    data_size = block->size() - sizeof(LargeBlock);
  memset(p, 0xC0, data_size);
#endif
}


void Memory::FreeRefCounted(Val* v) {
  assert(!v->is_readonly());
#ifdef SZL_MEMORY_DEBUG
  if (post_mark_)
    freed_since_mark_++;
#endif
  Free(v);
}


void Memory::Mark() {
  assert(!post_mark_);
  if (mark_count_++ == 0) {
    // Only need to mark and GC on the first call.
    chunks_at_mark_ = chunk_.size();
    GarbageCollect(proc_->state_.fp_, proc_->state_.sp_, NULL);
    // Remember how much was allocated in each chunk.
    for (int i = 0; i < chunk_.size(); i++)
      chunk_[i]->SetMark();
  } else {
    // Verify that no memory was allocated between Release() and Mark()
    CHECK(allocating_chunk_ == 0);
    CHECK(chunk_[0]->top() == chunk_[0]->mark());
    CHECK(large_postmark_blocks_ == NULL);
  }

  post_mark_ = true;
#ifdef SZL_MEMORY_DEBUG
  allocated_since_mark_ = 0;
  freed_since_mark_ = 0;
#endif
}


// Release does not preserve the allocation counters; it's best
// to ResetCounters afterwards.
void Memory::Release() {
#ifdef SZL_MEMORY_DEBUG
  if (FLAGS_sawzall_mm_checks && proc_->status() == Proc::TERMINATED) {
    // Free unused blocks so we can verify ref counting OK on normal termination.
    CompactSmallBlocks(proc_->state_.fp_, proc_->state_.sp_);
    // Verify all chunks are back to their marks.
    if (proc_->status() == Proc::TERMINATED) {
      for (int i = 0; i < chunk_.size(); i++) {
        if (chunk_[i]->top() != chunk_[i]->mark()) {
          // Memory leak?
          fprintf(stderr, "Memory leak: not all memory freed in chunk %d.\n", i);
        }
      }
    }
    // Verify allocate and free counts are identical.
    if (freed_since_mark_ != allocated_since_mark_) {
      // Note a memory leak.
      F.fprint(2, "Reference counting error: %d objects were allocated but "
              "%d objects had reference counts equal to zero at termination.\n",
              allocated_since_mark_, freed_since_mark_);
    }
  }
#endif

  // Start looking for free memory in the first chunk again.
  allocating_chunk_ = 0;

  // Not using the free list.
  using_free_list_ = false;

  // Free the specified percentage of chunks.
  int chunks_to_free = (chunk_.size() - chunks_at_mark_)
                       * kPercentageChunksToFree / 100;
  while (chunks_to_free--) {
    CHECK(chunk_.size() >= 2);  // after delete must still have at least one
    Chunk* c = chunk_.back();
    total_available_ -= c->size();
    delete c;
    chunk_.pop_back();
  }

  // Release memory allocated since Mark() in each chunk.
  for (int i = 0; i < chunk_.size(); i++)
    chunk_[i]->ReleaseToMark();

  // Free the post-mark large blocks.
  for (LargeBlock *p = large_postmark_blocks_, *next; p != NULL; p = next) {
    total_available_ -= p->size();
    next = p->next;
    free(p);
  }
  large_postmark_blocks_ = NULL;

  post_mark_ = false;
}


void Memory::CheckGCThreshold(size_t size) {
  int64 vps = VirtualProcessSize();
  if (gc_threshold_ < vps * kMaxGCThresholdPercent / 100)
    gc_threshold_ = vps * kMaxGCThresholdPercent / 100;
#ifdef SZL_MEMORY_DEBUG
  if (vps > max_process_size_)
    max_process_size_ = vps;
#endif
  if (vps + size > gc_threshold_) {
    // We don't want to grow the process, and malloc might have enough
    // for us.  Ideally we would do a "fail-rather-than-grow-process" call
    // to malloc, but that isn't provided.  So guess based on the amount
    // of free space available in the malloc allocator.  There is no
    // guarantee that it is contiguous but the more there is of it, the
    // more likely an allocation will succeed without asking for more
    // process memory.
#ifdef HAVE_MALLINFO
    struct mallinfo info = mallinfo();
    // Ack, limited to 4GB.  Stored as "int", make sure we use full 32 bits.
    int64 freespace = static_cast<unsigned int>(info.fordblks);
    // As we get closer to running out of process memory, get more aggressive
    // about requiring that malloc space is available to minimize the chance
    // that we will run out of process memory.
    // (If we seem to have enough, do nothing and hope malloc succeeds.)
#else
    int freespace = 0;
#endif
    if (vps + size - (freespace*kPercentMallocCounted / 100) > gc_threshold_) {
      // Try to reclaim some space now.  This also populates the
      // free list.  If we were considering getting a chunk then we will try
      // the free list first and may avoid calling malloc.
      int64 freed = FreeUnusedSmallBlocks(true, true) + FreeUnusedLargeBlocks();
      // Increase the threshold if it is too small.
      if (freed < gc_threshold_ * kMinFreePercentAfterGC / 100) {
        gc_threshold_ += (gc_threshold_ * kMinFreePercentAfterGC / 100) - freed;
        VLOG(1) << "GC threshold increased to " << (gc_threshold_>>20) << "MB";
      }
      // If we haven't already set up to GC, set it up now.
      if (!using_free_list_) {
        VLOG(1) << "Exceeded GC threshold, scheduling GC; mark count = " <<
                   mark_count_;
        if (gctrigger_)
          gctrigger_->SetupStopForGC();
        using_free_list_ = true;
      }
    }
  }
}


void Memory::GarbageCollect(Frame* fp, Val** sp, Instr* pc) {
  // GC is not allowed in the compilation heap.
  assert((proc_->mode() & Proc::kPersistent) == 0);
  int64 vps = VirtualProcessSize();
  if (gc_threshold_ < vps * kMaxGCThresholdPercent / 100) {
    gc_threshold_ = vps * kMaxGCThresholdPercent / 100;
    VLOG(1) << "GC threshold increased to " << (gc_threshold_>>20) << "MB";
  }
  CompactSmallBlocks(fp, sp);
  using_free_list_ = false;
}


int64 Memory::FreeUnusedSmallBlocks(bool build_free_list,
                                    bool always_coalesce) {
  // Scan the chunks for ref-counted small blocks with zero ref counts.
  // and free them (propagating ref count changes).
  // Keep track of how much free space we find and how much we free,
  // but do not count any block twice.
  // (There is no cheap way to determine the overlap in the two values.)
  int64 prev_total_allocated = total_allocated_;
  int64 small_free = 0;
  for (int chunknum = 0; chunknum < chunk_.size(); chunknum++) {
    char* ptr = chunk_[chunknum]->mark();
    char* end = chunk_[chunknum]->top();
    while (ptr < end) {
      SmallBlock* small = reinterpret_cast<SmallBlock*>(ptr);
      size_t size = small->size();
      if (small->allocated()) {
        if (small->ref_counted()) {
          // allocated reference-counted block, check reference count
          Val* v = reinterpret_cast<Val*>(small + 1);
          if (v->ref() == 0) {
            // explicitly free block, adjust counts, free owned blocks
            v->form()->Delete(proc_, v);
          }
        }
      } else {
        assert(ptr + size <= end);
        small_free += size;
      }
      ptr += size;
    }
  }
  int64 small_just_freed = prev_total_allocated - total_allocated_;

  if (!always_coalesce) {
    // We want to avoid coalescing when there is little to be gained.
    // So check small_just_freed (the amount just freed) and small_free
    // (the amount already free plus some of that just freed) to see if
    // there is enough to gain.  If not, compute and check the real total.
    // (The choice of divisor below is arbitrary and may need to be tuned.)
    int64 min_to_free = gc_threshold_ * kMinFreePercentAfterGC / 400;
    if (small_free < min_to_free &&
        small_free + small_just_freed >= min_to_free) {
      // Not enough.  Compute free space again counting the blocks just freed.
      small_free = 0;
      for (int chunknum = 0; chunknum < chunk_.size(); chunknum++) {
        char* ptr = chunk_[chunknum]->mark();
        char* end = chunk_[chunknum]->top();
        while (ptr < end) {
          SmallBlock* small = reinterpret_cast<SmallBlock*>(ptr);
          size_t size = small->size();
          if (!small->allocated()) {
            assert(ptr + size <= end);
            small_free += size;
          }
          ptr += size;
        }
      }
    }
    // Now if there isn't enough free space then don't bother coalescing.
    // Since we did not add to the free list we return zero.
    // This also causes the GC threshold to be increased in CheckGCThreshold()
    // by more than it would have been increased with a nonzero return value.
    if (small_free < min_to_free )
      return 0;
  }

  // Coalesce adjacent free blocks.  We cannot do this in the above loop
  // because earlier blocks may be freed as a result of calling Delete() on
  // Val objects stored in later blocks.
  // We also build the free list.
  // This step is skipped when we are about to compact since neither
  // coalescing nor building the free list makes sense then.
  free_list_->Clear();
  int64 small_allocated = 0;
  small_free = 0;
  for (int chunknum = 0; chunknum < chunk_.size(); chunknum++) {
    // For all but last chunk, set up any remaining space in the chunk as a
    // free block so it gets added to the free list and can be coalesced.
    if (chunknum != chunk_.size() - 1) {
      size_t alloc_size = chunk_[chunknum]->free();
      if (alloc_size >= min_small_block_size_) {
        void *p = chunk_[chunknum]->Alloc(alloc_size);
        assert(p);
        SmallBlock* small = static_cast<SmallBlock*>(p);
        small->size_and_flags = alloc_size;
      }
    }
    char* ptr = chunk_[chunknum]->mark();
    char* end = chunk_[chunknum]->top();
    FreeSmallBlock* pending = NULL;
    while (ptr < end) {
      SmallBlock* small = reinterpret_cast<FreeSmallBlock*>(ptr);
      size_t size = small->size();
      if (small->allocated()) {
        small_allocated += small->size();
        if (pending) {
          small_free += pending->size();
          if (build_free_list)
            free_list_->AddFreeBlock(pending);
          pending = NULL;
        }
      } else {
        if (pending) {
          // Coalesce free blocks.
          assert(reinterpret_cast<char*>(pending) + pending->size() == ptr);
          pending->size_and_flags += size;  // both flags are false anyway
        } else {
          // New pending block.
          pending = static_cast<FreeSmallBlock*>(small);
        }
      }
      ptr += size;
    }
    if (pending) {
      small_free += pending->size();
      if (chunknum == chunk_.size() - 1) {
        // Since it's at the end we can give the memory back to the chunk
        // and it will be used for subsequent arena-based allocation.
        chunk_[chunknum]->set_top(reinterpret_cast<char*>(pending));
      } else {
        if (build_free_list)
          free_list_->AddFreeBlock(pending);
      }
    }
  }

  small_alloc_since_last_free_ = 0;
  VLOG(1) << "Freed small blocks: " <<
             (small_allocated >> 20) << "MB allocated, " <<
             (small_free >> 20) << "MB free";
  return small_free;
}


int64 Memory::FreeUnusedLargeBlocks() {
  // Scan the large blocks for refcounted objects with zero ref counts and
  // delete them (marking them as free, but not deallocating).
  int64 large_allocated = 0;
  int64 large_freed = 0;
  LargeBlock** link = post_mark_ ? &large_postmark_blocks_
                                 : &large_premark_blocks_;
  for (LargeBlock* large = *link; large; large = large->next) {
    if (large->allocated_and_refcounted()) {
      // allocated reference-counted block, check reference count
      Val* v = reinterpret_cast<Val*>(large + 1);
      if (v->ref() == 0)
        v->form()->Delete(proc_, v);
    }
  }
  // Now free the blocks no longer marked as in use.
  while (LargeBlock* large = *link) {
    if (!large->allocated()) {
      // free the block, update but do not advance link
      size_t size = large->size();
      total_available_ -= size;
      *link = large->next;
      free(large);
      large_freed += size;
    } else {
      // skip the block, advance link
      link = &large->next;
      large_allocated += large->size();
    }
  }
  large_alloc_since_last_free_ = 0;
  VLOG(1) << "Freed large blocks: " <<
             (large_allocated >> 20) << "MB allocated, " <<
             (large_freed >> 20) << "MB freed";
  return large_freed;
}


void Memory::CompactSmallBlocks(Frame* fp, Val** sp) {
  // Explicitly free the Val objects with zero ref counts; this will decrement
  // ref counts for objects they reference, possibly freeing them.
  // (See the comments for dec_ref() and dec_ref_and_check().)
  FreeUnusedLargeBlocks();  // may free some small blocks so do this first
  FreeUnusedSmallBlocks(false, true);

  // Scan the chunks counting the blocks that must be moved so that we
  // know how much temporary space to allocate for the sizes below.
  VLOG(1) << "Counting blocks to be moved.";
  for (int chunknum = 0; chunknum < chunk_.size(); chunknum++) {
    char* ptr = chunk_[chunknum]->mark();
    char* end = chunk_[chunknum]->top();
    int move_block_count = 0;
    bool any_free_blocks = false;
    while (ptr < end) {
      SmallBlock* small = reinterpret_cast<SmallBlock*>(ptr);
      size_t size = small->size();
      assert(ptr + size <= end);
      if (!small->allocated())
        any_free_blocks = true;  // all subsequent allocated blocks will move
      else if (any_free_blocks)
        move_block_count++;      // will move, allocate space to save its size
      ptr += size;
    }
    chunk_[chunknum]->set_move_block_count(move_block_count);
  }

  // Scan the chunks, saving the sizes and replacing them with the distance
  // each chunk will be moved backwards.
  VLOG(1) << "Computing distances that blocks will be moved.";
  for (int chunknum = 0; chunknum < chunk_.size(); chunknum++) {
    // Allocate array to save the block sizes that will be overwritten.
    size_t* block_sizes = new size_t[chunk_[chunknum]->move_block_count()];
    chunk_[chunknum]->set_block_sizes(block_sizes);
    // Update blocks.
    char* ptr = chunk_[chunknum]->mark();
    char* end = chunk_[chunknum]->top();
    char* next = ptr;  // next free byte in compacted chunk.
    bool any_free_blocks = false;
    while (ptr < end) {
      SmallBlock* small = reinterpret_cast<SmallBlock*>(ptr);
      size_t size = small->size();
      assert(ptr + size <= end);
      if (!small->allocated()) {
        any_free_blocks = true;  // all subsequent allocated blocks will move
      } else {
        if (any_free_blocks) {
          // Will move, save its size and replace it with the move distance.
          *block_sizes++ = size;
          ptrdiff_t delta = next - ptr;
          assert(delta < 0);
          small->set_size(delta);
        }
        next += size;
      }
      ptr += size;
    }
    assert(block_sizes == chunk_[chunknum]->block_sizes() +
                          chunk_[chunknum]->move_block_count());
  }

  // Scan, adjusting pointers in Val objects.
  // (Note that any internal adjustment in a Val object should make all
  // uses of embedded pointers to other Val objects first, then adjust
  // (temporarily invalidating) those pointers.)
  VLOG(1) << "Adjusting pointers in small blocks.";
  for (int chunknum = 0; chunknum < chunk_.size(); chunknum++) {
    const size_t* block_sizes = chunk_[chunknum]->block_sizes();
    char* ptr = chunk_[chunknum]->mark();
    char* end = chunk_[chunknum]->top();
    bool any_free_blocks = false;
    while (ptr < end) {
      SmallBlock* small = reinterpret_cast<SmallBlock*>(ptr);
      size_t size;
      if (!small->allocated()) {
        size = small->size();
        any_free_blocks = true;   // all subsequent allocated blocks will move
      } else {
        if (any_free_blocks)
          size = *block_sizes++;  // block is moving; get size from array
        else
          size = small->size();   // block is not moving, its size is correct
        if (small->ref_counted()) {
          Val* v = reinterpret_cast<Val*>(small + 1);
          v->form()->AdjustHeapPtrs(proc_, v);
        }
      }
      ptr += size;
    }
    assert(block_sizes == chunk_[chunknum]->block_sizes() +
                          chunk_[chunknum]->move_block_count());
  }

  // Adjust pointers in Val objects in large blocks.
  VLOG(1) << "Adjusting pointers in large objects";
  for (LargeBlock *large = large_premark_blocks_; large; large = large->next) {
    if (large->ref_counted()) {
      Val* v = reinterpret_cast<Val*>(large + 1);
      v->form()->AdjustHeapPtrs(proc_, v);
    }
  }
  for (LargeBlock *large = large_postmark_blocks_; large; large = large->next) {
    if (large->ref_counted()) {
      Val* v = reinterpret_cast<Val*>(large + 1);
      v->form()->AdjustHeapPtrs(proc_, v);
    }
  }

  // Adjust pointers on the stack frames.
  VLOG(1) << "Adjusting pointers on the stack.";
  if (fp != NULL) {
    // First do the expressions in the top frame.
    Val** ptr = sp;           // top-most expression
    Val** end = fp->stack();  // base of expressions
    assert(ptr <= end);
    while (ptr < end) {
      *ptr = AdjustVal(*ptr);
      ptr++;
    }
    // Then for each frame, adjust its variables and the previous frame's
    // expressions, which are contiguous.  This way we don't need to know
    // the number of variables.
    for (Frame* frame = fp; frame; frame = frame->dynamic_link()) {
      // top-most local
      Val** ptr = &frame->at(0);
      // base of next frame's expressions, or bottom of stack.
      Val** end = frame->dynamic_link() ? frame->dynamic_link()->stack()
                                        : proc_->initial_sp();
      assert(ptr <= end);
      while (ptr < end) {
        *ptr = AdjustVal(*ptr);
        ptr++;
      }
    }
  }

  // Adjust pointers in the "additional input".
  VLOG(1) << "Adjusting pointers in the additional inputs.";
  for (int i = 0; i < proc_->additional_input_->size(); ++i) {
    Proc::AdditionalInput* item = &proc_->additional_input_->at(i);
    item->key = AdjustPtr(item->key);
    item->value = AdjustPtr(item->value);
  }

  // Adjust pointers in the per-variable trap data.
  if (proc_->var_trapinfo_ != NULL) {
    for (int i = 0; i < proc_->var_trapinfo_count_; i++) {
      BytesVal** ptr = &proc_->var_trapinfo_[i].message;
      if (*ptr != NULL)
        *ptr = AdjustPtr(*ptr);
    }
  }

  // Scan small blocks doing compaction.
  // (Note that if we ever do multi-chunk compaction, we must be careful with
  // the delta value since it will not necessarily be negative.)
  VLOG(1) << "Compacting small blocks";
  for (int chunknum = 0; chunknum < chunk_.size(); chunknum++) {
    const size_t* block_sizes = chunk_[chunknum]->block_sizes();
    char* ptr = chunk_[chunknum]->mark();
    char* end = chunk_[chunknum]->top();
    char* next = ptr;  // next free byte in compacted chunk.
    bool any_free_blocks = false;
    while (ptr < end) {
      SmallBlock* small = reinterpret_cast<SmallBlock*>(ptr);
      size_t size;
      if (!small->allocated()) {
        size = small->size();
        any_free_blocks = true;  // all subsequent allocated blocks will move
      } else {
        if (any_free_blocks) {
          // Move the block and restore its size.
          size = *block_sizes++;
          ptrdiff_t delta = small->size();
          assert(delta == next - ptr);
          small->set_size(size);
          // TODO: One large move is faster than many small moves.
          //     If the "memmove" below ever shows up in cpu profiling,
          //     consider scanning adjacent allocated blocks, restoring their
          //     sizes as we go, and then moving all of them at once.
          memmove(next, small, size);
        } else {
          size = small->size();
        }
        next += size;
      }
      ptr += size;
    }
    chunk_[chunknum]->set_top(next);
    assert(block_sizes == chunk_[chunknum]->block_sizes() +
                          chunk_[chunknum]->move_block_count());
    delete[] chunk_[chunknum]->block_sizes();
  }

  allocating_chunk_ = 0;
  VLOG(1) << "Compaction complete.";
}


void* Memory::AdjustPtr(void* ptr) {
  // Note that pointers to pre-allocated memory must not be adjusted.
  SmallBlock* small = static_cast<SmallBlock*>(ptr) - 1;
  assert(small->allocated());

  // For small blocks that are moving the size field temporarily holds the
  // distance that the block will move; this value will be negative or zero.
  // For large blocks and for small blocks that are not moving this value will
  // be the size.  Note that this limits large block sizes to values that fit
  // in ptrdiff_t. (We could use a flag to distinguish small and large blocks,
  // at the cost of making the code dependent on 8-byte or greater size
  // alignment.)
  ptrdiff_t delta = small->size();
  if (delta < 0) {
#ifdef SZL_MEMORY_DEBUG
    if (FLAGS_sawzall_mm_checks)
      CHECK(IsInSmallBlocks(ptr));  // cannot use IsInHeap: size is wrong
#endif
    assert(-delta < chunk_size_);
    ptr = static_cast<char*>(ptr) + delta;
#ifdef SZL_MEMORY_DEBUG
    if (FLAGS_sawzall_mm_checks)
      CHECK(IsInSmallBlocks(ptr));  // cannot use IsInHeap: size is wrong
#endif
  }
  return ptr;
}


Val* Memory::AdjustVal(Val* v) {
  // Note that pre-allocated Val pointers must not be adjusted.
  if (v->is_ptr() && v != NULL && !v->is_readonly())
    return AdjustPtr(v);
  else
    return v;
}


void Memory::CheckPtr(void* ptr) {
  CHECK(IsInHeap(ptr));
}


void Memory::CheckVal(Val* v) {
  if (v != NULL && v->is_ptr()) {
    if (!v->is_readonly())
      CheckPtr(v);
    v->form()->CheckHeapPtrs(proc_, v);
  }
}


bool Memory::IsInHeap(void* ptr) {
  SmallBlock* block = static_cast<SmallBlock*>(ptr) - 1;
  if (block->size() <= max_small_block_size_)
    return IsInSmallBlocks(ptr);
  else
    return IsInLargeBlocks(ptr);
}


bool Memory::IsInSmallBlocks(void* ptr) {
  for (int chunknum = 0; chunknum < chunk_.size(); chunknum++) {
    if (reinterpret_cast<char*>(ptr) >= chunk_[chunknum]->data() &&
        reinterpret_cast<char*>(ptr) <  chunk_[chunknum]->top())
      return true;
  }
  return false;
}


bool Memory::IsInLargeBlocks(void* ptr) {
  LargeBlock* block = static_cast<LargeBlock*>(ptr) - 1;
  for (LargeBlock *large = large_premark_blocks_; large; large = large->next) {
    if (large == block)
      return true;
  }
  for (LargeBlock *large = large_postmark_blocks_; large; large = large->next) {
    if (large == block)
      return true;
  }
  return false;
}


void Memory::AllocateChunk() {
#ifdef SZL_MEMORY_DEBUG
  int64 old_vps = VirtualProcessSize();
#endif
  Chunk* c = new Chunk(chunk_size_, sizeof(SmallBlock));  // explicitly freed
  chunk_.push_back(c);
  total_available_ += c->size();
#ifdef SZL_MEMORY_DEBUG
  int64 new_vps = VirtualProcessSize();
  if (new_vps > max_process_size_)
    max_process_size_ = new_vps;
  VLOG(1) << "Allocated chunk; vps " << (old_vps>>20) << " => " << (new_vps>>20);
#endif
}

void Memory::Check() {
  // Scan the heap. For each allocated object that is a Val with a nonzero
  // reference count, verify that each object that it references has a
  // nonzero reference count and is either allocated on the heap or has a
  // "read only" reference count; and then check the referenced objects.

  // Scan the chunks.
  for (int chunknum = 0; chunknum < chunk_.size(); chunknum++) {
    char* ptr = chunk_[chunknum]->mark();
    char* end = chunk_[chunknum]->top();
    while (ptr < end) {
      SmallBlock* small = reinterpret_cast<SmallBlock*>(ptr);
      size_t size = small->size();
      assert(ptr + size <= end);
      if (small->allocated_and_refcounted()) {
        Val* v = reinterpret_cast<Val*>(small + 1);
        if (v->ref() != 0)
          v->form()->CheckHeapPtrs(proc_, v);
      }
      ptr += size;
    }
  }

  // Scan the large blocks.
  for (LargeBlock *large = large_premark_blocks_; large; large = large->next) {
    if (large->ref_counted()) {
      Val* v = reinterpret_cast<Val*>(large + 1);
      v->form()->CheckHeapPtrs(proc_, v);
    }
  }
  for (LargeBlock *large = large_postmark_blocks_; large; large = large->next) {
    if (large->ref_counted()) {
      Val* v = reinterpret_cast<Val*>(large + 1);
      v->form()->CheckHeapPtrs(proc_, v);
    }
  }
}


}  // namespace sawzall
