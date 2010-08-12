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

#include <ctype.h>  // for isspace()

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <vector>

#include "engine/globals.h"
#include "public/commandlineflags.h"
#include "public/logging.h"

#include "utilities/sysutils.h"
#include "utilities/strutils.h"
#include "utilities/timeutils.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/map.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/symboltable.h"
#include "engine/scanner.h"
#include "engine/proc.h"
#include "engine/error.h"


DEFINE_string(szl_includepath,
              "",
              "Comma-separated list of directories in which to search for "
              "include files if they are not found in the directory of the "
              "including file, and for program files if they are not found in "
              "the current directory.");

DEFINE_string(protocol_compiler,
              "/usr/local/bin/protoc",
              "file name of protocol-compiler binary");

DEFINE_string(protocol_compiler_plugin,
              "/usr/local/bin/protoc_gen_szl",
              "file name of protocol-compiler szl plugin binary");

DEFINE_string(protocol_compiler_temp,
              "/tmp",
              "temporary directory for protocol compiler output");


namespace sawzall {

// currently we do not report more then one error per line to
// reduce the number of spurious errors caused by a previous
// error - however, sometimes that masks a real problem and
// for debugging purposes it is useful to see all error msgs
DEFINE_bool(report_all_errors, false, "report all errors, even if on the same line");

const char* FileContents(Proc* proc, const char* name, string* contents);

// Returns a command that will run protocol-compiler.
// The return value is a freshly-malloced string, which the caller must free.
static string ProtocolCompilerCommand(szl_string file_name,
                                      const char* source_dir) {
  vector<string> parts;
  SplitStringAtCommas(FLAGS_szl_includepath, &parts);

  string command = FLAGS_protocol_compiler +
                   " --plugin=" + FLAGS_protocol_compiler_plugin +
                   " --szl_out=" + FLAGS_protocol_compiler_temp;
  if (source_dir != NULL) {
    command.append(" --proto_path=");
    command.append(source_dir);
  }
  for (int i = 0; i < parts.size(); i++) {
    if (!parts[i].empty()) {
      command.append(" --proto_path=");
      command.append(parts[i]);
    }
  }
  command.append(" ");
  command.append(file_name);
  return command;
}


// Look up a file name and make sure it exists.
// If it starts with /, must exist there.
// If it's in the current directory of the source, look there.
//   (If source_dir is NULL, check the current directory of the process.)
// If not, but it starts ./, return the original string and let caller complain
// If not, see if it can be found in an include directory.
// If not, return NULL
const char* Scanner::FindIncludeFile(Proc* proc, const char* file_name,
                                     const char* source_dir) {
  // rooted path must exist
  if (file_name[0] == '/')
    return file_name;

  // see if file exists in current directory
  const char* tmp;
  if (source_dir != NULL)
    tmp = proc->PrintString("%s/%s", source_dir, file_name);
  else
    tmp = file_name;
  if (access(tmp, R_OK) == 0)  // yes
    return tmp;

  // if it mentions ./ explicitly, don't use the path
  if (strncmp(file_name, "./", 2) == 0)
    return file_name;

  // see if it exists in supplied directory
  vector<string> parts;
  SplitStringAtCommas(FLAGS_szl_includepath, &parts);
  for (int i = 0; i < parts.size(); i++) {
    if (!parts[i].empty()) {
      tmp = proc->PrintString("%s/%s", parts[i].c_str(), file_name);
      if (access(tmp, R_OK) == 0)
        return tmp;
    }
  }
  return NULL;
}


// ------------------------------------------------------------------------------
// Support for keywords

// An alphabetically sorted array of Keyword entries
// mapping each keyword to its corresponding symbol.
// Note: The identifiers must appear in sorted order!

#define DEF(keyword, symbol) { keyword, "'" keyword "'", symbol},
struct Keyword {
  const char* ident_;
  const char* quoted_ident_;  // ident_ enclosed in '
  Symbol sym_;
};

static struct Keyword keywords_[] = {
  DEF("all", ALL)
  DEF("and", AND)
  DEF("array", ARRAY)
  DEF("break", BREAK)
  DEF("case", CASE)
  DEF("continue", CONTINUE)
  DEF("default", DEFAULT)
  DEF("do", DO)
  DEF("each", EACH)

  DEF("else", ELSE)
  DEF("emit", EMIT)
  DEF("file", FILE_)
  DEF("for", FOR)
  DEF("format", FORMAT)
  DEF("function", FUNCTION)
  DEF("if", IF)
  DEF("include", INCLUDE)
  DEF("job", JOB)  // SuperSawzall
  DEF("keyby", KEYBY)  // SuperSawzall
  DEF("map", MAP)
  DEF("merge", MERGE)  // SuperSawzall
  DEF("mill", MILL)  // reserved for future use

  DEF("millmerge", MILLMERGE)  // reserved for future use
  DEF("not", NOT)
  DEF("of", OF)
  DEF("or", OR)
  DEF("parsedmessage", PARSEDMESSAGE)
  DEF("pipeline", PIPELINE)  // SuperSawzall
  DEF("proc", PROC)
  DEF("proto", PROTO)
  DEF("rest", REST)
  DEF("return", RETURN)
  DEF("skip", SKIP)

  DEF("some", SOME)
  DEF("static", STATIC)
  DEF("submatch", SUBMATCH)
  DEF("switch", SWITCH)
  DEF("table", TABLE)
  DEF("type", TYPE)
  DEF("weight", WEIGHT)
  DEF("when", WHEN)
  DEF("while", WHILE)
};
#undef DEF


// Returns true if the keywords_ array is sorted,
// returns false otherwise. Used for debug assertion
// only.
#ifndef NDEBUG  // avoid gcc complaints in optimized mode
static bool KeywordsAreSorted() {
  const int n = ARRAYSIZE(keywords_);
  for (int i = 1; i < n; i++)
    if (strcmp(keywords_[i-1].ident_, keywords_[i].ident_) >= 0)
      return false;
  for (int i = 0; i < n; i++)
    if (keywords_[i].sym_ != FIRST_KEYWORD + i)
      return false;
  return true;
}
#endif


// Lookup ident and return the corresponding
// symbol. Uses E.W. Dijkstra's binary search
// from the book  "Methodik des Programmierens".
static Symbol LookupSymbol(const char* ident) {
  const int n = ARRAYSIZE(keywords_);
  assert(n > 0);
  int low = 0;
  int high = n;
  while (low + 1 != high) {
    int mid = (low + high) / 2;  // low < mid < high
    if (strcmp(ident, keywords_[mid].ident_) < 0)  // ident < keywords_[mid]
      high = mid;
    else  // keywords_[mid] <= ident
      low = mid;
  }
  // low + 1 == high
  if (strcmp(ident, keywords_[low].ident_) == 0)
    return keywords_[low].sym_;
  else
    return IDENT;
}


// Lookup sym and return the corresponding quoted
// keyword string. Returns NULL if not found.
static const char* LookupKeyword(Symbol sym) {
  if (sym >= FIRST_KEYWORD && sym <= LAST_KEYWORD)
    return keywords_[sym-FIRST_KEYWORD].quoted_ident_;
  else
    return NULL;  // not found
}


// ------------------------------------------------------------------------------
// Symbols

bool IsKeyword(const char* ident) {
  return LookupSymbol(ident) != IDENT;
}


const char* Symbol2String(Symbol sym) {
  switch (sym) {
    // errors
    case SCANEOF:
      return "EOF";
    case ILLEGAL:
      return "illegal symbol";

    // literals
    case BYTES:
      return "bytes literal";
    case CHAR:
      return "char literal";
    case INT:
      return "int literal";
    case FINGERPRINT:
      return "fingerprint literal";
    case TIME:
      return "time literal";
    case FLOAT:
      return "float literal";
    case STRING:
      return "string literal";
    case UINT:
      return "uint literal";
    case IDENT:
      return "identifier";

    // special char sequences
    case PLUS:
      return "'+'";
    case MINUS:
      return "'-'";
    case TIMES:
      return "'*'";
    case DIV:
      return "'/'";
    case MOD:
      return "'%'";
    case BITAND:
      return "'&'";
    case BITOR:
      return "'|'";
    case BITXOR:
      return "'^'";
    case SHL:
      return "'<<'";
    case SHR:
      return "'>>'";
    case EQL:
      return "'=='";
    case NEQ:
      return "'!='";
    case LSS:
      return "'<'";
    case LEQ:
      return "'<='";
    case GTR:
      return "'>'";
    case GEQ:
      return "'>='";
    case AT:
      return "'@'";
    case LPAREN:
      return "'('";
    case RPAREN:
      return "')'";
    case LBRACK:
      return "'['";
    case RBRACK:
      return "']'";
    case LBRACE:
      return "'{'";
    case RBRACE:
      return "'}'";
    case CONDAND:
      return "'&&'";
    case CONDOR:
      return "'||'";
    case BITNOT:
      return "'~'";
    case NOT:
      return "'not' or '!'";
    case PERIOD:
      return "'.'";
    case COMMA:
      return "','";
    case SEMICOLON:
      return "';'";
    case COLON:
      return "':'";
    case ASSIGN:
      return "'='";
    case LARROW:
      return "'<-'";
    case RARROW:
      return "'->'";
    case DOLLAR:
      return "'$'";
    case QUERY:
      return "'?'";
    case INC:
      return "'++'";
    case DEC:
      return "'--'";

    // keywords and unknown symbols
    default:
      { const char* keyword = LookupKeyword(sym);
        if (keyword != NULL)
          return keyword;
      }
      return "<unknown symbol>";
  }
  ShouldNotReachHere();
  return NULL;
}


// ------------------------------------------------------------------------------
// Implementation of Source

Source::Source(const SourceFile* files, int num_files, const char* src) {
  assert(num_files > 0);
  // Make a copy of the sources files array; array may be a local variable
  // in the caller.  We do require the actual strings to outlast the caller,
  // but that's a less onerous restriction.
  num_files_ = num_files;
  files_ = new SourceFile[num_files];  // explicitly deallocated
  memmove(const_cast<SourceFile*>(files_), files, num_files * sizeof files[0]);
  file_num_ = 0;
  src_ = NULL;
  file_name_ = NULL;
  scanner_ = NULL;
  error_count_ = 0;
  if (src != NULL) {
    file_ = NULL;
    src_ = src;
  } else {
    OpenNextFile();
  }
  line_ = 1;
  nbytes_ = 0;
}


Source::~Source() {
  if (file_ != NULL) {
    fclose(file_);
  }
  delete[] files_;
}


void Source::OpenNextFile() {
  struct stat s;
  file_name_ = NULL;
  file_ = fopen(file_name(), "r");
  if (file_ == NULL) {
    F.fprint(2, "could not open '%s': %r\n", file_name());
  } else if (fstat(fileno(file_), &s) == 0 && S_ISDIR(s.st_mode)) {
    F.fprint(2, "'%s': is a directory\n", file_name());
    fclose(file_);
    file_ = NULL;
  }
  if (file_ == NULL) {
    // fopen failed or file is a directory => continue
    // with /dev/null to get to an EOF immediately
    error_count_++;
    file_ = fopen("/dev/null",  "r");
    CHECK(file_ != NULL) << ": couldn't open /dev/null";
  }
  if (scanner_ != NULL)
    scanner_->RegisterFile(file_name());
  line_ = 1;
}


int Source::FileGetc() {
  assert(file_ != NULL);
  int c = fgetc(file_);
  if (c == EOF) {
    // Advance to next file, if possible
    if (file_num_ < num_files_ - 1) {
      fclose(file_);
      file_num_++;
      OpenNextFile();
      c = fgetc(file_);
    }
  }
  return c;
}


int Source::ReadByte() {
  if (src_ == NULL)
    return FileGetc();

  if (*src_ != '\0')
    return *src_++;
  return EOF;
}


void Source::UnloadBytes(int i) {
  int nleft = nbytes_ - i;
  assert(nleft >= 0);
  for (int j = 0; j < nleft; j++)
    bytes_[j] = bytes_[i++];
  nbytes_ = nleft;
}


int Source::ReadChar() {
  int c;
  if (nbytes_ == 0) {
    // usual case: nothing saved, one byte does it; easy out
    c = ReadByte();
    if (c == EOF || c < Runeself)
      return c;
    bytes_[nbytes_++] = c;
  }
  // rare case: we are in a multi-byte sequence
  while (!fullrune(bytes_, nbytes_)) {
    c = ReadByte();
    if (c == EOF)
      return Runeerror;
    bytes_[nbytes_++] = c;
    if (nbytes_ > UTFmax) {
      UnloadBytes(1);  // skip one byte; hope to recover
      return Runeerror;
    }
  }
  Rune r;
  UnloadBytes(chartorune(&r, bytes_));
  return r;
}


int Source::NextChar() {
  ch_ = ReadChar();
  // count lines
  if (ch_ == '\n')
    line_++;
  return ch_;
}


void Source::SetFileLine(const char* file, int line) {
  assert(file == NULL || strlen(file) > 0);
  assert(line > 0);
  if (file != NULL)
    file_name_ = file;
  line_ = line;
}


// ------------------------------------------------------------------------------
// Implementation of Scanner

void Scanner::negate_int_value() {
  if ((unsigned long long)int_value_ == 1ULL<<63)
    Error("overflow making '%lld' positive for subtraction", int_value());
  int_value_ = -int_value_;
}


void Scanner::negate_float_value() {
  float_value_ = -float_value_;
}


// prettyprint source line
int Scanner::SzlFileLineFmt(Fmt::State* f) {
  FileLine* fl = FMT_ARG(f, FileLine*);
  return F.fmtprint(f, "%s:%d", fl->file(), fl->line());
}


// prettyprint source line traceback
int Scanner::SzlSourceLineFmt(Fmt::State* f) {
  Scanner* s = FMT_ARG(f, Scanner*);
  int n = 0;
  for (int i = 0; i < s->include_level_; i++) {
    n += F.fmtprint(f, "%s:%d: ", s->states_[i]->file_name(), s->states_[i]->line());
  }
  n += F.fmtprint(f, "%s:%d:", s->file_name(), s->line());
  return n;
}


// print a symbol
int Scanner::SymbolFmt(Fmt::State* f) {
  Symbol s = (Symbol)FMT_ARG(f, int);
  return Fmt::fmtstrcpy(f, Symbol2String(s));
}


// Only dependable if sym is most recently seen.
const char* Scanner::PrintSymbol(Symbol sym) {
  switch (sym) {
    case ILLEGAL:
      return proc_->PrintString("illegal char '%C' (0x%x)", illegal_value(), illegal_value());
    case CHAR:
      return proc_->PrintString("%k", int_value());
    case INT:
      return proc_->PrintString("'%d'", int_value());
    case FINGERPRINT:
      return proc_->PrintString("'0x%.16llxP'", int_value());
    case TIME:
      return proc_->PrintString("'%lluT'", int_value());
    case UINT:
      return proc_->PrintString("'%uU'", int_value());
    case STRING:
      return proc_->PrintString("%q", string_value());
    case FLOAT:
      return proc_->PrintString("'%s'", string_value());
    case IDENT:
      return proc_->PrintString("'%s'", string_value());
    case BYTES:
      // Likely to give poor results, so fall through
    default:
      return proc_->PrintString("%s", Symbol2String(sym));
  }
}


// helper functions

static bool is_letter(int ch) {
  return ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z') || ch == '_';
}


static bool is_digit(int ch) {
  return '0' <= ch && ch <= '9';
}


static int digit_val(int ch) {
  if ('0' <= ch && ch <= '9')
    return ch - '0';
  if ('a' <= ch && ch <= 'f')
    return ch - 'a' + 10;
  if ('A' <= ch && ch <= 'F')
    return ch - 'A' + 10;
  return 16;  // larger than any digit in any legal base
}


Scanner::Scanner(Proc* proc, Source* source)
  : source_(proc) {
#ifndef NDEBUG
  assert(KeywordsAreSorted());
#endif
  assert(source != NULL);
  proc_ = proc;
  include_level_ = 0;
  current_ = source;
  current_offset_ = 0;
  line_ = 1;
  offset_ = 0;
  allocated_len_ = 0;
  string_value_ = NULL;
  last_error_line_ = -1;  // reset to enable next error message
  current_->SetScanner(this);
  // but the first file is already open:
  RegisterFile(current_->file_name());
  next();  // sets ch_
}


Scanner::~Scanner() {
  while (include_level_ > 0)
    CloseInclude();
  delete[] string_value_;
}


void Scanner::Error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Errorv(false, fmt, &args);
  va_end(args);
}


void Scanner::Errorv(bool is_warning, const char* fmt, va_list* args) {
  // only report and count an error if it's on a different line
  if (FLAGS_report_all_errors || last_error_line_ != line()) {
    proc_->error()->Reportv(this, is_warning, fmt, args);
    if (!is_warning)
      last_error_line_ = line();
  }
}


int Scanner::error_count() const {
  return proc_->error()->count();
}


void Scanner::RegisterFile(const char* fname) {
  // The initial newline is a sentinel for searching backwards
  AddSourceString("\n#line ");
  AddSourceString(fname);
  AddSourceString(":1\n");
}


void Scanner::AddSourceChar(Rune ch) {
  current_offset_ = source_.length();
  char buf[UTFmax];
  int len = runetochar(buf, &ch);
  for (int i = 0; i < len; i++)
    source_.Append(buf[i]);
}


void Scanner::AddSourceString(const char* s) {
  while (*s != '\0')
    source_.Append(*s++);
}


void Scanner::EraseToEnd(int pos) {
  while (pos < source_.length())
    source_[pos++] = ' ';
}


void Scanner::EnsureStringSpace(int n) {
  if (string_len_ + n > allocated_len_) {
    // possibly not enough space left => grow
    // string_value_ (via amortized doubling) -
    char* old = string_value_;
    allocated_len_ *= 2;
    // note: one could get rid of this 'if' by
    // allocating a string_value in the constructor
    if (string_len_ + n > allocated_len_)
      allocated_len_ = string_len_ + n;
    string_value_ = new char[allocated_len_];  // explicitly deallocated
    memmove(string_value_, old, string_len_);
    delete[] old;
  }
  assert(string_len_ + n <= allocated_len_);
}


void Scanner::AddStringChar(Rune ch) {
  EnsureStringSpace(UTFmax);
  string_len_ += runetochar(&string_value_[string_len_], &ch);
}


void Scanner::AddBytesChar(int ch) {
  if (ch > 0xFF) {
    Error("character %k (\\u%.4x) is out of range for bytes literal", ch, ch);
    return;
  }
  EnsureStringSpace(1);
  string_value_[string_len_++] = ch;
}


void Scanner::next() {
  ch_ = current_->NextChar();
  // collect all chars so we have the original source code
  // (used for logging/security of all Sawzall programs)
  if (ch_ != EOF)
    AddSourceChar(ch_);
}


void Scanner::ScanLineDirective() {
  ScanIdent();  // always succeeds
  if (strcmp(string_value(), "line") != 0 || ch_ != ' ')
    return;
  next();  // consume ' '

  // parse an optional file name terminated by a colon
  StartString();
  while (ch_ != ':') {
    if (ch_ == '\n' || ch_ == EOF)
      return;
    // file names do not contain unicode, no need to call ScanUnicode()
    AddStringChar(ch_);
    next();
  }
  TerminateString();  // empty string if no file name provided
  const char* file = NULL;
  if (*string_value() != '\0')
    file = proc_->CopyString(string_value());
  next(); // consume ':'

  // parse line number
  int line = 0;
  while (isdigit(ch_)) {
    line = line * 10 + (ch_ - '0');
    if (line < 0)
      return;  // overflow
    next();
  }
  if (line == 0)
    return;  // either no digit found or 0 parsed

  // there must be nothing else after the directive
  // (note that we could also see EOF in which case
  // we probably don't want to accept the directive
  // since this file may have been included and the
  // directive would affect the file containing the
  // include)
  if (ch_ != '\n')
    return;

  // successfully parsed directive
  current_->SetFileLine(file, line);
}


void Scanner::SkipWhitespaceAndComments() {
  while (isspace(ch_) || ch_ == '#') {
    while (isspace(ch_))  // skip whitespace
      next();
    if (ch_ == '#') {  // skip comment
      next();  // consume '#'
      if (ch_ == 'l')  // look for #line directive
        ScanLineDirective();
      // skip rest of comment
      while (ch_ != '\n' && ch_ != EOF)
        next();
    }
  }
}


Rune Scanner::ScanEscape(int base, int num_digits, bool exact_count) {
  szl_int x = 0;
  int n = 0;  // number of correct digits found
  for (int d; n < num_digits && (d = digit_val(ch_)) < base; n++) {
    x = x * base + d;
    if (x > Runemax) {
      // only one error is printed per line, so this won't be noisy
      Error("unicode value too large (>0x%x) in character escape", Runemax);
    }
    next();
  }
  if (n == 0)  // this assumes num_digits > 0
    Error("digit in base %d expected; found %k", base, ch_);
  if (n < num_digits && exact_count)
    Error("found %d digits in %d-digit base %d character escape", n, num_digits, base);
  return x;
}


int Scanner::ScanDigits(int base, int max_digit) {
  for (int d; (d = digit_val(ch_)) < base; ) {
    AddStringChar(ch_);
    if (d > max_digit) max_digit = d;
    next();
  }
  if (max_digit < 0)
    Error("digit in base %d expected; found %k", base, ch_);
  return max_digit;
}


// assemble the characters for a number; convert the
// result after assembly.
Symbol Scanner::ScanNumber(bool negative, bool seen_period) {
  assert(is_digit(ch_));  // the first digit of the number or the fraction

  StartString();  // collect number chars as string

  int max_digit = -1;  // highest digit value seen so far
  if (seen_period) {
    // we have already seen a decimal point of the float
    AddStringChar('.');  // add it to the float string

  } else {
    // int or float
    int base = 10;
    int offset = 0;  // size of base-determining prefix, e.g. 2 for 0x.
    if (ch_ == '0') {
      // possibly a base specifier - look at next char
      AddStringChar(ch_);
      next();
      if (ch_ == 'b' || ch_ == 'B') {
        AddStringChar(ch_);
        next();
        base = 2;
        offset = 2;
      } else if ( ch_ == 'x' || ch_ == 'X') {
        AddStringChar(ch_);
        next();
        base = 16;
        offset = 2;
      } else {
        base = 8;
        max_digit = 0;
        offset = 1;
      }
    }

    // if first digit was 0, accept decimal for cases like 09.2e4
    max_digit = ScanDigits((base == 8 ? 10 : base), max_digit);

    if (base == 8 || base == 10) {
      // floating point is acceptable
      if (ch_ == '.' || ch_ == 'e' || ch_ == 'E') {
        // we have a float - consume '.' if any
        if (ch_ == '.') {
          AddStringChar(ch_);
          next();
          seen_period = true;
        }
        base = 10;
      } else if (max_digit >= base) {
        // we have an int, make sure octals are in range
        Error("illegal digit %d in octal literal", max_digit);
      }
    }
    if (!seen_period && (base != 10 || (ch_ != 'e' && ch_ != 'E'))) {
      TerminateString();
      const char* digits = string_value() + offset;
      errno = 0;  // for error handling below
      if (ch_ == 'P' || ch_ == 'p') {
        // fingerprint literal
        int_value_ = strtoull(digits, NULL, base);
        if (errno == ERANGE)
          Error("overflow in fingerprint literal %s%c", string_value(), ch_);
        if (negative)
          Error("fingerprint literal %s%c must be positive",
                string_value(), ch_);
        next();
        return FINGERPRINT;
      } else if (ch_ == 'T' || ch_ == 't') {
        // time literal
        int_value_ = strtoull(digits, NULL, base);
        if (errno == ERANGE)
          Error("overflow in time literal %s%c", string_value(), ch_);
        if (negative)
          Error("time literal %s%c must be positive", string_value(), ch_);
        next();
        return TIME;
      } else if (ch_ == 'U' || ch_ == 'u') {
        // uint literal
        int_value_ = strtoull(digits, NULL, base);
        if (errno == ERANGE)
          Error("overflow in uint literal %s%c", string_value(), ch_);
        if (negative)
          Error("uint literal %s%c must be positive", string_value(), ch_);
        next();
        return UINT;
      } else {
        // Integer literal.
        // The other integer cases must *not* have a '-' because they convert unsigned.
        // Converting and then negating causes overflow for largest negative integer,
        // so we put it back explicitly.  The 0x or 0b prefix is already processed.
        // This feels like too much work to squeeze out one special int, but so be it.
        if (negative) {
          // careful: -2^63 is representable but +2^63 is not, so cannot use strtoll then negate
          int_value_ = - strtoull(digits, NULL, base);  // unsigned!
          // values in [0,2^63) behave in the obvious way; result is non-positive and in range
          // 2^63 becomes -2^63 which is correct, non-positive and in range
          // values in (2^63,2^64) become positive and out of range
          // (negation maps them to unsigned (0,2^63); conversion to signed has no effect)
          if (errno == 0 && int_value_ > 0)
            errno = ERANGE;
        } else {
           int_value_ = strtoll(digits, NULL, base);
        }
       if (errno == ERANGE)
          Error("overflow in integer literal %s", string_value());
        return INT;
      }
    }
  }

  // we have a float and the decimal point, if any, has been absorbed.
  // absorb fractional part, if any
  ScanDigits(10, max_digit);
  if (ch_ == 'e' || ch_ == 'E') {
    AddStringChar(ch_);
    next();
    if (ch_ == '+' || ch_ == '-') {
      // scan sign
      AddStringChar(ch_);
      next();
    }
    ScanDigits(10, -1);  // minus sign (if any) still in the token
  }
  TerminateString();
  errno = 0;
  float_value_ = strtod(string_value(), NULL);
  // Catch underflow (ERANGE and value is zero) and overflow (ERANGE and
  // value is large) but ignore partial underflow (ERANGE and value is small).
  // Work around apparent bug in Ubuntu strtod - returns EDOM instead of ERANGE
  // in the underflow range [1.0e-324, 2.470e-324]
  if ((errno == ERANGE || errno == EDOM) &&
      (float_value_ == 0.0 || float_value_ > 1.0))
    Error("%serflow in floating-point literal %s",
      (float_value_ == 0.0)? "und" : "ov", string_value());
  else if (negative)
    float_value_ = -float_value_;
  return FLOAT;
}


void Scanner::ScanIdent() {
  assert(is_letter(ch_));
  StartString();
  do {
    AddStringChar(ch_);
    next();
  } while (is_letter(ch_) || is_digit(ch_));
  TerminateString();
}


// Keep in sync with utils/IsValidUnicode.
// This one is always called with a Rune value, never a szl_int.
Rune Scanner::ValidUnicode(Rune r) {
  if (r <= 0 || r > Runemax) {
    Error("unicode value 0x%x out of range", r);
    r = '?';  // avoid encoding trouble
  }
  if (0xD800 <= r && r <= 0xDFFF) {
    Error("unicode value 0x%x is a surrogate code point", r);
    r = '?';  // avoid encoding trouble
  }
  return r;
}


// Checks bytes but always operates on values from ScanEscape, which
// returns Runes.
Rune Scanner::ValidByte(Rune b) {
  if (b < 0 || b > 0xff) {
    Error("byte value 0x%x out of range", b);
    b = '?';  // avoid encoding trouble
  }
  return b;
}


Rune Scanner::ScanUnicode() {
  if (ch_ == '\\') {
    next();
    switch (ch_) {
      case 'a':
        next();
        return '\a';
      case 'b':
        next();
        return '\b';
      case 'f':
        next();
        return '\f';
      case 'n':
        next();
        return '\n';
      case 'r':
        next();
        return '\r';
      case 't':
        next();
        return '\t';
      case 'u':
        // hexadecimal escape - 4 digits
        next();
        return ValidUnicode(ScanEscape(16, 4, true));
      case 'U':
        // hexadecimal escape - 8 digits
        next();
        return ValidUnicode(ScanEscape(16, 8, true));
      case 'v':
        next();
        return '\v';
      case '0': case '1': case '2': case '3':
      case '4': case '5': case '6': case '7':
        // octal escape
        return ValidByte(ScanEscape(8, 3, false));
      case 'x':
        // hexadecimal escape
        next();
        return ValidByte(ScanEscape(16, 1000000 /* arbitrarily large */, false));
    }
  }
  // all other cases
  if (ch_ != EOF) {
    Rune ch = ch_;
    next();
    return ch;
  } else {
    // handle gracefully but report error
    Error("string or char not terminated");
    return 0;
  }
}


void Scanner::ScanChar() {
  assert(ch_ == '\'');
  next();
  if (ch_ == '\n' || ch_ == EOF)
    Error("unterminated character constant");
  else if (ch_ == '\'')
    Error("empty character constant");
  else
    int_value_ = ScanUnicode();

  if (ch_ == '\'')
    next();
  else
    Error("expected single quote, found %k", ch_);
}


// Does modify current_ when the terminating quote of the string
// is not followed by another character and EOF is encountered.
void Scanner::ScanString() {
  assert(ch_ == '"' || ch_ == '`');
  Rune quote = ch_;
  next();
  StartString();
  while (ch_ != quote) {
    if (ch_ == '\n' || ch_ == EOF) {
      Error("unterminated string");
      break;
    }
    if (quote == '"') {
      // interpret backslashes
      AddStringChar(ScanUnicode());
    } else {
      AddStringChar(ch_);
      next();
    }
  }
  TerminateString();
  if (ch_ == quote)
    next();
}


void Scanner::ScanByteString() {
  assert(ch_ == '"' || ch_ == '`');
  Rune quote = ch_;
  next();
  StartString();
  while (ch_ != quote) {
    if (ch_ == '\n' || ch_ == EOF) {
      Error("unterminated string");
      break;
    }
    if (quote == '"') {
      // interpret backslashes
      AddBytesChar(ScanUnicode());
    } else {
      AddBytesChar(ch_);
      next();
    }
  }
  if (ch_ == quote)
    next();
}


int Scanner::HexChar(int quote) {
  int value = digit_val(ch_);
  if (value < 16) {
    // valid char
    next();
  } else {
    // invalid char
    if (ch_ == quote)
      Error("hexadecimal bytes literal needs an even number of digits");
    else
      Error("invalid character %k in hexadecimal bytes literal");
    value = 0;
  }
  return value;
}


void Scanner::ScanHexByteString() {
  assert(ch_ == '"' || ch_ == '`');
  Rune quote = ch_;
  next();
  StartString();
  while (ch_ != quote) {
    if (ch_ == '\n' || ch_ == EOF) {
      Error("unterminated string");
      break;
    }
    // Need exactly two hex characters
    int c1 = HexChar(quote);
    int c2 = HexChar(quote);
    AddBytesChar((c1 << 4) | c2);
  }
  if (ch_ == quote)
    next();
}


void Scanner::ScanTime() {
  assert(ch_ == '"'|| ch_ == '`');
  ScanString();
  szl_time t;
  if (!date2uint64(string_value_, "", &t))
    Error("%q is not a legal time literal", string_value_);
  int_value_ = t;
}


bool Scanner::IsOpenInclude(const char* file_name, int include_level) {
  // If the include level is larger than the current include level, we
  // already closed the file and are back to some file in the including chain
  // and have possibly advanced to a different include branch that is shorter.
  if (include_level > include_level_)
    return false;

  // If the file's include level equals or greater than the current include
  // level, we are either in the current file or its children or we have already
  // closed it and advanced to a different include brach that is at least of the
  // same length. The name of the file at the include level will tell us.
  Source* src_at_include_level = NULL;
  if (include_level == include_level_)
    src_at_include_level = current_;
  else
    src_at_include_level = states_[include_level];
  // TODO: is it safe enough to use just the full file path?
  return (strcmp(src_at_include_level->file_name(), file_name) == 0);
}


// If generated_proto_source is NULL, this is a regular sawzall include file
// that should be read from file_name.
void Scanner::OpenInclude(const char* file_name,
                          const char* generated_proto_source) {
  // indicate include begin in raw source by inserting a line directive
  AddSourceString("\n#line ");
  AddSourceString(file_name);
  if (generated_proto_source != NULL)
    AddSourceString("_generated");
  AddSourceString(":1\n");
  if (proc_->AlreadyIncluded(file_name)) {
    AddSourceString("### ALREADY INCLUDED\n");
    if (ch_ != EOF) {
      // indicate include end in raw source by inserting a line directive,
      // unless including file is completely consumed (ch_ == EOF)
      AddSourceString(proc_->PrintString("\n#line %s:%d\n",
                                         current_->file_name(), current_->line()));
      // skip the newline character of the include line for correct line numbers
      if (ch_ != '\n')
        AddSourceChar(ch_);  // the last char was consumed before the include
    }
    return;
  }
  // switch to include file
  SourceFile include = { file_name, current_->source_dir() };
  Source* s = new Source(&include, 1, generated_proto_source);
  if (s->IsValid()) {
    if (include_level_ >= MAX_INCLUDE_LEVEL) {
      FatalError(
         "too many includes (perhaps due to recursion?), latest is %q",
         file_name);
    }
    states_[include_level_++] = current_;
    current_ = s;  // s will be deallocated in CloseInclude
    next();
  } else {
    delete s;
  }
  last_error_line_ = -1;  // reset to enable next error message
}


void Scanner::CloseInclude() {
  // switch back to including file
  assert(include_level_ > 0);
  delete current_;
  current_ = states_[--include_level_];
  ch_ = current_->LastChar();
  last_error_line_ = -1;  // reset to enable next error message
  if (ch_ != EOF) {
    // indicate include end in raw source by inserting a line directive, unless
    // including file is completely consumed (ch_ == EOF)
    AddSourceString(proc_->PrintString("\n#line %s:%d\n",
                                       current_->file_name(), current_->line()));
    // skip the newline character of the include line for correct line numbers
    if (ch_ != '\n')
      AddSourceChar(ch_);  // the last char was consumed before the include
  }
}


bool Scanner::IncludeFile(const char* incl_file_name) {
  const char* file_name = FindIncludeFile(proc_, incl_file_name,
                                        FileDir(proc_, current_->file_name()));
  if (file_name != NULL) {
    OpenInclude(file_name, NULL);
    return true;
  } else {
    Error("could not find include file %q: %r", incl_file_name);
    AddSourceChar(ch_);  // the last char was consumed before the include
    return false;
  }
}


// Scans the include clause, opens the included file, then advances to its
// first symbol. Returns this first symbol on success and the next symbol of
// the current file on error or in case of ignored multiple inclusion.
Symbol Scanner::ScanInclude() {
  assert(strcmp(string_value(), "include") == 0);
  int pos = source_.length() - 8;  // position of 'i' of 'include' in raw source
  assert(source_[pos] == 'i');
  SkipWhitespaceAndComments();

  bool file_specified = false;
  if (ch_ == '"' || ch_ == '`') {
    ScanString();
    if (*string_value() != '\0') {
      file_specified = true;
      const char* incl_name = proc_->CopyString(string_value());
      const char* file_type = strrchr(incl_name, '.');
      if (file_type != NULL && strcmp(file_type, ".proto") == 0)
        Error("including .proto file - use reserved word \"proto\" instead");
      EraseToEnd(pos);  // remove include from raw source
      IncludeFile(incl_name);
    }
  }

  if (!file_specified)
    Error("include expects a file name");

  return Scan();
}


// Scans the proto clause. If the next symbol is a filename string, uses
// protocol compiler to generate Sawzall code and opens the file, so the next
// call to Scan() can advance to its first symbol. If this is an ignored
// multiple inclusion or an error, the next call to Scan() will advance to the
// next symbol after the clause in the including file. Returns the include
// level of the proto file if opened, and of the current file otherwise.
//
// After the call, string_value() will be the proto filename from the clause
// (if any) and current_file_name() will be the full path of the file at the
// returned include level.
int Scanner::ScanProto() {
  assert(strcmp(string_value(), "proto") == 0);
  int pos = source_.length() - 6;  // position of 'p' of 'proto' in raw source
  assert(source_[pos] == 'p');
  SkipWhitespaceAndComments();

  bool file_specified = false;
  if (ch_ == '"' || ch_ == '`') {
    ScanString();
    if (*string_value() != '\0') {
      file_specified = true;
      const char* proto_name = proc_->CopyString(string_value());
      const char* file_name = FindIncludeFile(proc_, proto_name,
                                       FileDir(proc_, current_->file_name()));
      const char* slash = strchr(file_name, '/');
      const char* basename = (slash != NULL) ? slash+1 : file_name;
      const char* dot = strchr(basename, '.');
      int beforedot = (dot == NULL) ? strlen(basename) : (dot - basename);
      string output_name = FLAGS_protocol_compiler_temp + "/" +
        string(basename, beforedot) + ".szl";
      EraseToEnd(pos);  // remove proto invocation from raw source
      // indicate proto begin in raw source
      AddSourceString("\n### INSTANTIATE PROTO ");
      AddSourceString(proto_name);
      if (file_name != NULL) {
        string command = ProtocolCompilerCommand(file_name,
                                        FileDir(proc_, current_->file_name()));
        // Put the generated source code in a string allocated in this object.
        // It will be live for as long as needed by the Source object, then it may
        // be overwritten by a later include, and it will eventually be deleted
        // when the Scanner is.
        if (include_level_ + 1 >= MAX_INCLUDE_LEVEL) {
          FatalError(
              "too many includes (perhaps due to recursion?), latest is %q",
              file_name);
        }
        string* generated_proto_src =
            &generated_proto_sources_[include_level_ + 1];
        if (!RunCommand(command.c_str(), generated_proto_src)) {
          Error("Error compiling %q", proto_name);
        } else if (!generated_proto_src->empty()) {
          Error("Unexpected stdout from protocol compiler");
        } else {
          const char* error = FileContents(proc_, output_name.c_str(),
                                           generated_proto_src);
          if (error == NULL) {
            AddSourceString(proc_->PrintString("\n### COMMAND: %s",
                                               command.c_str()));
            OpenInclude(file_name, generated_proto_src->c_str());
          }
        }
      } else {
        Error("could not find proto file %q: %r", proto_name);
        AddSourceChar(ch_);  // the last char was consumed before the include
      }
    }
  }

  if (!file_specified)
    Error("proto expects a file name");

  return include_level_;
}


Symbol Scanner::IfNextThenElse(int ch, Symbol then, Symbol else_) {
  next();
  if (ch_ == ch) {
    next();
    return then;
  } else {
    return else_;
  }
}


Symbol Scanner::Scan() {
  last_end_offset_ = current_offset_;
  SkipWhitespaceAndComments();
  // the current file, line number  and offset is the location for the symbol
  file_name_ = current_->file_name();
  line_ = current_->line();
  offset_ = current_offset_;

  // a big switch over all chars
  switch (ch_) {
    // special chars (ASCII sorted)
    case '!':
      return IfNextThenElse('=', NEQ, NOT);
    case '"': case '`':
      ScanString();
      return STRING;
    case '$':
      next();
      return DOLLAR;
    case '%':
      next();
      return MOD;
    case '&':
      return IfNextThenElse('&', CONDAND, BITAND);
    case '\'':
      ScanChar();
      return CHAR;
    case '(':
      next();
      return LPAREN;
    case ')':
      next();
      return RPAREN;
    case '*':
      next();
      return TIMES;
    case '+':
      return IfNextThenElse('+', INC, PLUS);
    case ',':
      next();
      return COMMA;
    case '-':
      next();
      if (proc_->RecognizePipelineKeywords() && ch_ == '>') {
        next();
        return RARROW;
      }
      if (is_digit(ch_))
        return ScanNumber(true, false);
      if (ch_ == '-') {
        next();
        return DEC;
      }
      return MINUS;
    case '.':
      next();
      if (is_digit(ch_))
        return ScanNumber(false, true);
      return PERIOD;
    case '/':
      next();
      return DIV;
    case ':':
      next();
      return COLON;
    case ';':
      next();
      return SEMICOLON;
    case '<':
      next();
      if (ch_ == '-') {
        next();
        return LARROW;
      } else if (ch_ == '=') {
        next();
        return LEQ;
      } else if (ch_ == '<') {
        next();
        return SHL;
      }
      return LSS;
    case '=':
      return IfNextThenElse('=', EQL, ASSIGN);
    case '>':
      next();
      if (ch_ == '=') {
        next();
        return GEQ;
      } else if (ch_ == '>') {
        next();
        return SHR;
      }
      return GTR;
    case '@':
      next();
      return AT;
    case '?':
      next();
      return QUERY;
    case '[':
      next();
      return LBRACK;
    case ']':
      next();
      return RBRACK;
    case '^':
      next();
      return BITXOR;
    case '{':
      next();
      return LBRACE;
    case '|':
      return IfNextThenElse('|', CONDOR , BITOR );
    case '}':
      next();
      return RBRACE;
    case '~':
      next();
      return BITNOT;

    // digits
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      return ScanNumber(false, false);

    // capital letters (don't start keywords)
    case 'A': case 'B': case 'C': case 'D': case 'E':
    case 'F': case 'G': case 'H': case 'I': case 'J':
    case 'K': case 'L': case 'M': case 'N': case 'O':
    case 'P': case 'Q': case 'R': case 'S': case 'T':
    case 'U': case 'V': case 'W': case 'X': case 'Y':
    case 'Z': case '_':
      ScanIdent();
      if (ch_ == '"' || ch_ == '`') {
        if(strcmp(string_value(), "T") == 0) {
          ScanTime();
          return TIME;
        }
        if(strcmp(string_value(), "B") == 0) {
          ScanByteString();
          return BYTES;
        }
        if(strcmp(string_value(), "X") == 0) {
          ScanHexByteString();
          return BYTES;
        }
      }
      return IDENT;

    // lower-case letters (may start keywords)
    case 'a': case 'b': case 'c': case 'd': case 'e':
    case 'f': case 'g': case 'h': case 'i': case 'j':
    case 'k': case 'l': case 'm': case 'n': case 'o':
    case 'p': case 'q': case 'r': case 's': case 't':
    case 'u': case 'v': case 'w': case 'x': case 'y':
    case 'z':
      { ScanIdent();
        Symbol sym = LookupSymbol(string_value());
        switch (sym) {
          case INCLUDE:
            // include clauses are scanned right away, invisible to the parser
            return ScanInclude();
          case PROTO:
            return PROTO;
          case JOB:
          case PIPELINE:
          case MERGE:
          case KEYBY:
            if (!proc_->RecognizePipelineKeywords())
              return IDENT;
            // fall through
          default:
            return sym;
        }
        ShouldNotReachHere();
      }

    case EOF:
      if (include_level_ > 0) {
        CloseInclude();
        return Scan();
      } else {
        return SCANEOF;
      }
  }

  // everything else is illegal. throw it away.
  illegal_value_ = ch_;
  next();  // make some progress
  return ILLEGAL;
}


int Scanner::FirstByteNextSymbol() const {
  // skip whitespace, comments and # directives
  int n = 0;
  int ch = current_->LastChar();
  while (ch != '\0') {
    if (isspace(ch)) {  // skip whitespace
      ch = current_->Lookahead(n++);
    } else if (ch == '#') {  // skip comment or directive
      while (ch != '\0' && ch != '\n')
        ch = current_->Lookahead(n++);
    } else {
      break;
    }
  }
  return ch;
}

}  // namespace sawzall
