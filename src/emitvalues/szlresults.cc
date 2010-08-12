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

#include "public/porting.h"
#include "public/logging.h"
#include "public/hash_map.h"
#include "public/hashutils.h"

#include "utilities/strutils.h"

#include "public/szltype.h"
#include "public/szlresults.h"

static void ResultArray(const SzlType& t, SzlType* result);

const char* SzlResults::kValueLabel = "value_";
const char* SzlResults::kWeightLabel = "weight_";

struct SzlResultsStatics {
  SzlResults* (*creator)(const SzlType&, string*);
  bool        (*validate)(const SzlType&, string*);
  void        (*props)(const char*, SzlType::TableProperties*);
  void        (*elemfields)(const SzlType&, vector<SzlField>*);
};

typedef hash_map<string, SzlResultsStatics> SzlResultsCreators;
static SzlResultsCreators* creators;

// empty destructor for the abstract base class
SzlResults::~SzlResults() {
}

SzlResultsRegisterer::SzlResultsRegisterer(const char* kind,
                      SzlResults* (*creator)(const SzlType&, string*),
                      bool (*validate)(const SzlType&, string*),
                      void (*props)(const char*, SzlType::TableProperties*),
                      void (*elemfields)(const SzlType&, vector<SzlField>*)) {
  CHECK(validate != NULL) << ": no validate registered for " << kind;
  CHECK(props != NULL) << ": no props registered for " << kind;

  if (creators == NULL)
    creators = new SzlResultsCreators;

  CHECK(creators->find(kind) == creators->end())
    << ": multiple registrations of the same saw results type kind " << kind;

  (*creators)[kind].creator = creator;
  (*creators)[kind].validate = validate;
  (*creators)[kind].props = props;
  (*creators)[kind].elemfields = elemfields;
}

SzlResults* SzlResults::CreateSzlResults(const SzlType& t, string* error) {
  CHECK(creators != NULL) << ": No SzlResults are registered";
  CHECK(t.kind() == SzlType::TABLE);
  SzlResultsCreators::iterator creator = creators->find(t.table());
  if (creator == creators->end()) {
    *error = "unknown saw results type";
    return NULL;
  }

  if (creator->second.creator == NULL) {
    *error = StringPrintf("can't read result for non-mill table of type %s",
                          t.PPrint().c_str());
    return NULL;
  }

  return (*creator->second.creator)(t, error);
}

// Check if the mill type is a valid instance of this table kind.
// If not, a reason is returned in error.
// We already know all indices are valid, as are the types for the
// element and the weight, which is present iff it's needed.
bool SzlResults::IsValid(const SzlType& t, string* error) {
  if (creators == NULL) {
    *error = "no known tables";
    return false;
  }
  CHECK(t.kind() == SzlType::TABLE);
  SzlResultsCreators::iterator creator = creators->find(t.table());
  if (creator == creators->end()) {
    *error = "unknown saw type ";
    return false;
  }
  return (*creator->second.validate)(t, error);
}

bool SzlResults::Properties(const char* kind,
                            SzlType::TableProperties* props) {
  if (creators == NULL) {
    return false;
  }
  SzlResultsCreators::iterator creator = creators->find(kind);
  if (creator == creators->end()) {
    return false;
  }
  if (props != NULL) {
    (*creator->second.props)(kind, props);
  }
  return true;
}

void SzlResults::AllProperties(vector<SzlType::TableProperties>* props) {
  if (creators == NULL) {
    return;
  }
  for (SzlResultsCreators::iterator it = creators->begin();
       it != creators->end(); it++) {
    SzlType::TableProperties prop;
    (*it->second.props)(it->first.c_str(), &prop);
    props->push_back(prop);
  }
}

// Handle a field and, if it's a tuple, its recursive fields.
static void ResultFields(const SzlType& t,
                         bool unprefixed,
                         const string& prefix,
                         vector<SzlField>* fields) {
  if (t.kind() == SzlType::ARRAY || t.kind() == SzlType::MAP) {
    // Fill in the type
    SzlType type(SzlType::VOID);
    ResultArray(t, &type);
    // Put the field and the type.
    fields->push_back(SzlField(prefix, type));
    return;
  } else if (t.BaseType()) {
    fields->push_back(SzlField(prefix, t));
    return;
  } else if (!t.TupleType()) {
    LOG(FATAL) << "can't create output field descriptions for " << t.PPrint();
  }

  for (int i = 0; i < t.fields_size(); ++i) {
    string name;
    if (!unprefixed || t.field(i).label().empty())
      name = prefix + "_";
    if (t.field(i).label().empty())
      StringAppendF(&name, "%d", i);
    else
      name += t.field(i).label();
    ResultFields(t.field(i).type(), false, name, fields);
  }
}

// Convert an array or map type into its result type.
static void ResultArray(const SzlType& t, SzlType* result) {
  // Recurse along the "array of" chain.  We can't iterate, because we'll
  // end up with a const type, and we need to flatten tuples at the bottom.
  if (t.kind() == SzlType::ARRAY) {
    SzlType type(SzlType::VOID);
    ResultArray(t.element()->type(), &type);
    SzlField elem(t.element()->label(), type);
    result->set_kind(SzlType::ARRAY);
    result->set_element(&elem);
    return;
  } else if (t.kind() == SzlType::MAP) {
    result->set_kind(SzlType::MAP);

    // We don't support multi-index maps.
    if (t.indices_size() != 1) {
      LOG(FATAL) << "unexpected number of indices for \"" << t.PPrint() <<
        "\": " << t.indices_size() << "; each map must have exactly 1 index";
    }
    SzlType index_type(SzlType::kVoid);
    ResultArray(t.index(0).type(), &index_type);
    result->AddIndex(t.index(0).label(), index_type);

    SzlType value_type(SzlType::kVoid);
    ResultArray(t.element()->type(), &value_type);
    result->set_element(t.element()->label(), value_type);
    return;
  } else if (t.BaseType()) {
    // Nothing to do for base types.
    *result = t;
    return;
  } else if (!t.TupleType()) {
    LOG(ERROR) << "can't create output field descriptions for " << t.PPrint();
  }

  // Need to collect the tuple items at the end of the "array of" chain.
  vector<SzlField> efields;
  ResultFields(t, true, "", &efields);

  // Make them into a tuple.
  result->set_kind(SzlType::TUPLE);
  for (int i = 0; i < efields.size(); i++)
    result->AddField(efields[i].label(), efields[i].type());
}

// Handle all of the indices.
static void ResultIndicies(const SzlType& t, vector<SzlField>* fields) {
  for (int i = 0; i < t.indices_size(); ++i) {
    string name = t.index(i).label();
    if (name.empty())
      name = StringPrintf("index_%d", i);
    ResultFields(t.index(i).type(), false, name, fields);
  }
}

// A helper to add the flattened field descriptions for a single
// field in a table.
void SzlResults::AppendField(const SzlField* e, const char* defname,
                             vector<SzlField>* fields) {
  string elabel = e->label();
  if (elabel.empty())
    elabel = defname;

  ResultFields(e->type(), false, elabel, fields);
}

// Produce a description of the Results.
// *rtype is filled in with the result type, which is a flattened tuple;
// that is, it contains no nested tuples.  It may contain arrays and maps,
// but any tuples they contain must be flattened as well.
//
// The order of output is indices, elements, weights, and finally extra
// information such as deviations.
//
// REQUIRES: a validated table.
void SzlResults::ResultType(const SzlType& type,
                            SzlType* rtype, int *nindices) {
  CHECK(rtype != NULL);
  CHECK(nindices != NULL);
  CHECK(type.kind() == SzlType::TABLE) << ": not a table: " << type;

  // Find all of the fields
  vector<SzlField> fields;
  ResultIndicies(type, &fields);
  *nindices = fields.size();
  CHECK(creators != NULL);
  SzlResultsCreators::iterator creator = creators->find(type.table());
  CHECK(creator != creators->end());

  // A non-mill table generates no mill output;
  // clean up any index fields we added.
  if (creator->second.elemfields == NULL) {
    fields.clear();
    *nindices = 0;
  } else {
    (*creator->second.elemfields)(type, &fields);
  }

  // Make them into a tuple.
  SzlType t(SzlType::TUPLE);
  for (int i = 0; i < fields.size(); i++)
    t.AddField(fields[i].label(), fields[i].type());

  *rtype = t;
}
