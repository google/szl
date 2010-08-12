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

// The implementation of class Memory cannot use the Sawzall
// List class because List in turn is using Memory for its allocation.

// TODO: Get rid of include of <vector>; performance issue?
#include <vector>

#ifndef NDEBUG
// Normally additional memory manager debugging code is enabled only in
// debug compiles, but it can be enabled in opt compiles if needed.
// Some of the more expensive checks also require a command line option;
// see memory.cc.
#define SZL_MEMORY_DEBUG
#endif


namespace sawzall {

// The primary memory model is a chunked heap with mark/release functionality
// for small blocks, and malloc with a linked list to track allocation for
// large blocks.
// After every single Sawzall run (i.e., after each log record processed), all
// memory allocated in each chunk or using malloc during the run is freed.
//
// We assume that we are working with limited memory and that the memory limit
// has been specified.  We also assume that for the vast majority of runs
// we will not run out of memory even though memory is not reused.  The memory
// manager is designed to give maximum performance for that case while still
// enabling execution to continue by reusing memory when necessary.
//
// If memory use approaches the limit, two approaches are used to reclaim
// unused blocks.
//
// First, unreferenced large blocks are freed and unreferenced small blocks
// are put in a free list.  Allocation continues from the free list until
// the end of the current instruction (or for native code, until the end of
// the current record).  Unreferenced blocks are found by scanning the chunks
// and the large block list and checking for reference-counted blocks with
// zero reference counts.  Form::Delete() and Val::dec_ref_and_check() are
// used to free contained objects.
//
// In interpreted mode, when we reach the next instruction the free list is
// discarded, the chunks are compacted and we go back to arena allocation.
// We refer to compaction as "garbage collection" in the interpreter loop
// rather than specify the exact type or phase of memory reclamation.
//
// We defer dealing with zero-reference-count objects until free memory is low
// because deallocation requires decrementing reference counts for objects
// referenced by the deallocated object; this can increase execution time even
// when we never need to reclaim memory (observed 5% increase in testing).

// Useful macros for arena-based allocation:
//
// At compile time only, using the compilation heap, for objects that do not
// hold execution values:
//
// NEW
//   Allocates a new instance of the specified class and calls the default
//   constructor.
// NEWP
//   Like NEW but passes the "proc" argument to the constructor.
//
// At compile time or run time, using either heap, for execution values or
// for temporary data used for execution:
//
// ALLOC
//   Allocates a non-reference-counted object by getting n bytes of memory and
//   casting the result to be a pointer to the specified type.  The constructor
//   is not called.  When a runtime object allocated by ALLOC is no longer
//   used the client must use FREE to deallocate it.  Objects that are not
//   themselves reference-counted but are owned by reference-counted objects
//   are managed by the appropriate Form methods; other are managed on an
//   ad hoc basis.
// ALLOC_COUNTED
//   Like ALLOC but for reference-counted objects; used only by the Form::NewXXX
//   methods.  Runtime objects allocated by ALLOC_COUNTED are freed by the
//   appropriate Form::Delete method invoked when the memory manager is
//   reclaiming memory.
// FREE
//   Frees an object allocated by ALLOC.  The destructor is not called.
//   Used by the Form methods and ad hoc users of ALLOC.
// FREE_COUNTED
//   Frees an object allocated by ALLOC_COUNTED.  Used only by the Form::Delete
//   methods.

#define ALLOC(proc, type, n) \
  (static_cast<type*>((proc)->heap()->AllocNonRefCounted(n)))

#define ALLOC_COUNTED(proc, type, n) \
  (static_cast<type*>((proc)->heap()->AllocRefCounted(n)))

#define FREE(proc, p) \
  ((proc)->heap()->FreeNonRefCounted(p))

#define FREE_COUNTED(proc, p) \
  ((proc)->heap()->FreeRefCounted(p))

#define NEW(proc, type) \
  (new ((proc)->heap()->AllocCompileTime(sizeof(type))) (type))

#define NEWP(proc, type) \
  (new ((proc)->heap()->AllocCompileTime(sizeof(type))) (type)(proc))

// Use NEW_ARRAY instead of NEW for arrays to work around a gcc bug
// causing it to generate a bogus variable length array warning.
// WARNING: The values in the array will not be initialized.
#define NEW_ARRAY(proc, type, n) \
  (static_cast<type*> ((proc)->heap()->AllocCompileTime(sizeof(type) * (n))))


class Proc;
class GCTrigger;
class Frame;
class Val;


class Memory {
 public:
  Memory(Proc* proc);
  virtual ~Memory();
  
private:
  void* Alloc(size_t size, bool ref_counted);
  void Free(void* p);
public:

  void* AllocCompileTime(size_t size);
  void* AllocNonRefCounted(size_t size) { return Alloc(size, false); }
  void* AllocRefCounted(size_t size) {
#ifdef SZL_MEMORY_DEBUG
    if (post_mark_)
      allocated_since_mark_++;
#endif
    return Alloc(size, true);
  }
  void FreeNonRefCounted(void* p) { Free(p); }
  void FreeRefCounted(Val* v);
  // Check the heap for correctness; called by __heapcheck().  Very expensive.
  void Check();

  // Adjust a pointer for heap compaction.
  void* AdjustPtr(void* ptr);
  template<class T> T* AdjustPtr(T* ptr) {
    return reinterpret_cast<T*>(AdjustPtr(implicit_cast<void*>(ptr)));
  }
  // Adjust a value (including smi) for heap compaction.
  Val* AdjustVal(Val* v);
  // Check pointers and values that might be pointers.
  void CheckPtr(void* ptr);
  void CheckVal(Val* v);
  // Mark all currently allocated blocks as persistent.
  void Mark();
  // Release blocks allocated since Mark().
  void Release();
  // Reset the allocation counters.
  void ResetCounters();
  // Finish reclaiming memory; all Val pointers must be findable.
  void GarbageCollect(Frame* fp, Val** sp, Instr* pc);
  // Note the object used to trigger GC from the interpreter loop.
  void RegisterGCTrigger(GCTrigger* gctrigger) { gctrigger_ = gctrigger; }
  // Accessors / setters.
  size_t total_available() const { return total_available_; }
  size_t total_allocated() const { return total_allocated_; }
  void set_memory_limit(int64 memory_limit_MB);

  static const size_t kAllocAlignment = sizeof(int64);

 private:
  class Chunk;
  class SmallBlock;
  class FreeSmallBlock;
  class LargeBlock;
  class SmallBlockFreeList;

  // Allocate a new chunk.
  void AllocateChunk();
  // Check whether we need to reclaim memory now.
  void CheckGCThreshold(size_t size);
  // Compact allocated small blocks in chunks, adjusting Val pointers as needed.
  void CompactSmallBlocks(Frame* fp, Val** sp);
  // Free small blocks with zero reference counts.  When preparing to compact
  // or in danger of running out of memory, always_coalesce should be true.
  int64 FreeUnusedSmallBlocks(bool build_free_list, bool always_coalesce);
  // Free large blocks with zero reference counts.
  int64 FreeUnusedLargeBlocks();
  // Check whether a pointer falls within the heap (for debugging).
  bool IsInHeap(void* ptr);
  bool IsInSmallBlocks(void* ptr);
  bool IsInLargeBlocks(void* ptr);

  Proc* proc_;                   // owner of this heap

  // Arena allocation.
  vector<Chunk*> chunk_;         // allocated chunks
  int allocating_chunk_;         // which chunk we are allocating from
  bool post_mark_;               // whether Mark() called but not Release()
  int mark_count_;               // number of times Mark() called
  int chunks_at_mark_;           // number of chunks when Mark() called

  // Non-arena allocation.
  LargeBlock* large_premark_blocks_;   // large blocks allocated before mark
  LargeBlock* large_postmark_blocks_;  // large blocks allocated after mark
  SmallBlockFreeList* free_list_;      // free small blocks in chunks
  bool using_free_list_;               // using free list; compaction pending
  int64 small_alloc_since_last_free_;  // to periodically free small blocks
  int64 large_alloc_since_last_free_;  // to periodically free large blocks

  // Memory limit monitoring.
  int64 memory_limit_;           // memory limit
  int64 gc_threshold_;           // memory limit to trigger GC
  int64 max_process_size_;       // max process size
  GCTrigger* gctrigger_;         // coordinate GC with Engine

  // Block size thresholds.
  size_t chunk_size_;            // size of a chunk
  size_t max_small_block_size_;  // threshold between chunk and malloc
  size_t min_small_block_size_;  // minimum size of a small block

  // Statistics.
  size_t total_available_;       // bytes ready for allocation (chunks + malloc)
  size_t total_allocated_;       // bytes returned from Alloc()
#ifdef SZL_MEMORY_DEBUG
  int allocated_since_mark_;     // number of blocks allocated since Mark()
  int freed_since_mark_;         // number of blocks freed since Mark()
#endif

  // Allocation and garbage collection parameters.
  // For now these are fixed.  They could be command line flags.

  // Minimum value for --heap_chunk_size.
  static const int kMinHeapChunkSize = 32;
  // Maximum value for --heap_chunk_size.
  static const int kMaxHeapChunkSize = 128 * 1024;
  // Minimum size a client will ever request.
  static const size_t kMinAllocSize = 12;
  // The initial value for the GC threshold, as percentage of the memory limit.
  static const int kInitialGCThresholdPercentage = 50;
  // The maximum value for the GC threshold, as percentage of the memory limit.
  static const int kMaxGCThresholdPercent = 95;
  // If less than this much memory (as a percent of the threshold) will be
  // recovered by GC, also increase the threshold (if not at limit).
  static const int kMinFreePercentAfterGC = 20;
  // Percentage of free malloc space we count (assume fragmentation).
  static const int kPercentMallocCounted = 50;
  // Percentage of chunks to free at Release.  If we free too few chunks
  // then we may not have enough memory for large blocks next time.
  static const int kPercentageChunksToFree = 30;
};


}  // namespace sawzall
