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

// This is one of a small number of top-level header files for the Sawzall
// component.  See sawzall.h for a complete list.  No other lower-level
// header files should be included by clients of the Sawzall implementation.

#ifndef _PUBLIC_SZLRESULTS_H__
#define _PUBLIC_SZLRESULTS_H__

#include <string>
#include <vector>

#include "public/szltype.h"

// Abstract interface for reading table entries.
//
// Each entry stored in under one key in a table
// may produce multiple results in the output.
// For example, all of the samples for a sample table are
// combined into one output value.
// The appropriate instance of this class can be used to read
// the value, and extract from it the individual results.
//
// This interface also provides the ability to validate types;
// this is used internally by szl, and should not be called directly.
//
// Implementations of tables must provide the following static functions,
// and register them with REGISTER_SZL_RESULTS
//
// Implementation of CreateSzlResults.
// static SzlResults* Create(const SzlType&, string* error);
//
// Implementation of IsValid.
// static bool Validate(const SzlType& type, string* error);
//
// Implementation of Properties.
// static void Props(const char* kind, SzlType::TableProperties* props);
//
// Fill in fields with the non-index fields in the result.
// Type is valid and of the appropriate kind for this table.
// static void ElemFields(const SzlType& type, vector<SzlField>* fields);
class SzlResults {
 public:

  // Create a new SzlResults for a specific szl type.
  // If there is an error, NULL is returned,
  // and the error string is filled with a message.
  static SzlResults* CreateSzlResults(const SzlType& type, string* error);

  // Check if the type is a valid instance of this table kind.
  // If not, a reason is returned in error.
  // We already know all indices are valid, as are the types for the
  // element and the weight, which are present iff needed.
  static bool IsValid(const SzlType& type, string* error);

  // Retrieve the TableProperties for this kind of table.
  // Returns true if this kind of table exists, and fills in props if not NULL.
  // Returns false if this kind of table is unknown.
  static bool Properties(const char* kind, SzlType::TableProperties* props);

  // Retrieve a vector of all known table properties
  static void AllProperties(vector<SzlType::TableProperties>* props);

  // Produce a description of the Results.
  // *rtype is filled in with the result type, which is a flattened tuple;
  // that is, it contains no nested tuples.  It may contain arrays and
  // maps, but any tuples they contain must be flattened as well.
  //
  // The order of output is indices, elements, weights, and finally extra
  // information such as deviations.
  //
  // REQUIRES: a validated table.
  static void ResultType(const SzlType& type, SzlType* rtype, int *nindices);

  virtual ~SzlResults();

  // Read a value string.  Returns true if string successfully decoded.
  virtual bool ParseFromStringWithIndex(const string& index,
                                        const string& val) {
    return ParseFromString(val);
  }
  virtual bool ParseFromString(const string& val) = 0;

  // Get the individual results.  The are SzlEncoded; see ResultType for
  // a description of their format.
  virtual const vector<string>* Results() = 0;

  // Report the total elements added to the table.
  virtual int64 TotElems() const = 0;

 protected:
  static const char* kValueLabel;
  static const char* kWeightLabel;

  // A helper to add the flattened field descriptions for a single
  // field in a table.
  // Uses deflabel if the field has no label.
  // Use kValueLabel for elements, kWeightLabel for weights,
  // and something appropriate for extra fields.
  static void AppendField(const SzlField* field, const char* deflabel,
                          vector<SzlField>* fields);
};

// Plumbing for auto-registration of szl results types.
class SzlResultsRegisterer {
 public:
  SzlResultsRegisterer(const char* kind,
                       SzlResults* (*creator)(const SzlType&, string*),
                       bool (*validate)(const SzlType&, string*),
                       void (*props)(const char*, SzlType::TableProperties*),
                       void (*elemfields)(const SzlType&, vector<SzlField>*));
};

#define REGISTER_SZL_RESULTS(kind, name) \
static SzlResultsRegisterer __szl ## kind ## _results__creator(#kind,\
                                 &name::Create,\
                                 &name::Validate,\
                                 &name::Props,\
                                 &name::ElemFields)

// Registration of a non-mill table.  These tables are used only for
// type checking of instances of their table types.
#define REGISTER_SZL_NON_MILL_RESULTS(kind, validate, props) \
static SzlResultsRegisterer __szl ## kind ## _results__creator(#kind,\
                                 NULL,\
                                 validate,\
                                 props,\
                                 NULL)

#endif  // _PUBLIC_SZLRESULTS_H__
