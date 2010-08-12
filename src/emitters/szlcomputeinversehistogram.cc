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
#include <algorithm>

#include "openssl/md5.h"

#include "public/porting.h"
#include "public/logging.h"
#include "public/hashutils.h"

#include "public/szlencoder.h"
#include "public/szldecoder.h"
#include "public/szlresults.h"
#include "public/szlvalue.h"


static double EstimateUniqueCount(const string &elem, int64 nElems,
                                  int64 maxElems, int64 totElems) {
  if (nElems < maxElems)
    return nElems;

  uint8 digest[MD5_DIGEST_LENGTH];
  MD5Digest(elem.data(), elem.size(), &digest);

  double a, b, c;
  a = 0;
  b = 1;
  for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
    a = 256*a + digest[i];
    b *= 256;
  }
  c =  b/a * maxElems;
  if (c > totElems) c = totElems;
  return c;
}


struct MySzlValueLess {
  MySzlValueLess(const SzlValue* const* wlist, const SzlOps& ops)
      : wlist_(wlist), ops_(ops)  { }
  bool operator()(int i, int j) const {
    return ops_.Less(*wlist_[i], *wlist_[j]);
  }
  const SzlValue* const* wlist_;
  const SzlOps& ops_;
};


void ComputeInverseHistogram(const SzlOps& weight_ops, const string& last_elem,
                             const SzlValue** wlist,
                             int64 nElems, int64 maxElems, int64 totElems,
                             vector<string>* output) {

  int *perm = NULL;        // sorted permutation of weights
  double nUnique = 0;

  if (nElems > 0) {
    // estimate UNIQUE_COUNT. We only need the hash of the last element
    nUnique = EstimateUniqueCount(last_elem, nElems, maxElems, totElems);

    // sort the weights
    perm = new int[nElems];
    for (int i = 0; i < nElems; ++i)
      perm[i] = i;
    sort(perm, perm + nElems, MySzlValueLess(wlist, weight_ops));
  }

  SzlEncoder enc;

  // output UNIQUE_COUNT
  SzlValue w;
  weight_ops.AssignZero(&w);
  enc.Reset();
  weight_ops.Encode(w, &enc);
  enc.PutFloat(nUnique);
  output->push_back(enc.data());
  weight_ops.Clear(&w);

  // Output the inverse distribution
  for (int i = 0; i < nElems;) {
    int j = i + 1;
    for ( ; j < nElems; j++)
      if (!weight_ops.Eq(*wlist[perm[i]], *wlist[perm[j]]))
        break;
    enc.Reset();
    weight_ops.Encode(*wlist[perm[i]], &enc);
    enc.PutFloat((j-i+0.0) / nElems);
    output->push_back(enc.data());
    i = j;
  }
  delete[] perm;
}
