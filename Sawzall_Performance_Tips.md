# Saw Performance Tips #

If your use of Sawzall uses lots of data, is CPU-intensive and/or performance critical, check out the techniques below that can help you optimize your code.

## Profile code snippets with `getresourcestats()` ##

If you're not sure which way is more efficient, just do a little test:

```
   start: resourcestats = getresourcestats();
   # put code snippet here
   end: resourcestats = getresourcestats();
   emit stdout <- format("allocated memory: %d bytes", end.allocatedmem - start.allocatedmem);
   emit stdout <- format("user time: %d us", int(end.usertime - start.usertime));
```

## Avoid unnecessary memory allocations ##

Growing data structure in Sawzall allocates new memory, so it can significantly impact performance if you are doing this in a loop for a large number of elements.

### Instead of building up a large string by concatenating new chunks (uses O(n^2) memory) ###
```
s: string = "";
for (i: int; i < N; i++) {
  s = s + f(i);
}
```
first compute the length of the character array you're going to copy into, allocate that much space, and then operate on the full array:
```
s: string = new(string, N, ' ');
for (i: int; i < N; i++) {
  s[i] = f(i);
}
```


### Instead of building up a large array by concatenating new elements ###
```
a: array of int = {}
for (i: int; i < N; i++) {
  a = a + f(i);
}
```
preallocate the array and populate it:
```
a: array of int = new(array of int, N, -1);
for (i: int; i < N; i++) {
  a[i] = f(i);
}
```

### Instead of growing a large map dynamically when an upper bound is known ahead of time ###
```
m: map[f: foo] of b: bar = {:};
when (i: each int; def(a[i]) && def(b[i])) {
  m[a[i]] = b[i];
}
```
consider preallocating the map, thus minimizing the reallocation cost, as follows:
```
m: map[f: foo] of b: bar = new(map[foo] of bar, len(a));
when (i: each int; def(a[i]) && def(b[i])) {
  m[a[i]] = b[i];
}
```

The savings aren't as dramatic as one gets with strings or arrays, but they can still be significant when dealing with large maps.

## Remove unnecessary `def()` checks ##

`def()` can be more expensive than a simple test, especially when it fails.
It's not so expensive that it should be avoided, but the semantics of undefined
variables makes it possible to simplify some calculations.
Also, sloppy use of `def()` to test map membership can cause multiple lookups;
there are intrinsics to help.

### Instead of checking both the base and field ###
```
log_record: LogRecordProto = input;
if (!def(log_record))
  return;
value: string = log_record.value; # implicit conversion to string
if (!def(value))
  return;
```
just check the field (assuming you don't care about any other fields):
```
log_record: LogRecordProto = input;
value: string = log_record.value; # implicit conversion to string
if (!def(value))
  return;
```
Note that checking `log_record` would be sufficient if there were no conversion; but `string(log_record.value)` could be undefined even if `log_record.value` is defined.

### Instead of checking many parts or fields ###
```
log_record: LogRecordProto = input;
# This is bad anyway; probably want inproto() instead.
if (!def(log_record.value) || !def(log_record.version) || !def(log_record.id))
  return;
```
just check the base value:
```
log_record: LogRecordProto = input;
if (!def(log_record))
  return;
# all fields are defined
```

### Instead of using `def()` when working with map keys ###
```
x: string;
if (def(my_map[some_key])) {
  x = my_map[some_key];
  emit stdout <- format("%s contains key %s", my_map, some_key);
} else {
  x = "default value";
}
```
use `haskey()`:
```
if (haskey(my_map, some_key))
  emit stdout <- format("%s contains key %s", my_map, some_key);
```
or `lookup()`:
```
x := lookup(my_map, some_key, "default value");
```

### When looking for a proto field that matches a string, instead of converting it to string first, then comparing ###
```
value: string = log_record.value; # implicit conversion from bytes to string
if (def(value)) { # make sure bytes-to-string conversion did not fail
  if (value == "foo") {
    ...
  }
}
```
compare to a byte constant first, then convert to string:
```
if (log_record.value == B"foo") { # use bytes(foo) for string variables
  value: string = log_record.value;
  # No need to def-check this conversion
  ...
}
```

## Avoid unnecessary conversions ##

Consider this code snippet:
```
if (string(log_record.list[i].value, "UTF-8") == "yellow")
```
It's some trouble to convert the <tt>value</tt> field to string format so that we can compare it to a constant.  And we forgot to check that the conversion succeeded.  But why are we converting?  Instead, use the original value and compare it to a bytes constant:
```
if (log_record.list[i].value == B"yellow")
```

## Replace `saw()` / `sawn()` / `sawzall()` intrinsics with simpler primitives ##

These intrinsic functions have more overhead, so for simple operations it can be better to use simpler primitives, especially if the calculation is in a loop.

### Instead of using `saw()` ###
```
url: string = "http://www.google.com/search?q=foo";

# check if the url is a google search
parts: array of string = saw(url, `^http://`, `^www\.google\.[^:/]+`,
                             `^/search\?`, `.+`);
if (len(parts) == 4)
  emit stdout <- "url matched";
```
use `match()`:
```
# check if the url is a google search
if (match(`^http://www\.google\.[^:/]+/search\?.+`, url))
  emit stdout <- "url matched";
```

or use `strfind()` and string slices:

```
# check if the url is a google search
static kPart1: string = "http://www.google.";
static kPart1Len: int = len(kPart1);
static kPart2: string = "/search?";

if (url[0:kPart1Len] == kPart1 && strfind(kPart2, url[kPart1Len+1:$-1]) >= 0)
  emit stdout <- "url matched";
```

## Simplify regular expressions ##

Regular expressions can be slow so take care to make them specific.

### Instead of using `match()` ###
```
static kHttp: string = `^http`;

if (match(kHttp, url)
  emit stdout <- format("%s begins with http", url);
```

use `strfind()`:

```
static kHttp: string = "http";

if (strfind(kHttp, url) == 0)
  emit stdout <- format("%s begins with", url);
```

or use a string slice:

```
static kHttp: string = "http";
static kLenHttp: int = len(kHttp);

if (url[0:kLenHttp] == kHttp)
  emit stdout <- format("%s begins with", url);
```

### Instead of declaring your regular expression with double quotes ["] ###
```
# matches any number of whitespace charaters a plus sign and at least one digit
if (match("\\s*\\+\\d+", some_str))
  emit stdout <- format("%s matches", some_str);
```
use backticks [`] (don't have to escape backslashes):
```
# matches any number of whitespace charaters a plus sign and at least one digit
if (match(`\s*\+\d+`, some_str))
  emit stdout <- format("%s matches", some_str);
```

## Initialize variables statically if they do not depend on the processed record ##

The `static` keyword specifies that a variable (including functions) is to be initialized once (once per machine in distributed execution), instead of once per record. This can be particularly useful when using the `load()` function to read a data file to be shared for analysis of all records.
```
type TZMap = map[longname: string] of shortname: string;

static LoadData: function(): TZMap {
  text := string(load("tzmap"));
  if (def(text)) {
    return convert(TZMap, sawzall(text, `[^\t]+`, skip `\t`, `[^\n]+`, skip `\n`));
  } else {
    return {:};
  }
};

static kTZMap: TZMap = LoadData();
```

Note that anything that depends on the current record cannot be static.

## Emitter Tips ##

Emits are much slower than the computations you do in Sawzall because they send data to external aggregators that have many moving parts, so emit wisely.

### Replace multiple emits with one that emits all data ###

For example,

```
t: table sum of { int, int, int, ... };
...
emit t <- { value1, 0, 0, ... };
emit t <- { 0, value2, 0, ... };
emit t <- { 0, 0, value3, ... };
...
```
can be rewritten as
```
t: table sum of { int, int, int, ... };
...
emit t <- { value1, value2, value3, ... };
```

And although simple `sum` tables of ints and floats are partially optimized, doing this:
```
t0: table sum of int;
t1: table sum of int;
t2: table sum of int;
...

emit t0 <- nrand(10);
emit t1 <- nrand(10);
emit t2 <- nrand(10);
...
```
is still slower than:
```
t: table sum of { int, int, int, ... };
emit t <- { nrand(10), nrand(10), nrand(10), ... };
```

### Consider local post-processing instead of emitting extra data ###

For example, if you want to populate a symmetric matrix of values, consider emitting only elements above the diagonal, i.e. only (a, b) and not (b, a) if (a, b) == (b, a). Instead of
```
when (i: each int; def(experiment_list[i]) {
  when (j: each int; def(experiment_list[j]) && (i != j)) {
    emit out_table[experiment_list[i]][experiment_list[j]] <- large_amounts_of_data;
  }
}
```
only emit half of the matrix modifying the code to always put the lower value first:
```
when (i: each int; def(experiment_list[i]) {
  when (j: each int; experiment_list[i] < experiment_list[j]) {
   emit out_table[experiment_list[i]][experiment_list[j]] <- large_amounts_of_data;
  }
}
```

Since cross products are generally very large and the emits can take a large portion of a program's execution time, this can save substantial amounts of resources.