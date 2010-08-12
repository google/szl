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


// Structure for storing approx quantiles for each key in the table.
// Declared as: table quantile(N)[...] of value: <ordered sawtype>
// The parameter N (>=2) sets the relative error (eps_) that we are ready to
// tolerate. eps_ is set equal to 1/(N-1). This means that if
// there are "tot_elems_ " total emits to a particular key, then an
// element that we claim has rank X could have true rank in the range
// [X - eps_*tot_elems_, X + eps_*tot_elems_].

class SzlQuantile : public SzlTabWriter {
 private:
  explicit SzlQuantile(const SzlType& type)
      : SzlTabWriter(type, true, false)  { }
  virtual ~SzlQuantile()  { }
  class SzlQuantileEntry;

 public:
  static SzlTabWriter* Create(const SzlType& type, string* error) {
    return new SzlQuantile(type);
  }
  virtual SzlTabEntry* CreateEntry(const string& index) const {
    return new SzlQuantileEntry(element_ops(), param());
  }

 private:
  // The entry for each key inserted in a szl "table". If the
  // table is not indexed then there is only one entry
  // for the entire table.
  class SzlQuantileEntry : public SzlTabEntry {
   public:
    explicit SzlQuantileEntry(const SzlOps& element_ops, int param)
      : element_ops_(element_ops), num_quantiles_(max(param, 2)) {
      k_ = ComputeK();
      Clear();
    }
    virtual ~SzlQuantileEntry() { Clear(); }
    virtual int AddElem(const string& elem);
    virtual void Flush(string* output);
    virtual void FlushForDisplay(vector<string>* output);
    virtual SzlTabEntry::MergeStatus Merge(const string& val);

    void Clear() {
      for (int i = 0; i < buffer_.size(); i++)
        delete buffer_[i];
      buffer_.clear();
      tot_elems_ = 0;
    }

    virtual int Memory();
    virtual int64 TotElems() const  { return tot_elems_; }
    virtual int TupleCount()  { return buffer_.size(); }

    const SzlOps& element_ops() const  { return element_ops_; }

   private:
    const SzlOps& element_ops_;

    // We support quantiles over a sequence of upto MAX_TOT_ELEMS = 1 Trillion
    // elements. The value of k_, the buffer-size in the Munro-Paterson algorithm
    // grows roughly logarithmically as MAX_TOT_ELEMS. So we set
    // MAX_TOT_ELEMS to a "large-enough" value, and not to kint64max.
    static const int64 MAX_TOT_ELEMS = 1024LL * 1024LL * 1024LL * 1024LL;

    int64 ComputeK();
    int EnsureBuffer(const int level);
    int Collapse(vector<string> *const a, vector<string> *const b,
                 vector<string> *const output);
    int RecursiveCollapse(vector<string> *buf, const int level);
    bool EncodingToString(SzlDecoder *const dec, string *const output);

    const int num_quantiles_;  // #quantiles
    vector<vector<string>* > buffer_;
    int64 k_;  // max #elements in any buffer_[i]
    string min_;
    string max_;
  };
};
