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

// Copied from the internals of the protocol compiler.
// These ought to be made available to plugins.


namespace google {
namespace protobuf {
namespace compiler {
namespace szl {


// The obvious conversions.

string SimpleItoa(int i);
string SimpleItoa(unsigned int i);
string SimpleItoa(long i);
string SimpleItoa(unsigned long i);
string SimpleItoa(long long i);
string SimpleItoa(unsigned long long i);
string SimpleFtoa(float f);
string SimpleDtoa(double d);


// Returns a string with chararacters escaped where C would need them escaped.

string CEscape(const string&);


// Returns a string with [A-Z] replaced with [a-z].
// Only used for identifiers, so non-ASCII upper case characters are ignored.

inline void LowerString(string* s) {
  string::iterator end = s->end();
  for (string::iterator i = s->begin(); i != end; ++i) {
    // tolower() changes based on locale.  We don't want this!
    if ('A' <= *i && *i <= 'Z') *i += 'a' - 'A';
  }
}


// As described by their names.

inline bool HasPrefixString(const string& str, const string& prefix) {
  return str.size() >= prefix.size() &&
         str.compare(0, prefix.size(), prefix) == 0;
}

inline string StripPrefixString(const string& str, const string& prefix) {
  if (HasPrefixString(str, prefix)) {
    return str.substr(prefix.size());
  } else {
    return str;
  }
}

inline bool HasSuffixString(const string& str, const string& suffix) {
  return str.size() >= suffix.size() &&
         str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline string StripSuffixString(const string& str, const string& suffix) {
  if (HasSuffixString(str, suffix)) {
    return str.substr(0, str.size() - suffix.size());
  } else {
    return str;
  }
}


}  // namespace szl
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
