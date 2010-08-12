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

#include <math.h>
#include <vector>
#include <string>
#include <algorithm>

#include "public/porting.h"
#include "public/logging.h"

#include "utilities/strutils.h"


// Please read the short description of the Munro-Paterson algorithm at the
// beginning of sawquantile.cc
//
// Basically, our goal is to compute quantiles from a bunch of buffers.
// We assign a "weight" of 2^i to every element of a buffer at
// level i in the binary tree (leaves are at level 0).  Now sort all the
// elements (in various buffers) together. Then compute the "weighted 100
// splitters" of this sequence.
void ComputeQuantiles(const vector<vector<string>* >& buffer,
                      const string& min_string, const string& max_string,
                      int num_quantiles, int64 tot_elems,
                      vector<string>* quantiles) {
  CHECK(max_string >= min_string);
  CHECK_GE(buffer.size(), 1);

  quantiles->clear();

  VLOG(2) << "ComputeQuantiles(): min=" << min_string;
  quantiles->push_back(min_string);

  // buffer[0] and buffer[1] may be unsorted; all others are already sorted.
  if (buffer[0] == NULL) {
    VLOG(2) << "ComputeQuantiles(): Not sorting buffer[0] (it is NULL).";
  } else {
    VLOG(2) << "ComputeQuantiles(): Sorting buffer[0] ...";
    sort(buffer[0]->begin(), buffer[0]->end());
  }
  if ((buffer.size() < 2) || (buffer[1] == NULL)) {
    VLOG(2) << "ComputeQuantiles(): Not sorting buffer[1] (it doesn't exist).";
  } else {
    VLOG(2) << "ComputeQuantiles(): Sorting buffer[1] ...";
    sort(buffer[1]->begin(), buffer[1]->end());
  }

  // Simple sanity check: the weighted sum of all buffers should equal
  // "tot_elems".  The weight of buffer[i] is 2^(i-1) for i >= 2. Otherwise,
  // the weight is 1.
  int64 t = 0;
  for (int j = 0; j < buffer.size(); ++j) {
    const int64 weight = (j <= 1) ? 1LL : (0x1LL << (j - 1));
    t += (buffer[j] == NULL) ? 0 : (buffer[j]->size() * weight);
  }
  CHECK_EQ(t, tot_elems);

  vector<int> index(buffer.size(), 0);

  // Our goal is to identify the weighted "num_quantiles - 2" splitters in the
  // sorted sequence of all buffers taken together.
  // "S" will store the cumulative weighted sum so far.
  int64 S = 0;
  for (int i = 1; i <= num_quantiles - 2; ++i) {
    // Target "S" for the next splitter (next quantile).
    const int64 target_S
      = static_cast<int64>(ceil(i * (tot_elems / (num_quantiles - 1.0))));
    CHECK_LE(target_S, tot_elems);

    while (true) {
      // Identify the smallest element among buffer_[0][index[0]],
      // buffer_[1][index[1]],  buffer_[2][index[2]], ...
      string smallest = max_string;
      int min_buffer_id = -1;
      for (int j = 0; j < buffer.size(); ++j) {
        if ((buffer[j] != NULL) && (index[j] < buffer[j]->size())) {
          if (!(smallest < buffer[j]->at(index[j]))) {
            smallest = buffer[j]->at(index[j]);
            min_buffer_id = j;
          }
        }
      }
      CHECK_GE(min_buffer_id, 0);

      // Now increment "S" by the weight associated with "min_buffer_id".
      //
      // Note: The "weight" of elements in buffer[0] and buffer[1] is 1 (these
      //       are leaf nodes in the Munro-Paterson "tree of buffers".
      //       The weight of elements in buffer[i] is 2^(i-1) for i >= 2.
      int64 S_incr = (min_buffer_id <= 1) ? 1 : (0x1LL << (min_buffer_id - 1));

      // If we have met/exceeded "target_S", we have found the next quantile.
      // Then break the loop. Otherwise, just update index[min_buffer_id] and S
      // appropriately.
      if (S + S_incr >= target_S) {
        CHECK(buffer[min_buffer_id]->at(index[min_buffer_id]) == smallest);
        quantiles->push_back(smallest);
        break;
      } else {
        ++index[min_buffer_id];
        S += S_incr;
      }
    }
  }

  VLOG(2) << StringPrintf("ComputeQuantiles(): max=%s", max_string.c_str());
  quantiles->push_back(max_string);
}
