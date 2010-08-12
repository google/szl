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

// Implements szl table structure for storing approximate quantiles
// in a table.  The implementation is based on the following paper:
//
// [MP80]  Munro & Paterson, "Selection and Sorting with Limited Storage",
//         Theoretical Computer Science, Vol 12, p 315-323, 1980.
//    More info available at
//    http://scholar.google.com/scholar?q=munro+paterson
//
// The above paper is not available online. You could read a detailed
// description of the same algorithm here:
//
// [MRL98] Manku, Rajagopalan & Lindsay, "Approximate Medians and other
//         Quantiles in One Pass and with Limited Memory", Proc. 1998 ACM
//         SIGMOD, Vol 27, No 2, p 426-435, June 1998.
//
// Also see  the following paper by Greenwald and Khanna, which contains
// another implementation that is thought to be slower:
// M. Greenwald and S. Khanna. Space-efficient online computation of
// quantile summerles. SIGMOD'01, pp. 58-66, Santa Barbara, CA, May
// 2001.
//
// Brief description of Munro-Paterson algorithm
// =============================================
// Imagine a binary tree of buffers. Every buffer has size "k". Now imagine
// populating the leaves of the tree (from left to right) with the input
// stream.  Munro-Paterson is very simple: As soon as both children of a
// buffer are full, we invoke a Collapse() operation.  What is a Collapse()?
// Basically, we take two buffers of size "k" each, sort them together and pick
// every other element in the sorted sequence. That's it!
//
// When the input stream runs dry, we would have populated some "b" buffers at
// various levels by following the Munro-Paterson algorithm.  How do we compute
// 100 quantiles from these "b" buffers? Assign a "weight" of 2^i to every
// element of a buffer at level i (leaves are at level 0).  Now sort all the
// elements (in various buffers) together. Then compute the "weighted 100
// splitters" of this sequence. Please see the code below or the paper above for
// furthe details.

#include <math.h>
#include <vector>
#include <algorithm>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/strutils.h"

#include "public/szlvalue.h"
#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szltabentry.h"

#include "emitters/szlquantile.h"


// For computing final quantiles for display.
void ComputeQuantiles(const vector<vector<string>* >& buffer,
  const string& min_string, const string& max_string,
  int num_quantiles, int64 tot_elems, vector<string>* quantiles);


// Simple struct that stores a single entry of quantile summary.
// It is a triple (string, int64, int64) that stores the value
// along with some bookkeeping vars (g_value, delta). See the
// paper above for details.

struct SummaryTuple {
  string value;  // Sample value from stream of input elements

  // Difference between the minimum rank of this element
  // and the previous one. See paper for details.
  int64 g_value;

  // Difference between the minimum and maximum rank of this
  // element.
  int64 delta;

  // Simple constructor. Simply copies the values that
  // are input into appropriate struct vars.
  SummaryTuple(const string& in_value,
               int64 in_g_value, int64 in_delta)
    : value(in_value), g_value(in_g_value), delta(in_delta)  { }
};


REGISTER_SZL_TAB_WRITER(quantile, SzlQuantile);


// We compute the "smallest possible k" satisfying two inequalities:
//    1)   (b - 2) * (2 ^ (b - 2)) + 0.5 <= epsilon * MAX_TOT_ELEMS
//    2)   k * (2 ^ (b - 1)) \geq MAX_TOT_ELEMS
//
// For an explanation of these inequalities, please read the Munro-Paterson or
// the Manku-Rajagopalan-Linday papers.
int64 SzlQuantile::SzlQuantileEntry::ComputeK() {
  const double epsilon = 1.0 / (num_quantiles_ - 1);
  int b = 2;
  while ((b - 2) * (0x1LL << (b - 2)) + 0.5 <= epsilon * MAX_TOT_ELEMS) {
    ++b;
  }
  const int64 k = MAX_TOT_ELEMS / (0x1LL << (b - 1));
  VLOG(2) << StringPrintf(
      "ComputeK(): returning k = %lld for num_quantiles_ = %d (epsilon = %f)",
      k, num_quantiles_, epsilon);
  return k;
}


// If buffer_[level] already exists, do nothing.
// Else create a new buffer_[level] that is empty.
int SzlQuantile::SzlQuantileEntry::EnsureBuffer(const int level) {
  int extra_memory = 0;
  if (buffer_.size() < level + 1) {
    size_t old_capacity = buffer_.capacity();
    buffer_.resize(level + 1, NULL);
    extra_memory +=
        (buffer_.capacity() - old_capacity) * sizeof(vector<string>*);
  }
  if (buffer_[level] == NULL) {
    VLOG(2) << StringPrintf("Creating buffer_[%d] ...", level);
    buffer_[level] = new vector<string>();
    extra_memory += sizeof(*(buffer_[level]));
  }
  return extra_memory;
}

// Estimate the amount of memory being used.
// This is an expensive call since it iterates over all members of
// all buffers in "buffer_".
int SzlQuantile::SzlQuantileEntry::Memory() {
  // capacity() returns the number of elements for which memory has
  // been allocated. capacity() is always greater than or equal to size().
  int memory = sizeof(SzlQuantileEntry) + min_.size() + max_.size()
      + sizeof(vector<string> *) * buffer_.capacity();

  for (vector<vector<string>* >::const_iterator iter = buffer_.begin();
      iter != buffer_.end(); ++iter) {
    if (*iter == NULL)
      continue;
    // Account for the memory taken by the current buffer.
    memory += sizeof(**iter) + (*iter)->capacity() * sizeof(string);
    for (vector<string>::const_iterator member = (*iter)->begin();
        member != (*iter)->end(); ++member) {
      memory += member->size();
    }
  }
  return memory;
}

// For Collapse(), both "a" and "b" should be vectors of length "k_".
// Conceptually, Collapse() combines "a" and "b" into a single vector, sorts
// this vector and then chooses every other member of this vector.
// The result is stored in "output".
//
// The return value is the change is memory requirements. What causes
// increase/decrease in memory?  (i) "output" is populated and (ii) just before
// returning, both "a" and "b" are cleared.
int SzlQuantile::SzlQuantileEntry::Collapse(vector<string> *const a,
                                            vector<string> *const b,
                                            vector<string> *const output) {
  CHECK_EQ(a->size(), k_);
  CHECK_EQ(b->size(), k_);
  CHECK_EQ(output->size(), 0);

  int memory_delta = 0;
  int index_a = 0;
  int index_b = 0;
  int count = 0;
  const string* smaller;

  while (index_a < k_ || index_b < k_) {
    if (index_a >= k_ || (index_b < k_ && a->at(index_a) >= b->at(index_b))) {
      smaller = &(b->at(index_b++));
    } else {
      smaller = &(a->at(index_a++));
    }

    if ((count++ % 2) == 0) {  // remember "smallest"
      output->push_back(*smaller);
    } else {  // forget "smallest"
      memory_delta -= smaller->size();
    }
  }

  // Account for the memory taken by output and a & b.
  memory_delta += (output->capacity() - a->capacity() - b->capacity())
      * sizeof(string);

  // Make sure we completely deallocate the memory taken by a & b.
  {
    vector<string> tmp;
    a->swap(tmp);
  }
  {
    vector<string> tmp;
    b->swap(tmp);
  }

  return memory_delta;
}

// Algorithm for RecursiveCollapse():
//
// 1. Let "merged" denote the output of Collapse("buf", "buffer_[level]")
//
// 2. If "buffer_[level + 1]" is full (i.e., already exists)
//      Collapse("merged", "buffer_[level + 1]")
//    else
//      "buffer_[level + 1]"  <-- "merged"
//
// The return value is the difference in memory usage.
int SzlQuantile::SzlQuantileEntry::RecursiveCollapse(vector<string> *buf,
                                                     const int level) {
  VLOG(2) << StringPrintf("RecursiveCollapse() invoked with level = %d", level);

  CHECK_EQ(buf->size(), k_);
  CHECK_GE(level, 1);
  CHECK_GE(buffer_.size(), level + 1);
  CHECK(buffer_[level] != NULL);
  CHECK_EQ(buffer_[level]->size(), k_);

  int memory_delta = EnsureBuffer(level + 1);

  vector<string> *merged;
  if (buffer_[level + 1]->size() == 0) {  // buffer_[level + 1] is empty
    merged = buffer_[level + 1];
  } else {                                // buffer_[level + 1] is full
    merged = new vector<string>;
    // merged is going to be filled with k_ elements we might as well
    // reserve the space now.
    merged->reserve(k_);
  }
  // We account for the memory taken by merged even if it's a
  // temporary vector, since in this case it will be passed to
  // RecursiveCollapse, which will substract the memory it takes.
  memory_delta += Collapse(buffer_[level], buf, merged);
  if (buffer_[level + 1] == merged) {
    return memory_delta;
  }
  memory_delta += RecursiveCollapse(merged, level + 1);
  delete merged;
  return memory_delta;
}

// Goal: Add a new element ("elem" is a SzlEncoded value).
// Return value: "diff in memory usage".
//
// Algorithm:
//   if (buffer_[0] is not full (i.e., has less than k_ elements)) {
//       insert "elem" into buffer_[0]
//   } else if buffer_[1] is not full (i.e., has less than k_ elements) {
//       insert "elem" into "buffer_[1]"
//   } else {
//       Sort buffer_[0] and buffer_[1]
//       RecursiveCollapse(buffer_[0], buffer_[1])
//       Insert into buffer_[0]
//   }
int SzlQuantile::SzlQuantileEntry::AddElem(const string& elem) {
  int memory_delta = 0;

  // Update min_ and max_.
  if ((tot_elems_ == 0) || (elem < min_)) {
    memory_delta += elem.size() - min_.size();
    min_ = elem;
    VLOG(3) << "AddElem(" << elem << "): min_ updated to " << min_;
  }
  if ((tot_elems_ == 0) || (max_ < elem)) {
    memory_delta += elem.size() - max_.size();
    max_ = elem;
    VLOG(3) << "AddElem(" << elem << "): max_ updated to " << max_;
  }

  // First, test if both buffer_[0] and buffer_[1] are full.
  // This is equivalent to testing
  //    (tot_elems_ > 0)    &&   (tot_elems_ % (2 * k_) == 0).
  // If so, sort buffer_[0] and buffer_[1] and invoke RecursiveCollapse().
  // When RecursiveCollapse() returns, both buffer_[0] and buffer_[1] would be
  // "empty" (i.e., with zero elements in them).
  if ((tot_elems_ > 0) && (tot_elems_ % (2 * k_) == 0)) {
    CHECK(buffer_[0] != NULL);
    CHECK(buffer_[1] != NULL);
    CHECK_EQ(buffer_[0]->size(), k_);
    CHECK_EQ(buffer_[1]->size(), k_);
    VLOG(2) << "AddElem(" << elem << "): Sorting buffer_[0] ...";
    sort(buffer_[0]->begin(), buffer_[0]->end());
    VLOG(2) << "AddElem(" << elem << "): Sorting buffer_[1] ...";
    sort(buffer_[1]->begin(), buffer_[1]->end());
    const int level = 1;
    // RecursiveCollapse will start with Collapse(buffer_[0], buffer_[level]).
    memory_delta += RecursiveCollapse(buffer_[0], level);
  }

  // At this point, we are sure that either buffer_[0] or buffer_[1] can
  // accommodate "elem".
  memory_delta += EnsureBuffer(0);
  memory_delta += EnsureBuffer(1);
  CHECK((buffer_[0]->size(), k_) || (buffer_[1]->size(), k_));
  int index = (buffer_[0]->size() < k_) ? 0 : 1;
  VLOG(3) << "AddElem(" << elem << "): Inserting into buffer_[" << index << "]";
  int old_capacity = buffer_[index]->capacity();
  buffer_[index]->push_back(elem);
  memory_delta += elem.size()
      + (buffer_[index]->capacity() - old_capacity) * sizeof(string);
  ++tot_elems_;
  VLOG(3) << StringPrintf("AddElem(%s): returning with tot_elems_ = %lld",
                          elem.c_str(), tot_elems_);
  return memory_delta;
}


// Flush the state to "output".
void SzlQuantile::SzlQuantileEntry::Flush(string* output) {
  SzlEncoder enc;

  // We emit "dummy_epsilon == 0.0" for historyical reasons.
  double dummy_epsilon = 0.0;
  enc.PutInt(tot_elems_);
  enc.PutInt(num_quantiles_);
  enc.PutFloat(dummy_epsilon);
  enc.PutInt(k_);
  enc.PutInt(buffer_.size());

  if (tot_elems_ > 0) {
    // Encode "min_" and "max_".
    enc.AppendEncoding(min_.data(), min_.size());
    enc.AppendEncoding(max_.data(), max_.size());

    // Encode each member of "buffer_[]".
    for (vector<vector<string>* >::const_iterator iter = buffer_.begin();
         iter != buffer_.end(); ++iter) {
      if (*iter == NULL) {
        enc.PutInt(0);
      } else {
        enc.PutInt((*iter)->size());
        for (vector<string>::const_iterator member = (*iter)->begin();
             member != (*iter)->end(); ++member) {
          enc.AppendEncoding(member->data(), member->size());
        }
      }
    }
  }
  enc.Swap(output);
  Clear();
  VLOG(2) << StringPrintf("Flush() succeeded. tot_elems_ = %lld", tot_elems_);
}


void SzlQuantile::SzlQuantileEntry::FlushForDisplay(vector<string>* output) {
  output->clear();
  if (tot_elems_ == 0) {
    output->push_back("");
    return;
  }

  // We display the quantiles, not the raw output.
  ComputeQuantiles(buffer_, min_, max_, num_quantiles_, tot_elems_, output);
}


// Simple helper function to extract a value from "dec" by using "ops()".
// Returns true on success, false on failure.
bool SzlQuantile::SzlQuantileEntry::EncodingToString(SzlDecoder *const dec,
                                        string *const output) {
  // Record the starting position of the next value.
  unsigned const char* p1 = dec->position();
  // Skip past it
  if (!element_ops().Skip(dec)) {
    return false;
  }
  // Now we know the end of the encoded value.
  // Record the new position
  unsigned const char* p2 = dec->position();
  output->assign(reinterpret_cast<const char*>(p1), p2 - p1);
  return true;
}

// Goal: Merge "val" with the existing state stored in SzlQuantileEntry.
//
// Recap:
//   Please read the short description of Munro-Paterson algorithm at the very
//   top of this file. Then, recall that SzlQuantileEntry::Flush() simply dumps
//   all non-empty buffers (in the binary tree of buffers).
//
// Algorithm:
//   We have to merge two "trees of buffers".
SzlTabEntry::MergeStatus SzlQuantile::SzlQuantileEntry::Merge(
                                      const string& val) {
  SzlDecoder dec(val.data(), val.size());
  int64 tot_elems, num_quantiles, k, num_buffers;

  double dummy_epsilon = 0.0;
  if (!val.empty()) {
    // Format errors and sanity checks
    if ((!dec.GetInt(&tot_elems)) || (tot_elems < 0)
        || (!dec.GetInt(&num_quantiles)) || (num_quantiles != num_quantiles_)
        || (!dec.GetFloat(&dummy_epsilon))) {
      LOG(ERROR) << "Failed to parse header in Merge()";
      return MergeError;
    }
  }

  if (val.empty() || dummy_epsilon > 0.0) {
    return MergeError;
  }
  if ((!dec.GetInt(&k)) || (k != k_) || (!dec.GetInt(&num_buffers))) {
    LOG(ERROR) << "Failed to parse header in Merge()";
    return MergeError;
  }
  if (tot_elems == 0) {
    VLOG(2) << "Merge() encountered tot_elems = 0";
    return MergeOk;
  }
  VLOG(2) << StringPrintf("Merge(): tot_elems=%lld num_buffers=%lld",
                          tot_elems, num_buffers);

  // Update min_ and max_
  string min_string, max_string;
  if (!EncodingToString(&dec, &min_string)
      || !EncodingToString(&dec, &max_string)) {
    return MergeError;
  }
  if ((tot_elems_ == 0) || (min_string < min_)) {
    min_ = min_string;
    VLOG(2) << StringPrintf("Merge(): min_ updated to %s", min_.c_str());
  }
  if ((tot_elems_ == 0) || (max_ < max_string)) {
    max_ = max_string;
    VLOG(2) << StringPrintf("Merge(): max_ updated to %s", max_.c_str());
  }

  // Now comes the complex part of this method.
  //
  // We start de-serializing the buffers in "dec".
  // The first two buffers correspond to the leaf nodes in the "binary tree of
  // buffers".  The contents of these buffers are simply
  for (int level = 0; level < num_buffers; ++level) {
    int64 count;
    if (!dec.GetInt(&count) || (count < 0)) {
      return MergeError;
    }
    // Buffers at level >= 2 are either empty (of size 0) or full (of size k_)
    CHECK(level < 2 || count == 0 || count == k_);
    if (count == 0) {  // this is an "empty" buffer (with no elements).
      continue;        // so we do nothing.
    }

    VLOG(2) << StringPrintf("Merge(): About to de-serialize buffer at level %d"
                            " with %lld elements.", level, count);
    EnsureBuffer(level);

    // If buffer_[level] is "empty" (with size 0)
    //    newbuffer <-- buffer_[level]
    // else
    //    newbuffer <-- "a newly allocated buffer"
    vector<string> *newbuffer;
    if (buffer_[level]->size() == 0) {
      newbuffer = buffer_[level];
    } else {
      newbuffer = new vector<string>;
      newbuffer->reserve(k_);
    }

    // De-serialize the buffer at this level into "newbuffer".
    while (count-- > 0) {
      newbuffer->push_back("");
      if (!EncodingToString(&dec, & (newbuffer->back()))) {
        if (newbuffer != buffer_[level]) {
          delete newbuffer;
        }
        return MergeError;
      }
    }

    // The first two buffers in "buffer_" (corresponding to level == 0,1) are
    // actually the leaf buffers in the "tree of buffers" in the Munro-Paterson
    // algorithm.  So only if (level >= 2) does the buffer correspond to an
    // internal node in the tree.
    //
    // If (level >= 2) {
    //    If (newbuffer != buffer_[level]) {
    //       RecursiveCollapse(newbuffer, level)
    //     }
    // } else {
    //    If (newbuffer != buffer_[level]) {
    //       AddElem() for every member of "newbuffer"
    //    }
    // }
    if (level >= 2) {
      tot_elems_ += k_ * (0x1LL << (level - 1));
      if (newbuffer != buffer_[level]) {
        RecursiveCollapse(newbuffer, level);
        delete newbuffer;
        newbuffer = NULL;
      }
    } else if (newbuffer != buffer_[level]) {
      for (vector<string>::const_iterator iter = newbuffer->begin();
          iter != newbuffer->end(); ++iter) {
        AddElem(*iter);
      }
      delete newbuffer;
      newbuffer = NULL;
    } else {
      tot_elems_ += newbuffer->size();
    }
  }

  VLOG(2) << StringPrintf("Merge() succeeded. tot_elems_ = %lld", tot_elems_);
  return MergeOk;
}
