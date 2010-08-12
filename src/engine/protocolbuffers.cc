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

#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/wire_format_lite.h"
#include "google/protobuf/wire_format_lite_inl.h"

#include "engine/globals.h"
#include "public/commandlineflags.h"
#include "public/logging.h"

#include "utilities/strutils.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/proc.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/taggedptrs.h"
#include "engine/form.h"
#include "engine/val.h"
#include "engine/factory.h"

#include "engine/protocolbuffers.h"

DEFINE_bool(strict_input_types, false,
            "unknown tags in input buffers are fatal");
DEFINE_bool(parsed_messages, true,
            "convert parsed messages back into parsed messages");

// Terminology: Szl uses the term "tag" to refer to a field id number,
// i.e. what protocol buffers refer to as a field number.  Protocol
// buffer use the term "tag" to refer to the a containing both
// the field id and the wire type.
// In order to avoid confusing "tag" with "field number" with "field ordinal",
// and also to avoid confusing protocol buffer wire type with szl type,
// we use the following terms in this file only:
//   uint32 tag - protocol buffer tag, containing the field id and wire type
//   int id - field id, or "field number" in protocol buffer terminology
//   WireType wire_type - protocol buffer wire type
//   Type* type - szl type

namespace sawzall {

namespace protocolbuffers {

typedef google::protobuf::internal::WireFormatLite WireFormatLite;
typedef google::protobuf::io::ZeroCopyInputStream ZeroCopyInputStream;
typedef google::protobuf::io::ArrayInputStream ArrayInputStream;
typedef google::protobuf::io::StringOutputStream StringOutputStream;
typedef google::protobuf::io::CodedInputStream CodedInputStream;
typedef google::protobuf::io::CodedOutputStream CodedOutputStream;


static const char* ValueIntoProto(Proc* proc, CodedOutputStream* stream,
                                  Type* type, ProtoBufferType output_type,
                                  Val* value, int id);


static const char* ReadGroup(Proc* proc, CodedInputStream* stream,
                             TupleVal** value, TupleType* tuple);


const char* DefaultItem(Proc* proc, Val** dst, Field* field, bool readonly) {
  Type* type = field->type();
  if (! type->is_structured()) {
    if (field->has_value()) {
      assert(field->value()->AsLiteral() != NULL);
      *dst = field->value()->AsLiteral()->val();
    } else {
      // fill the slot with the type-specific 0 value
      *dst = type->as_basic()->form()->NewValBasic64(proc, type, 0);
    }
    if (readonly)
      (*dst)->set_readonly();
    return NULL;  // success
  }

  if (type->is_bytes() || type->is_string() || type->is_array()) {
    if (type->is_bytes()) {
      if (field->has_value()) {
        *dst = field->value()->as_bytes();
        if (*dst == NULL) {
          // TODO: This may never happen; if not, should remove it.
          return proc->PrintError(
            "cannot handle default value for field %s: %T",
            field->name(), field->type());
        }
      } else {
        // allocate empty bytes
        *dst = Factory::NewBytes(proc, 0);
      }
    } else if (type->is_string()) {
      if (field->has_value()) {
        *dst = field->value()->as_string();
      } else {
        // allocate empty string
        *dst = Factory::NewString(proc, 0, 0);
      }
    } else if (type->is_array()) {
      if (field->has_value()) {
        // we have no array literals at the moment - this cannot happen
        return "no support for array literals as default values";
      } else {
        // allocate empty array
        *dst = type->as_array()->form()->NewVal(proc, 0);
      }
    } else {
      return proc->PrintError(
        "cannot handle default value for field %s: %T",
        field->name(), field->type());
    }
    if (readonly)
      (*dst)->set_readonly();
    return NULL;  // success
  }

  if (type->is_tuple())
    return DefaultTuple(proc, reinterpret_cast<TupleVal**>(dst),
                        type->as_tuple(), readonly);

  ShouldNotReachHere();  // or we are missing some case
  return NULL;
}


const char* DefaultTuple(Proc* proc, TupleVal** dst, TupleType* tuple,
                         bool readonly) {
  assert(tuple->is_proto());
  TupleVal* t = tuple->form()->NewVal(proc, TupleForm::clear_inproto);
  List<Field*>* fields = tuple->fields();
  for (int i = 0; i < fields->length(); i++) {
    Field* field = fields->at(i);
    if (!field->type()->is_bad() && field->read()) {
      Val** field_val = &t->field_at(field);
      // TODO: consider alternatives to this check
      const char* error = DefaultItem(proc, field_val, field, readonly);
      if (error != NULL)
        return error;  // DefaultItem failed
    }
  }
  if (readonly)
    t->set_readonly();
  *dst = t;
  return NULL;  // success
}



static const char* ReadItem(Proc* proc, CodedInputStream* stream, Val** dst,
                            Type* type, uint32* tag, bool append) {
  // At entry: tag for this field has already been read and is in "*tag".
  // At exit: tag for the following field has already been read and stored
  // into "*tag".
  assert(stream->LastTagWas(*tag));

  // if reading of a subcomponent fails and we have more details
  // about the problem, error is set to the error message
  const char* error = NULL;

  Type::FineType fine_type = type->fine_type();
  if (append && fine_type != Type::ARRAY)
    return "duplicate tag";
  WireFormatLite::WireType wire_type = WireFormatLite::GetTagWireType(*tag);
  switch (fine_type) {
    case Type::INT:
    case Type::UINT:
    case Type::BOOL:
    case Type::FINGERPRINT:
    case Type::TIME:
      { uint64 val;
        bool ok = false;  // assume reading fails
        switch (wire_type) {
          case WireFormatLite::WIRETYPE_VARINT:
            ok = WireFormatLite::ReadPrimitive<uint64, WireFormatLite::TYPE_UINT64>(stream, &val);
            break;
          case WireFormatLite::WIRETYPE_FIXED32:
            { uint32 val32 = 0;
              ok = WireFormatLite::ReadPrimitive<uint32, WireFormatLite::TYPE_FIXED32>(stream, &val32);
              val = val32;
            }
            break;
          case WireFormatLite::WIRETYPE_FIXED64:
            ok = WireFormatLite::ReadPrimitive<uint64, WireFormatLite::TYPE_FIXED64>(stream, &val);
            break;
          default:
            error = "field type is numeric but data type is not";
            break;
        }
        if (ok) {
          // reading was successful
          switch (fine_type) {
            case Type::INT:
              *dst = SymbolTable::int_form()->NewVal(proc, val);
              break;
            case Type::UINT:
              *dst = SymbolTable::uint_form()->NewVal(proc, val);
              break;
            case Type::BOOL:
              *dst = Factory::NewBool(proc, val != 0);
              break;
            case Type::FINGERPRINT:
              *dst = Factory::NewFingerprint(proc, val);
              break;
            case Type::TIME:
              *dst = Factory::NewTime(proc, val);
              break;
            default:
              ShouldNotReachHere();
          }
          *tag = stream->ReadTag();
          return NULL;
        }
        if (error == NULL)
          error = "numeric ReadPrimitive() failed";
      }
      break;

    case Type::FLOAT:
      switch (wire_type) {
        case WireFormatLite::WIRETYPE_FIXED32:
        { float val;
          if (WireFormatLite::ReadPrimitive<float, WireFormatLite::TYPE_FLOAT>(stream, &val)) {
            *dst = Factory::NewFloat(proc, val);
            *tag = stream->ReadTag();
            return NULL;  // successful
          }
        }
        break;
        case WireFormatLite::WIRETYPE_FIXED64:
        { double val;
          if (WireFormatLite::ReadPrimitive<double, WireFormatLite::TYPE_DOUBLE>(stream, &val)) {
            *dst = Factory::NewFloat(proc, val);
            *tag = stream->ReadTag();
            return NULL;  // successful
          }
        }
        break;
      default:
        error = "field type is floating-point but data type does not match";
        break;
      }
      if (error == NULL)
        error = "floating-point ReadPrimitive() failed";
      break;

    case Type::BYTES:
      if (wire_type == WireFormatLite::WIRETYPE_LENGTH_DELIMITED) {
        uint32 len;
        if (stream->ReadVarint32(&len) && len >= 0) {
          BytesVal* val = Factory::NewBytes(proc, len);
          if (stream->ReadRaw(val->base(), len)) {
            *dst = val;
            *tag = stream->ReadTag();
            return NULL;  // successful
          }
          val->dec_ref();  // failed, abandon the BytesVal
        }
        error = "Read of a bytes value failed";
        break;
      }
      error = "field type is 'bytes' but data type is not LENGTH_DELIMITED";
      break;

    case Type::STRING:
      if (wire_type == WireFormatLite::WIRETYPE_LENGTH_DELIMITED) {
        uint32 len;
        if (stream->ReadVarint32(&len)) {
          // Cannot use ReadRaw as for bytes because we have to validate
          // the string as UTF8 and may modify it.
          string scratch;
          if (stream->ReadString(&scratch, len)) {
            // build Sawzall string
            *dst = Factory::NewStringBytes(proc, len, scratch.data());
            *tag = stream->ReadTag();
            return NULL;  // successful
          }
        }
        error = "Read of a string failed";
        break;
      }
      error = "field type is 'bytes' but data type is not LENGTH_DELIMITED";
      break;

    case Type::ARRAY:
      { ArrayType* array = type->as_array();
        Type* elem_type = array->elem_type();
        // read array elements
        // load them into a vector first (for speed)
        vector<Val*> elements;
        assert(error == NULL);
        int id = WireFormatLite::GetTagFieldNumber(*tag);
        while (true) {
          // TODO: look into packed arrays
          Val* valptr;
          error = ReadItem(proc, stream, &valptr, elem_type, tag, false);
          if (error != NULL)
            break;
          elements.push_back(valptr);
          // There appears to be no reason why the elements of an array
          // must all have the same wire type.  So stop only when the
          // field id changes.
          if (WireFormatLite::GetTagFieldNumber(*tag) != id)
            break;
        }

        // return the result, if any
        // note that 0-element arrays don't show up in the proto-buffer
        // (we handle them by setting default values for missing fields)
        if (error == NULL) {
          // we had no errors while reading the array.
          ArrayVal* val;
          int n = elements.size();
          int begin;  // destination index for first element
          int end;  // destination index for last element (+1)
          if (!append) {
            begin = 0;
            end = n;
            val = array->form()->NewVal(proc, end);
          } else {
            // extremely rare case: some elements of the array appeared earlier in the data.
            // copy them to the beginning of a resized array.
            assert(*dst != NULL);
            ArrayVal* prefix = (*dst)->as_array();
            assert(prefix != NULL);
            begin = prefix->length();
            end = begin + n;
            val = array->form()->NewVal(proc, end);
            for (int i = 0; i < begin; i++) {
              val->at(i) = prefix->at(i);  // note: we haven't inc_ref'ed the elements...
              prefix->at(i) = NULL;  // ... so we remove them from the old array
            }
            (*dst)->dec_ref();  // release the old array
          }
          for (int i = 0; i < n; i++) {
            val->at(begin + i) = elements[i];
          }
          *dst = val;
          return NULL;  // successful; next tag has already been read
        }
      }
      // error while reading the array => reading failed
      break;

    case Type::TUPLE:
      { TupleType* tuple = type->as_tuple();
        assert(tuple->is_proto());

        // foreign group
        if (wire_type == WireFormatLite::WIRETYPE_LENGTH_DELIMITED) {
          uint32 len;
          if (stream->ReadVarint32(&len) && len > 0) {
            // Since we know we're decoding directly from an array,
            // we should be able to get direct access to the buffer.
            const void* data;
            int size;
            stream->GetDirectBufferPointerInline(&data, &size);
            if (size >= len) {
              ArrayInputStream msg_data(data, len);
              CodedInputStream msg_stream(&msg_data);
              error = ReadGroup(proc, &msg_stream,
                                reinterpret_cast<TupleVal**>(dst), tuple);
              if (error == NULL) {
                // If ended with a bogus END_GROUP tag or a non-EOF zero tag.
                if (!msg_stream.ConsumedEntireMessage())
                  return "unexpected END_GROUP or invalid tag found";
                stream->Skip(len);
                *tag = stream->ReadTag();
                return NULL;
              }
            }
          }
          if (error == NULL)
            error = "Read of an embedded message failed";

          // embedded group
        } else if (wire_type == WireFormatLite::WIRETYPE_START_GROUP) {
          error = ReadGroup(proc, stream, reinterpret_cast<TupleVal**>(dst), tuple);
          if (error == NULL) {
            // Tag that stopped ReadGroup is always END_GROUP or zero.
            if (stream->LastTagWas(0))
              return "END_GROUP tag is missing";
            *tag = stream->ReadTag();
            return NULL;
          }
        } else {
          error = "field type is 'tuple' but data type is not a group";
        }
      }
      break;

    default:
      ShouldNotReachHere();
  }

  // reading failed
  assert(error != NULL);
  if (FLAGS_v > 0)
    F.print("reading proto field (%T @ %d) failed (%s)"
                     "(wrong proto file used?)", type, tag, error);
  return proc->PrintError("reading proto field (%T @ %d) failed (%s)"
                     "(wrong proto file used?)", type, tag, error);
}


static const char* ReadGroup(Proc* proc, CodedInputStream* stream,
                             TupleVal** value, TupleType* tuple) {
  // At entry the first field tag has not yet been read.
  // At exit the tag that terminated the group has been read,
  // and that tag was either END_GROUP or zero.
  assert(tuple->is_proto());

  // allocate space for the tuple
  TupleVal* tvalue = tuple->form()->NewVal(proc, TupleForm::clear_inproto);
  *value = tvalue;

  const bool preallocated_default = (tuple->default_proto_val() != NULL);
  size_t size = 0;
  if (preallocated_default) {
    TupleVal* default_proto_val = tuple->default_proto_val();
    size = tuple->nslots();  // do not copy inproto bits
    // Do an exact memory copy of the proto buffer default value.  Note that
    // if the default value contains Val*'s, those Val*'s will be copied as-is
    // without properly updating their ref counts. However, their ref counts
    // are set to "infinite" so this should be ok.
    for (int i = 0; i < size; i++)
      tvalue->slot_at(i) = default_proto_val->slot_at(i);
  }
  // read the tuple fields
  uint32 tag = stream->ReadTag();  // at top of loop tag has already been read
  while (tag != 0) {
    const void* unused_ptr;       // for computing number of bytes skipped
    int available_before = 0;     // ditto (init in case stream already at end)
    int available_after = 0;      // ditto (init in case stream already at end)
    WireFormatLite::WireType wire_type = WireFormatLite::GetTagWireType(tag);
    if (wire_type == WireFormatLite::WIRETYPE_END_GROUP)
      break;
    int id = WireFormatLite::GetTagFieldNumber(tag);
    Field* field = tuple->field_for(id);
    if (field == NULL) {
      // an error syndrome when calling this code with invalid input
      // is that index goes negative and field number becomes zero.
      if (id == 0 || FLAGS_strict_input_types)
        return proc->PrintError("field for tag: %d (proto type id = %d) "
                     "not found (wrong input format or wrong proto file?)",
                     id, wire_type);
      else if (FLAGS_v > 0)
        F.print("we are ignoring unknown tag: %d\n", id);
      stream->GetDirectBufferPointerInline(&unused_ptr, &available_before);
      if (!WireFormatLite::SkipField(stream, tag))
        return proc->PrintError("field for tag: %d (proto type id = %d) "
                                "could not be skipped; corrupt data?",
                                id, wire_type);
      stream->GetDirectBufferPointerInline(&unused_ptr, &available_after);
      proc->add_proto_bytes_skipped(CodedOutputStream::VarintSize32(tag) + 
                                    available_before - available_after);
      tag = stream->ReadTag();
    } else {
      if (field->read()) {
        // use the inproto bit to decide if we must append to existing data
        if (ReadItem(proc, stream, &tvalue->field_at(field), field->type(),
                     &tag, tvalue->field_bit_at(tuple, field)) == NULL) {
          // field successfully read -- set the bit in the inproto bit vector
          tvalue->set_field_bit_at(tuple, field);
        } else {
          // skip the field
          // TODO: we probably need to handle this case better.
          // (should we fail the conversion?)
          if (FLAGS_v > 0)
            F.print("we are skipping field: %s\n", field->name());
          stream->GetDirectBufferPointerInline(&unused_ptr, &available_before);
          if (!WireFormatLite::SkipField(stream, tag))
            return proc->PrintError("field for tag: %d (proto type id = %d) "
                                    "could not be skipped; corrupt data?",
                                    id, wire_type);
          stream->GetDirectBufferPointerInline(&unused_ptr, &available_after);
          proc->add_proto_bytes_skipped(CodedOutputStream::VarintSize32(tag) +
                                        available_before - available_after);
          tag = stream->ReadTag();
        }
      } else {
        // TODO: might want to increase to level 2 or 3
        if (FLAGS_v > 0)
          F.print("we are ignoring unused tag: %d\n", id);
        stream->GetDirectBufferPointerInline(&unused_ptr, &available_before);
        if (!WireFormatLite::SkipField(stream, tag))
          return proc->PrintError("field for tag: %d (proto type id = %d) "
                                  "could not be skipped; corrupt data?",
                                  id, wire_type);
        stream->GetDirectBufferPointer(&unused_ptr, &available_after);
        proc->add_proto_bytes_skipped(CodedOutputStream::VarintSize32(tag) +
                                      available_before - available_after);
        tag = stream->ReadTag();
      }
    }
  }

  if (!preallocated_default) {
    // fill in default values for all referenced fields that haven't been read
    const List<Field*>* fields = tuple->fields();
    for (int i = 0; i < fields->length(); i++) {
      Field* field = fields->at(i);
      if (field->read() && !tvalue->field_bit_at(tuple, field)) {
        if (!field->type()->is_bad()) {
          // TODO: consider alternatives to this check
          Val** fv = &tvalue->field_at(field);
          const char* error =
            DefaultItem(proc, fv, field, false);
          if (error != NULL)
            return error;  // DefaultItem failed
        }
      }
    }
  }
  // done
  return NULL;  // success
}


static const char* TupleIntoProto(Proc* proc, CodedOutputStream* stream,
                                  TupleType* proto, TupleVal* value) {
  List<Field*>* fields = proto->fields();
  // for each Field, encode it into the ProtocolBuffer.
  // It's not so easy to do this simply, because the values are
  // not uniformly Desc.
  for (int i = 0; i < fields->length(); i++) {
    Field* field = fields->at(i);
    assert(field->has_tag()); // TODO: useful message here
    // only encode the field if it is actually present
    // (no need to encode default values for missing optional fields)
    if (value->field_bit_at(proto, field)) {
      const char* error = ValueIntoProto(proc, stream, field->type(),
                   field->pb_type(), value->field_at(field), field->tag());
      if (error != NULL)
        return error;
    }
  }
  return NULL;  // success
}


// When a proto buffer message is output, each field will be converted
// to the underlying type specified in the declaration.  If no underlying
// type was specified, use this default.
static ProtoBufferType DefaultProtoBufferType(BasicType* type) {
  if (type->is_bool())
    return PBTYPE_BOOL;
  if (type->is_int())
    return PBTYPE_INT64;
  if (type->is_uint())
     return PBTYPE_UINT64;
  if (type->is_fingerprint() || type->is_time())
    return PBTYPE_FIXED64;
  if (type->is_float())
    return PBTYPE_DOUBLE;
  if (type->is_bytes())
    return PBTYPE_BYTES;
  if (type->is_string())
    return PBTYPE_STRING;
  ShouldNotReachHere();
  return PBTYPE_UNKNOWN;
}


static const char* ArrayIntoProto(Proc* proc, CodedOutputStream* stream,
                                  ArrayType* array_type,
                                  ProtoBufferType output_type,
                                  ArrayVal* array_value, int id) {
  Type* type = array_type->elem_type();
  // Check for the array element type we do not handle;
  // all other types we let ValueIntoProto() handle.
  if (type->is_string()) {
    return proc->PrintError(
      "Conversion of %T in protocol buffer not defined, "
      "convert to bytes first\n", type);
  } else if (type->is_array()) {
    // I don't think this can happen (in protocol buffers),
    // so there is no way of reading it back
    return proc->PrintError(
      "Conversion of %T in protocol buffer not defined\n",
      array_type);
  } else if (!type->is_tuple() && !type->is_basic()) {
    return proc->PrintError(
      "elem type %T in array not convertible into proto\n", type);
  }

  for (int i = 0; i < array_value->length(); i++) {
    Val* value = array_value->at(i);
    const char* error = ValueIntoProto(proc, stream, type, output_type, value, id);
    if (error != NULL)
      return error;
  }
  return NULL;  // success
}


static const char* BasicIntoProto(Proc* proc, CodedOutputStream* stream,
                                  Type* type, ProtoBufferType output_type,
                                  Val* value, int id) {
  if (output_type == PBTYPE_UNKNOWN) {
    output_type = DefaultProtoBufferType(type->as_basic());
  }

  switch (output_type) {
    case PBTYPE_UNKNOWN:
      return proc->PrintError(
        "Conversion of %T in protocol buffer not defined\n", type);

    case PBTYPE_BOOL:
      WireFormatLite::WriteBool(id, value->as_bool()->val(), stream);
      return NULL;  // success

    case PBTYPE_STRING:
    case PBTYPE_BYTES: {
      // String and bytes values have the same wire format.
      // Write tag, length and value directly because there is no other
      // way to avoid copying the data to a string.
      char* base;
      int length;
      if (value->is_string()) {
        base = value->as_string()->base();
        length = value->as_string()->length();
      } else if (value->is_bytes()) {
        base = value->as_bytes()->base();
        length = value->as_bytes()->length();
      } else {
        ShouldNotReachHere();
      }

      WireFormatLite::WriteTag(id, WireFormatLite::WIRETYPE_LENGTH_DELIMITED,
                               stream);
      stream->WriteVarint32(length);
      stream->WriteRaw(base, length);
      return NULL;  // success
    }

    case PBTYPE_DOUBLE:
      WireFormatLite::WriteDouble(id, value->as_float()->val(), stream);
      return NULL;  // success

    case PBTYPE_FLOAT:
      WireFormatLite::WriteFloat(id, value->as_float()->val(), stream);
      return NULL;  // success

    case PBTYPE_FIXED64:
      WireFormatLite::WriteFixed64(id, value->basic64(), stream);
      return NULL;  // success

    case PBTYPE_FIXED32:
      WireFormatLite::WriteFixed32(id, value->basic64(), stream);
      return NULL;  // success

    case PBTYPE_INT64:
    case PBTYPE_UINT64:
      WireFormatLite::WriteUInt64(id, value->basic64(), stream);
      return NULL;  // success

    case PBTYPE_INT32:
    case PBTYPE_UINT32:
      WireFormatLite::WriteUInt64(id, (int32) value->as_int()->val(), stream);
      return NULL;  // success
  }

  ShouldNotReachHere();
  return NULL;
}


static const char* ValueIntoProto(Proc* proc, CodedOutputStream* stream,
                                  Type* type, ProtoBufferType output_type,
                                  Val* value, int id) {
  if (type->is_basic()) {
    return BasicIntoProto(proc, stream, type, output_type, value, id);
  } else if (type->is_array()) {
    return ArrayIntoProto(
      proc, stream, type->as_array(), output_type, value->as_array(), id);
  } else if (type->is_tuple()) {
    TupleType* tuple = type->as_tuple();
      if (FLAGS_parsed_messages && tuple->is_message()) {
      string message;  // not valid until msg_stream destructor runs
      {
        StringOutputStream msg_data(&message);
        CodedOutputStream msg_stream(&msg_data);
        const char* error = TupleIntoProto(proc, &msg_stream, tuple,
                                           value->as_tuple());
        if (error != NULL)
          return error;
      }
      // TODO: point out that it should be possible to do
      // the proto1 operation "MoveStringFromCodedOutputStream" in proto2.
      WireFormatLite::WriteBytes(id, message, stream);
    } else {
      WireFormatLite::WriteTag(id, WireFormatLite::WIRETYPE_START_GROUP,
                               stream);
      const char* error = TupleIntoProto(proc, stream, tuple,
                                         value->as_tuple());
      if (error != NULL)
        return error;
      WireFormatLite::WriteTag(id, WireFormatLite::WIRETYPE_END_GROUP, stream);
    }
  } else {
    return proc->PrintError(
      "Conversion of %T in protocol buffer not defined\n", type);
  }
  return NULL;  // success
}


const char* ReadTuple(Proc* proc, TupleType* proto, TupleVal** value,
                      BytesVal* bytes) {
  proc->add_proto_bytes_read(bytes->length());
  ArrayInputStream data(bytes->base(), bytes->length());
  CodedInputStream stream(&data);
  const char* error = ReadGroup(proc, &stream, value, proto);
  // If ended with a bogus END_GROUP tag or a non-EOF zero tag.
  if (error == NULL && !stream.ConsumedEntireMessage())
    return "unexpected END_GROUP or invalid tag found";
  return error;
}


const char* WriteTuple(Proc* proc, TupleType* proto, TupleVal* value,
                       BytesVal** bytes) {
  assert(proto->is_proto());
  // construct the CodedOutputStream that goes with the proto
  string message;  // not valid until msg_stream destructor runs
  {
    StringOutputStream data(&message);
    CodedOutputStream stream(&data);;
    const char* error = TupleIntoProto(proc, &stream, proto, value);
    if (error != NULL)
      return error;
  }

  // copy content into a bytes value and return it
  BytesVal* b = Factory::NewBytes(proc, message.length());
  memcpy(b->base(), message.data(), message.length());
  *bytes = b;
  return NULL;  // success
}


ProtoBufferType ParseProtoBufferType(szl_string type_name) {
  if (strcmp(type_name, "double") == 0) {
    return PBTYPE_DOUBLE;
  }
  if (strcmp(type_name, "float") == 0) {
    return PBTYPE_FLOAT;
  }
  if (strcmp(type_name, "int64") == 0) {
    return PBTYPE_INT64;
  }
  if (strcmp(type_name, "uint64") == 0) {
    return PBTYPE_UINT64;
  }
  if (strcmp(type_name, "int32") == 0) {
    return PBTYPE_INT32;
  }
  if (strcmp(type_name, "uint32") == 0) {
    return PBTYPE_UINT32;
  }
  if (strcmp(type_name, "fixed64") == 0) {
    return PBTYPE_FIXED64;
  }
  if (strcmp(type_name, "fixed32") == 0) {
    return PBTYPE_FIXED32;
  }
  if (strcmp(type_name, "boolean") == 0) {
    return PBTYPE_BOOL;
  }
  if (strcmp(type_name, "bool") == 0) {
    return PBTYPE_BOOL;
  }
  if (strcmp(type_name, "bytes") == 0) {
    return PBTYPE_BYTES;
  }
  if (strcmp(type_name, "string") == 0) {
    return PBTYPE_STRING;
  }
  return PBTYPE_UNKNOWN;
}


szl_string ProtoBufferTypeName(ProtoBufferType pb_type) {
  switch (pb_type) {
    case PBTYPE_DOUBLE:
      return "double";
    case PBTYPE_FLOAT:
      return "float";
    case PBTYPE_INT64:
      return "int64";
    case PBTYPE_UINT64:
      return "uint64";
    case PBTYPE_INT32:
      return "int32";
    case PBTYPE_UINT32:
      return "uint32";
    case PBTYPE_FIXED64:
      return "fixed64";
    case PBTYPE_FIXED32:
      return "fixed32";
    case PBTYPE_BOOL:
      return "bool";
    case PBTYPE_BYTES:
      return "bytes";
    case PBTYPE_STRING:
      return "string";
    default:
      F.fprint(2, "unknown pb_type %d\n", pb_type);
      ShouldNotReachHere();
      return "?";
  }
}


TypeCompatibility ComputeTypeCompatibility(
  ProtoBufferType pb_type, BasicType* szl_type) {

  switch (pb_type) {
    case PBTYPE_DOUBLE:
      return (szl_type->is_float() ? COMPAT_OK : COMPAT_INVALID);

    case PBTYPE_FLOAT:
      return (szl_type->is_float() ? COMPAT_MAY_OVERFLOW : COMPAT_INVALID);

    case PBTYPE_INT64:
      return ((szl_type->is_int() || szl_type->is_uint()) ? COMPAT_OK : COMPAT_INVALID);

    case PBTYPE_UINT64:
      if (szl_type->is_uint())
        return  COMPAT_OK;
      /* fall through */

    case PBTYPE_INT32:
      return (szl_type->is_int() ? COMPAT_MAY_OVERFLOW : COMPAT_INVALID);

    case PBTYPE_FIXED32:
      if (szl_type->is_uint() || szl_type->is_int())
        return COMPAT_MAY_OVERFLOW;
      else
        return COMPAT_INVALID;

    case PBTYPE_FIXED64:
      if (szl_type->is_int())
        return COMPAT_MAY_OVERFLOW;
      else if (szl_type->is_uint() ||
               szl_type->is_fingerprint() || szl_type->is_time())
        return COMPAT_OK;
      else
        return COMPAT_INVALID;

    case PBTYPE_BOOL:
      return (szl_type->is_bool() ? COMPAT_OK : COMPAT_INVALID);

    case PBTYPE_BYTES:
    case PBTYPE_STRING:
      return (szl_type->is_bytes() || szl_type->is_string() ?
              COMPAT_OK : COMPAT_INVALID);

    default:
      return COMPAT_INVALID;
  }
}


const char* EncodeForOutput(Proc* proc, string* result, Type* type, Val* value,
                       int id) {
  // TODO: find a way to keep this around instead of constructing
  // each time.
  StringOutputStream data(result);
  CodedOutputStream stream(&data);
  // The output type is only used for basic types and arrays of basic types.
  // For those cases we are encoding non-proto Sawzall values for which there
  // is no associated protocol buffer encoding type.
  return ValueIntoProto(proc, &stream, type, PBTYPE_UNKNOWN, value, id);
}

}  // namespace protocolbuffers

}  // namespace sawzall
