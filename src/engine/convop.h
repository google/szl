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

namespace sawzall {

class ConvOp {
 public:
  // Conversions return NULL if they are successful,
  // or an error string if they fail. The error string
  // should indicate the problem (there is no need
  // to include the name of the conversion operator
  // as it is known to the interpreter anyway).
  static const char* ConvertBasic(Proc* proc, ConversionOp op, Val**& sp, Type* type);
  static const char* ConvertArray(Proc* proc, ConversionOp op, Val**& sp, ArrayType* type);
  static const char* ConvertArrayToMap(Proc* proc, MapType* map_type,
                                       ConversionOp key_op, ConversionOp value_op,
                                       Val**& sp);
};

bool ImplementedArrayToArrayConversion(ConversionOp op);
bool ImplementedArrayToMapConversion(ConversionOp op);

// for printing
const char* ConversionOp2String(ConversionOp op);

// for optimization
bool ConversionCanFail(ConversionOp op);


}  // namespace sawzall
