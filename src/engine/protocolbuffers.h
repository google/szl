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

namespace sawzall {

namespace protocolbuffers {


// Used for determining whether a szl type is compatible with an underlying
// proto buffer type.
enum TypeCompatibility {
  // Not compatible
  COMPAT_INVALID,
  // Compatible, but conversions between the types may cause overflow
  COMPAT_MAY_OVERFLOW,
  // Compatible
  COMPAT_OK
};


// Fill the default field value into dst. Returns error message or NULL.
const char* DefaultItem(Proc* proc, Val** dst, Field* field, bool readonly);

// Fill the default tuple value into dst. Returns error message or NULL.
const char* DefaultTuple(Proc* proc, TupleVal** dst, TupleType* tuple,
                         bool readonly);

// Convert the protocol buffer array into the tuple value
// assuming it is of type proto. Returns error message or NULL.
const char* ReadTuple(Proc* proc, TupleType* proto, TupleVal** value,
                      BytesVal* bytes);

// Convert the tuple value into the protocol buffer array
// assuming it is of type proto. Returns error message or NULL.
const char* WriteTuple(Proc* proc, TupleType* proto, TupleVal* value,
                       BytesVal** bytes);

// Return the ProtoBufferType corresponding to the proto buffer type name.
ProtoBufferType ParseProtoBufferType(szl_string type_name);

// Return the proto buffer type name corresponding to a ProtoBufferType.
// The result is not defined for PBTYPE_UNKNOWN.
szl_string ProtoBufferTypeName(ProtoBufferType pb_type);

// Determine whether the given szl type is compatible
// with the given underlying proto buffer type.
TypeCompatibility ComputeTypeCompatibility(ProtoBufferType pb_type,
                                           BasicType* szl_type);

const char* EncodeForOutput(Proc* proc, string* result, Type* type, Val* value,
                            int id);

}  // namespace protocolbuffers

}  // namespace sawzall
