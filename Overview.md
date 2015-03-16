# Components #

Szl consists of a compiler and runtime to support the Sawzall language.  It contains:

  * A tool **szl** that compile and run Sawzall programs.
  * A protocol compiler plugin for the Sawzall language.
  * A library that provides compilation and execution services, including the serialization of types and aggregated data.
  * A library that implements the aggregation (table) functionality.
  * A library for the optional intrinsic functions.
  * Tests and examples

## The Sawzall Language ##

Sawzall is a procedural language developed for parallel analysis of very large data sets (such as logs). It provides protocol buffer handling, regular expression support, string and array manipulation, associative arrays (maps), structured data (tuples), data fingerprinting (64-bit hash values), time values, various utility operations and the usual library functions operating on floating-point and string values. For years Sawzall has been Google's logs processing language of choice and is used for various other data analysis tasks across the company.

Instead of specifying how to process the entire data set, a Sawzall program describes the processing steps for a single data record independent of others. It also provides statements for emitting extracted intermediate results to predefined containers that aggregate data over all records. The separation between per-record processing and aggregation enables parallelization. Multiple records can be processed in parallel by different runs of the same program, possibly distributed across many machines. The language does not specify a particular implementation for aggregation, but a number of aggregators are supplied.  Aggregation within a single execution is automatic.  Aggregation of results from multiple executions is not automatic but an example program is supplied.

## The **szl** program ##

This standalone tool compiles and executes Sawzall programs.  Input records can be text or binary.  Aggregated data is output as formatted text.  All supplied aggregators (table types) are supported.

## The Protocol Compiler Plugin ##

If the **protoc** protocol compiler (http://code.google.com/p/protobuf/) is run with the Sawzall language plugin **protoc\_gen\_szl** it generates Sawzall code as its output.  Conversion to and from binary protocol buffer format is supported directly in the language.

## Compilation and Execution Library ##

When szl is used as part of another program, e.g. in a program that uses it for map-reduce, the **libszl** library can be used to compile and execute Sawzall programs.  This library also supports serializing aggregated data and serializing type information used to describe aggregated data.  This provides a mechanism for exporting aggregated data and then importing and combining it in a later execution, as in map-reduce.

## Aggregation Library ##

The **libszlemitters** library contains support for the builtin data aggregators (table types).  A program using **libszl** can omit this library if the builtin table types are not needed.  It is also possible to write custom emitters, together with or separate from the builtin emitters, without modifying **libszl**.  Emitters register themselves during static initialization, so supplying them when a program is linked makes them available.

## Intrinsic Function Library ##

Some of the builtin intrinsic functions are provided in a separate library, which may be omitted in a program using **libszl** if these intrinsics are not needed.  It is aso possible to write custom intrinsics.  Intrinsics register themselves during static initialization, so supplying them when a program is linked makes them available.

## Tests and Examples ##

The source code includes tests of Sawzall language features, aggregation (table types) and other functionality.

Several tests are written as example programs, including an example of the use of **libszl** in a simulated map-reduce context.

# Documentation #

  * [Abstract (and link) of a technical paper discussing Sawzall](Interpreting_the_Data.md)
  * [Sawzall language specification](http://szl.googlecode.com/svn/doc/sawzall-spec.html)
  * [Informal Sawzall language description](http://szl.googlecode.com/svn/doc/sawzall-language.html)
  * [Coding style guide](http://szl.googlecode.com/svn/doc/sawzall-style-guide.html)
  * [Intrinsic functions](http://szl.googlecode.com/svn/doc/sawzall-intrinsics.html)
  * [Table (aggregator) types](Sawzall_Table_Types.md)
  * [Discussion of undefined values and def() checks](Sawzall_Undef_Notes.md)
  * [Performance tips](Sawzall_Performance_Tips.md)
  * [Proto-to-Sawzall Translation](Sawzall_Proto_Translation.md)