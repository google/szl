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

// This file contains code for handling the 'reporting' flags.  These
// are flags that, when present, cause the program to report some
// information and then exit.  --help and --version are the canonical
// reporting flags, but we also have flags like --helpxml, etc.
//
// There's only one function that's meant to be called externally:
// HandleCommandLineHelpFlags().
//
// HandleCommandLineHelpFlags() will check what 'reporting' flags have
// been defined, if any -- the "help" part of the function name is a
// bit misleading -- and do the relevant reporting.  It should be
// called after all flag-values have been assigned, that is, after
// parsing the command-line.

// We need PRIu64, which is only defined if we explicitly ask for it
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <string>
#include <vector>

#include "public/porting.h"
#include "public/commandlineflags.h"
#include "public/logging.h"

#include "utilities/strutils.h"


// The 'reporting' flags.  They all call exit().
DEFINE_bool(help, false,
            "show help on all flags [tip: all flags can have two dashes]");
DEFINE_bool(helpxml, false,
            "produce an xml version of help");
DEFINE_bool(version, false,
            "show version and build info and exit");


namespace sawzall {


// --------------------------------------------------------------------
// DescribeOneFlag()
// DescribeOneFlagInXML()
//    Routines that pretty-print info about a flag.  These use
//    a CmdLineFlag, which is the way the commandlineflags
//    API exposes static info about a flag.
// --------------------------------------------------------------------

static const int kLineLength = 80;

static void AddString(const string& s,
                      string* final_string, int* chars_in_line) {
  const int slen = static_cast<int>(s.length());
  if (*chars_in_line + 1 + slen >= kLineLength) {  // < 80 chars/line
    *final_string += "\n      ";
    *chars_in_line = 6;
  } else {
    *final_string += " ";
    *chars_in_line += 1;
  }
  *final_string += s;
  *chars_in_line += slen;
}

// Create a descriptive string for a flag.
// Goes to some trouble to make pretty line breaks.
string DescribeOneFlag(const CmdLineFlag& flag) {
  string main_part = StringPrintf("    -%s (%s)",
                                  flag.name, flag.description);
  const char* c_string = main_part.c_str();
  int chars_left = static_cast<int>(main_part.length());
  string final_string = "";
  int chars_in_line = 0;  // how many chars in current line so far?
  while (1) {
    assert(chars_left == strlen(c_string));  // Unless there's a \0 in there?
    const char* newline = strchr(c_string, '\n');
    if (newline == NULL && chars_in_line+chars_left < kLineLength) {
      // The whole remainder of the string fits on this line
      final_string += c_string;
      chars_in_line += chars_left;
      break;
    }
    if (newline != NULL && newline - c_string < kLineLength - chars_in_line) {
      int n = static_cast<int>(newline - c_string);
      final_string.append(c_string, n);
      chars_left -= n + 1;
      c_string += n + 1;
    } else {
      // Find the last whitespace on this 80-char line
      int whitespace = kLineLength-chars_in_line-1;  // < 80 chars/line
      while ( whitespace > 0 && !isspace(c_string[whitespace]) ) {
        --whitespace;
      }
      if (whitespace <= 0) {
        // Couldn't find any whitespace to make a line break.  Just dump the
        // rest out!
        final_string += c_string;
        chars_in_line = kLineLength;  // next part gets its own line for sure!
        break;
      }
      final_string += string(c_string, whitespace);
      chars_in_line += whitespace;
      while (isspace(c_string[whitespace]))  ++whitespace;
      c_string += whitespace;
      chars_left -= whitespace;
    }
    if (*c_string == '\0')
      break;
    StringAppendF(&final_string, "\n      ");
    chars_in_line = 6;
  }

  // Append data type
  AddString(string("type: ") + flag.type_string, &final_string, &chars_in_line);
  // Append the effective default value (i.e., the value that the flag
  // will have after the command line is parsed if the flag is not
  // specified on the command line).  We assume that the help flag
  // will never be used with other flags, and that no startup code ever
  // modified flag variables, so the value of the variable is the default.
  if (flag.type == CmdLineFlag::BOOL)
    AddString(StringPrintf("default: %s", (*flag.ptr_bool ? "true" : "false")),
              &final_string, &chars_in_line);
  else if (flag.type == CmdLineFlag::INT32)
    AddString(StringPrintf("default: %"PRId32, *flag.ptr_int32),
              &final_string, &chars_in_line);
  else if (flag.type == CmdLineFlag::INT64)
    AddString(StringPrintf("default: %"PRId64, *flag.ptr_int64),
              &final_string, &chars_in_line);
  else if (flag.type == CmdLineFlag::STRING)  // add quotes for strings
    AddString(StringPrintf("default: \"%s\"", flag.ptr_string->c_str()),
              &final_string, &chars_in_line);
  else
    AddString("unrecognized flag??", &final_string, &chars_in_line);

  StringAppendF(&final_string, "\n");
  return final_string;
}

// Simple routine to xml-escape a string: escape & and < only.
static string XMLText(const string& txt) {
  string ans = txt;
  for (string::size_type pos = 0; (pos = ans.find("&", pos)) != string::npos; )
    ans.replace(pos++, 1, "&amp;");
  for (string::size_type pos = 0; (pos = ans.find("<", pos)) != string::npos; )
    ans.replace(pos++, 1, "&lt;");
  return ans;
}

static void AddXMLTag(string* r, const char* tag, const string& txt) {
  StringAppendF(r, "<%s>%s</%s>", tag, XMLText(txt).c_str(), tag);
}


static string DescribeOneFlagInXML(const CmdLineFlag& flag) {
  // The file and flagname could have been attributes, but default
  // and meaning need to avoid attribute normalization.  This way it
  // can be parsed by simple programs, in addition to xml parsers.
  string r("<flag>");
  AddXMLTag(&r, "name", flag.name);
  AddXMLTag(&r, "meaning", flag.description);
  AddXMLTag(&r, "type", flag.type_string);
  if (flag.type == CmdLineFlag::BOOL)
    AddXMLTag(&r, "default", 
              StringPrintf("default: %s", (*flag.ptr_bool ? "true" : "false")));
  else if (flag.type == CmdLineFlag::INT32)
    AddXMLTag(&r, "default",
              StringPrintf("default: %"PRId32, *flag.ptr_int32));
  else if (flag.type == CmdLineFlag::INT64)
    AddXMLTag(&r, "default",
              StringPrintf("default: %"PRId64, *flag.ptr_int64));
  else if (flag.type == CmdLineFlag::STRING)  // add quotes for strings
    AddXMLTag(&r, "default",
              StringPrintf("default: \"%s\"", flag.ptr_string->c_str()));
  r += "</flag>";
  return r;
}

// --------------------------------------------------------------------
// ShowUsageWithFlags()
// ShowXMLOfFlags()
//    These routines variously expose the registry's list of flag
//    values.  ShowUsage*() prints the flag-value information
//    to stdout in a user-readable format (that's what --help uses).
//    ShowXMLOfFlags() prints the flag-value information to stdout
//    in a machine-readable format.  In all cases, the flags are
//    sorted: first by filename they are defined in, then by flagname.
// --------------------------------------------------------------------

// Show help.
static void ShowUsageWithFlags(const char *progname) {
  fprintf(stdout, "%s\n", progname);

  vector<CmdLineFlag>& flags = *CmdLineFlag::flags;

  for (int i = 0; i < flags.size(); i++)
      fprintf(stdout, "%s", DescribeOneFlag(flags[i]).c_str());
}

// Convert the help, program, and usage to xml.
static void ShowXMLOfFlags(const char *prog_name) {
  vector<CmdLineFlag>& flags = *CmdLineFlag::flags;

  // XML.  There is no corresponding schema yet
  fprintf(stdout, "<?xml version=\"1.0\"?>\n");
  // The document
  fprintf(stdout, "<AllFlags>\n");
  // the program name and usage
  fprintf(stdout, "<program>%s</program>\n", XMLText(prog_name).c_str());
  // All the flags
  for (int i = 0; i < flags.size(); i++)
    fprintf(stdout, "%s\n", DescribeOneFlagInXML(flags[i]).c_str());
  // The end of the document
  fprintf(stdout, "</AllFlags>\n");
}


// --------------------------------------------------------------------
// ShowVersion()
//    Called upon --version.
// --------------------------------------------------------------------

static void ShowVersion(const char* progname) {
  fprintf(stdout, "%s\n", progname);
# if !defined(NDEBUG)
  fprintf(stdout, "Debug build\n");
# endif
}


// --------------------------------------------------------------------
// HandleCommandLineHelpFlags()
//    Checks all the 'reporting' commandline flags to see if any
//    have been set.  If so, handles them appropriately.  Note
//    that all of them, by definition, cause the program to exit
//    if they trigger.
// --------------------------------------------------------------------

void HandleCommandLineHelpFlags(const char* argv0) {
  const char* sep = strrchr(argv0, '/');
  const char* progname =  sep ? sep + 1 : argv0;

  if (FLAGS_help) {
    // show all options
    ShowUsageWithFlags(progname);
    exit(1);

  } else if (FLAGS_helpxml) {
    ShowXMLOfFlags(progname);
    exit(1);

  } else if (FLAGS_version) {
    ShowVersion(progname);
    // Unlike help, we may be asking for version in a script, so return 0
    exit(0);
  }
}


}  // namespace sawzall
