The Sawzall parser invokes the protocol compiler as a child process for each `.proto` file included into a `.szl` program using `proto` clauses.
The protocol compiler takes a `.proto` file and generates the Sawzall code for it. It can be invoked directly like so:

```
Usage: protoc --plugin=/full/path/to/protoc-gen-szl --szl_out=. [--import_path=PATH] PROTO_FILE.proto
```

Below is the mapping between the proto syntax and the corresponding Sawzall syntax for the protocol buffer format.

[Top-Level Statements](#Top_Level_Statements.md)
  * Imports
  * Messages
  * Packages
  * Enums

[Fields](#Fields.md)
  * Semantic Labels
  * Options
  * Scalar Types
  * Group Types
  * Message Types
  * Enums

<br>

<h3>Top Level Statements</h3>

<table><thead><th> <b>Proto</b> </th><th> <b>Sawzall</b> </th></thead><tbody>
<tr><td>  </td><td>  </td></tr>
<tr><td> <b>Imports</b> </td><td>  </td></tr>
<tr><td> <code>import "logs/eventid/eventid.proto"</code> </td><td> <code>proto "logs/eventid/eventid.proto"</code> </td></tr>
<tr><td> <b>Messages</b> </td><td>  </td></tr>
<tr><td> <code>// [parsed] message</code><br><code>message MyMessage {</code><br><code>  ...</code><br><code>};</code> </td><td> <code>type MyMessage = parsedmessage {</code><br><code>  ...</code><br><code>};</code> </td></tr>
<tr><td> <b>Packages</b> </td><td>  </td></tr>
<tr><td> <code>package foo;</code><br><code>message MyMessage {</code><br><code>  ...</code><br><code>};</code> </td><td> <code>type foo.MyMessage = parsedmessage {</code><br><code>  ...</code><br><code>};</code> </td></tr>
<tr><td> <b>Enums</b> </td><td>  </td></tr>
<tr><td> <code>enum Enum {</code><br><code>  FOO = 1;</code><br><code>  BAR = 2;</code><br><code>  BAZ = 3;</code><br><code>};</code><br><br><br><br><br><br><br>`</td><td> <code>type Enum = parsedmessage {</code><br><code>  type Enum = int,</code><br><br><code>  static FOO: Enum = 1,</code><br><code>  static BAR: Enum = 2,</code><br><code>  static BAZ: Enum = 3,</code><br><br><code>  static Enum_names: map[enum_value: int] </code><br><code>    of enum_name: string = {</code><br><code>    3: "BAZ",</code><br><code>    2: "BAR",</code><br><code>    1: "FOO",</code><br><code>  },</code><br><code>};</code><br>`</td></tr>
<tr><td><code>message Baz {</code><br><code>  required Enum required_enum = 1;</code><br><br><code>  optional Enum optional_enum = 2 [default = FOO];</code><br><br><code>  repeated Enum repeated_enum = 3;</code><br><br><code>};</code></td><td><code>type Baz = parsedmessage {</code><br><code>  required_enum:</code><br><code>    Enum @ 3: int32,</code><br><code>  optional_enum:</code><br><code>    Enum = 1 @ 4: int32</code><br><code>  repeated_enum: array of</code><br><code>    Enum @ 3: int32</code><br><code>};</code></td></tr></tbody></table>

<br>

<h3>Fields</h3>

<table><thead><th> <b>Proto</b> </th><th> <b>Sawzall</b> </th></thead><tbody>
<tr><td>  </td><td>  </td></tr>
<tr><td> <b>Semantic Labels</b> </td><td>  </td></tr>
<tr><td><code>  required  bool required_bool  =  1;</code><br><br><code>  optional  bool optional_bool  =  2;</code><br><br><code>  repeated  bool repeated_bool  =  3;</code> </td><td><code>  required_bool:</code><br><code>    bool @ 1: bool,</code><br><code>  optional_bool:</code><br><code>    bool @ 2: bool,</code><br><code>  repeated_bool: array of</code><br><code>    bool @ 3: bool</code> </td></tr>
<tr><td> <b>Options</b> </td><td>  </td></tr>
<tr><td><code>  required  bool  mybool1  =  1 [deprecated=true];</code><br><br><code>  optional  bool  mybool2  =  2 [default=true];</code><br><br><code>  repeated  bool  mybools  =  3 [packed=true];</code></td><td><code>  mybool1:</code><br><code>    bool @ 1: bool,</code><br><code>  mybool2:</code><br><code>    bool = true @ 2: bool,</code><br><code>  mybools:</code><br><code>    bytes @ 3: string</code> </td></tr>
<tr><td> <b>Scalar Types</b> </td><td>  </td></tr>
<tr><td><code>  required    int32 required_int32     =   1;</code><br><br><code>  required    int64 required_int64     =   2;</code><br><br><code>  required   uint32 required_uint32    =   3;</code><br><br><code>   required   uint64 required_uint64    =   4;</code><br><br><code>  required   sint32 required_sint32    =   5;</code><br><br><code>  required   sint64 required_sint64    =   6;</code><br><br><code>   required  fixed32 required_fixed32   =   7;</code><br><br><code>  required  fixed64 required_fixed64   =   8;</code><br><br><code>  required sfixed32 required_sfixed32  =   9;</code><br><br><code>  required sfixed64 required_sfixed64  =  10;</code><br><br><code>   required    float required_float     =  11;</code><br><br><code>  required   double required_double    =  12;</code><br><br><code>  required   string required_string    =  13;</code><br><br><code>  required    bytes required_bytes     =  14;</code><br><br><code>   required     bool required_bool      =  15;</code></td><td><code>  required_int32:</code><br><code>    int @ 1: int32,</code><br><code>  required_int64:</code><br><code>    int @ 2: int64,</code><br><code>  required_uint32:</code><br><code>    uint @ 3: uint32,</code><br><code>  required_uint64:</code><br><code>    uint @ 4: uint64,</code><br><code>  required_sint32:</code><br><code>    int @ 5: int32,</code><br><code>  required_sint64:</code><br><code>    int @ 6: int64,</code><br><code>  required_fixed32:</code><br><code>    uint @ 7: fixed32,</code><br><code>  required_fixed64:</code><br><code>    uint @ 8: uint64,</code><br><code>  required_sfixed32:</code><br><code>    int @ 9: int32,</code><br><code>  required_sfixed64:</code><br><code>    int @ 10: int64,</code><br><code>  required_float:</code><br><code>    float @ 11: float,</code><br><code>  required_double:</code><br><code>    float @ 12: double,</code><br><code>  required_string:</code><br><code>    string @ 13:  ring,</code><br><code>  required_bytes:</code><br><code>    bytes @ 14: bytes,</code><br><code>  required_bool:</code><br><code>    bool @ 15: bool</code></td></tr>
<tr><td> <b>Group Types</b> </td><td>  </td></tr>
<tr><td><code>  optional group Group = 1 {</code><br><br><code>    required  bool required_bool  =  2;</code><br><br><code>  }</code></td><td><code>  type Group = {</code><br><code>    required_bool:</code><br><code>      bool @ 2: bool</code><br><code>  },</code><br><br><code>  group:</code><br><code>    Group @ 1</code></td></tr>
<tr><td> <b>Message Types</b> </td><td>  </td></tr>
<tr><td><code>message Message {</code><br><code>  message Nested {</code><br><code>  }</code><br><br><code>  optional Nested optional_nested = 1;</code><br><br><code>};</code><br><br><code>message Type {</code><br><code>  required Message  required_message = 1;</code><br><br><code>};</code> </td><td><code>type Message = parsedmessage {</code><br><code>  type Nested = {</code><br><code>  },</code><br><br><code>  optional_nested:</code><br><code>    Message.Nested @ 1,</code><br><code>};</code><br><br><code>type Type = parsedmessage {</code><br><code>  required_message:</code><br><code>  Message @ 1,</code><br><code>};</code> </td></tr>
<tr><td> <b>Enums</b> </td><td>  </td></tr>
<tr><td><code>message Foo {</code><br><br><code>  enum Enum {</code><br><code>    ONE   = 1;  </code><br><code>    TWO   = 2;</code><br><code>    THREE = 3;</code><br><code>  }</code><br><br><br><br><code>  required Enum required_enum = 1;</code><br><br><code>  optional Enum optional_enum = 2 [default = ONE];</code><br><br><code>  repeated Enum repeated_enum = 3;</code><br><br><code>};</code> </td><td><code>type Foo = parsedmessage {</code><br><code>  type Enum = int,</code><br><code>  static ONE: Enum = 1,</code><br><code>  static TWO: Enum = 2,</code><br><code>  static THREE: Enum = 3,</code><br><br><code>  static Enum_names: map[enum_value: int]</code><br><code>    of enum_name: string = {</code><br><code>    3: "THREE",</code><br><code>    2: "TWO",</code><br><code>    1: "ONE",</code><br><code>  },</code><br><code>  required_enum:</code><br><code>    Enum @ 1: int32,</code><br><code>  optional_enum:</code><br><code>    Enum = 1 @ 2: int32</code><br><code>  repeated_enum: array of</code><br><code>    Enum @ 3: int32</code><br><code>};</code></td></tr>
<tr><td><code>message Bar {</code><br><code>  required Foo.Enum required_enum = 1;</code><br><br><code>  optional Foo.Enum optional_enum = 2 [default = ONE];</code><br><br><code>  repeated Foo.Enum repeated_enum = 3;</code><br><br><code>};</code></td><td><code>type Bar = parsedmessage {  required_enum:</code><br><code>    Foo.Enum @ 1: int32,</code><br><code>  optional_enum:</code><br><code>    Foo.Enum = 1 @ 2: int32,</code><br><code>  repeated_enum: array of</code><br><code>    Foo.Enum @ 3: int32</code><br><code>};</code></td></tr>