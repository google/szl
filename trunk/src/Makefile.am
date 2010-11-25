## Process this file with automake to produce Makefile.in

AM_CXXFLAGS = $(CXXFLAGS) $(PTHREAD_CFLAGS) $(SZL_OPT_FLAG) $(SZL_DEFINES) -g -O1

AM_LDFLAGS = $(PTHREAD_CFLAGS) $(SZL_EXTRA_LDFLAGS)

clean-local:
	rm -f *.loT

CLEANFILES = $(protoc_outputs)

MAINTAINERCLEANFILES =   \
  Makefile.in


##### Need a plugin to let the protocol compiler generate Sawzall output

protoc_gen_szl_SOURCES = \
  protoc_plugin/circularity_detector-inl.h \
  protoc_plugin/circularity_detector.h \
  protoc_plugin/linked_ptr.h \
  protoc_plugin/proto-sorter.cc \
  protoc_plugin/proto-sorter.h \
  protoc_plugin/strutil.cc \
  protoc_plugin/strutil.h \
  protoc_plugin/szl_generator.cc \
  protoc_plugin/szl_generator.h \
  protoc_plugin/szl_plugin.cc \
  protoc_plugin/topologicalsorter-inl.h \
  protoc_plugin/topologicalsorter.h


##### We use one .proto file in the C++ code

protoc_inputs = emitvalues/sawzall.proto

protoc_outputs = emitvalues/sawzall.pb.cc emitvalues/sawzall.pb.h

BUILT_SOURCES = $(protoc_outputs)

# We have to cd to $(srcdir) before executing protoc because $(protoc_inputs) is
# relative to srcdir, which may not be the same as the current directory when
# building out-of-tree.
$(protoc_outputs): $(protoc_inputs) Makefile
	oldpwd=`pwd` && ( cd $(srcdir) && $(PROTOC) -I. --cpp_out=$$oldpwd $(protoc_inputs) )


##### Internal libraries

# This is mostly just to organize the code.  The fmt, utilities and values
# libraries do not depend on the rest of szl.   The engine library contains
# the compiler and runtime.

noinst_LTLIBRARIES = libfmt.la libutilities.la libvalues.la libengine.la

libfmt_la_SOURCES = \
  fmt/charstod.cc \
  fmt/dofmt.cc \
  fmt/errfmt.cc \
  fmt/fltfmt.cc \
  fmt/fmt.cc \
  fmt/fmt.h \
  fmt/fmtdef.h \
  fmt/fmtfd.cc \
  fmt/fmtfdflush.cc \
  fmt/fmtlocale.cc \
  fmt/fmtprint.cc \
  fmt/fmtquote.cc \
  fmt/fmtrune.cc \
  fmt/fmtstr.cc \
  fmt/fmtvprint.cc \
  fmt/fprint.cc \
  fmt/nan.h \
  fmt/nan64.cc \
  fmt/pow10.cc \
  fmt/print.cc \
  fmt/runefmtstr.cc \
  fmt/runes.cc \
  fmt/runes.h \
  fmt/runeseprint.cc \
  fmt/runesmprint.cc \
  fmt/runesnprint.cc \
  fmt/runesprint.cc \
  fmt/runevseprint.cc \
  fmt/runevsmprint.cc \
  fmt/runevsnprint.cc \
  fmt/seprint.cc \
  fmt/smprint.cc \
  fmt/snprint.cc \
  fmt/sprint.cc \
  fmt/strtod.cc \
  fmt/strtod.h \
  fmt/vfprint.cc \
  fmt/vseprint.cc \
  fmt/vsmprint.cc \
  fmt/vsnprint.cc

libutilities_la_SOURCES = \
  utilities/acmrandom.cc \
  utilities/acmrandom.h \
  utilities/commandlineflags.cc \
  utilities/commandlinehelpflags.cc \
  utilities/dbutils.h \
  utilities/gzipwrapper.cc \
  utilities/gzipwrapper.h \
  utilities/hashutils.cc \
  utilities/logging.cc \
  utilities/lzw.cc \
  utilities/lzw.h \
  utilities/mt_random.cc \
  utilities/mt_random.h \
  utilities/port_ieee.h \
  utilities/quotefmt.cc \
  utilities/quotefmt.h \
  utilities/random_base.cc \
  utilities/random_base.h \
  utilities/recordio.cc \
  utilities/strtotm.cc \
  utilities/strtotm.h \
  utilities/strutils.cc \
  utilities/strutils.h \
  utilities/sysutils.cc \
  utilities/sysutils.h \
  utilities/szlmutex.cc \
  utilities/szlmutex.h \
  utilities/timeutils.cc \
  utilities/timeutils.h \
  utilities/varint.cc \
  utilities/zlibwrapper.cc \
  utilities/zlibwrapper.h


libvalues_la_SOURCES = \
  emitvalues/sawzall.pb.cc \
  emitvalues/sawzall.pb.h \
  emitvalues/sawzall.proto \
  emitvalues/szldecoder.cc \
  emitvalues/szlemitter.cc \
  emitvalues/szlencoder.cc \
  emitvalues/szlencoding.h \
  emitvalues/szlresults.cc \
  emitvalues/szltabentry.cc \
  emitvalues/szltype.cc \
  emitvalues/szlvalue.cc \
  emitvalues/szlxlate.cc \
  emitvalues/szlxlate.h


libengine_la_SOURCES = \
  engine/analyzer.cc \
  engine/analyzer.h \
  engine/assembler.cc \
  engine/assembler.h \
  engine/backendtype.cc \
  engine/backendtype.h \
  engine/closurecheck.cc \
  engine/code.cc \
  engine/code.h \
  engine/codegen.cc \
  engine/codegen.h \
  engine/codegenutils.cc \
  engine/codegenutils.h \
  engine/compiler.cc \
  engine/compiler.h \
  engine/constantfolding.cc \
  engine/constantfolding.h \
  engine/convop.cc \
  engine/convop.h \
  engine/debugger.cc \
  engine/debugger.h \
  engine/elfgen.cc \
  engine/elfgen.h \
  engine/engine.cc \
  engine/engine.h \
  engine/error.cc \
  engine/error.h \
  engine/factory.cc \
  engine/factory.h \
  engine/fieldreferences.cc \
  engine/form.cc \
  engine/form.h \
  engine/frame.cc \
  engine/frame.h \
  engine/gctrigger.cc \
  engine/gctrigger.h \
  engine/globals.cc \
  engine/globals.h \
  engine/help.cc \
  engine/help.h \
  engine/histogram.cc \
  engine/histogram.h \
  engine/intrinsic.cc \
  engine/intrinsic.h \
  engine/ir.cc \
  engine/ir.h \
  engine/linecount.cc \
  engine/linecount.h \
  engine/map.cc \
  engine/map.h \
  engine/memory.cc \
  engine/memory.h \
  engine/nativecodegen.cc \
  engine/nativecodegen.h \
  engine/nativesupport.cc \
  engine/nativesupport.h \
  engine/node.cc \
  engine/node.h \
  engine/opcode.cc \
  engine/opcode.h \
  engine/outputter.cc \
  engine/outputter.h \
  engine/parser.cc \
  engine/parser.h \
  engine/printvisitor.cc \
  engine/printvisitor.h \
  engine/proc.cc \
  engine/proc.h \
  engine/profile.cc \
  engine/profile.h \
  engine/propagatevalues.cc \
  engine/protocolbuffers.cc \
  engine/protocolbuffers.h \
  engine/regsstate.cc \
  engine/regsstate.h \
  engine/rewriteasserts.cc \
  engine/sawzall.cc \
  engine/scanner.cc \
  engine/scanner.h \
  engine/scope.cc \
  engine/scope.h \
  engine/symboltable.cc \
  engine/symboltable.h \
  engine/taggedptrs.cc \
  engine/taggedptrs.h \
  engine/tracer.cc \
  engine/tracer.h \
  engine/treevisitor.cc \
  engine/treevisitor.h \
  engine/type.cc \
  engine/type.h \
  engine/utils.cc \
  engine/utils.h \
  engine/val.cc \
  engine/val.h \
  engine/value.cc \
  engine/when.cc \
  engine/when.h



##### Libraries to install

libszl_la_LIBADD = \
  libengine.la \
  libfmt.la \
  libutilities.la \
  libvalues.la
libszl_la_SOURCES =

libszlintrinsics_la_SOURCES = \
  intrinsics/additionalinputintrinsic.cc \
  intrinsics/dbintrinsic.cc \
  intrinsics/inprotocount.cc \
  intrinsics/mathintrinsic.cc \
  intrinsics/miscintrinsic.cc \
  intrinsics/sortintrinsic.cc \
  intrinsics/ziplib.cc

libszlemitters_la_SOURCES = \
  emitters/szlbootstrapsum.cc \
  emitters/szlbootstrapsumresults.cc \
  emitters/szlcollection.cc \
  emitters/szlcollectionresults.cc \
  emitters/szlcomputeinversehistogram.cc \
  emitters/szlcomputequantiles.cc \
  emitters/szldistinctsample.cc \
  emitters/szldistinctsampleresults.cc \
  emitters/szlheap.cc \
  emitters/szlheap.h \
  emitters/szlmaximum.cc \
  emitters/szlmaximumresults.cc \
  emitters/szlmrcounter.cc \
  emitters/szlquantile.cc \
  emitters/szlquantile.h \
  emitters/szlquantile_performance.cc \
  emitters/szlquantileresults.cc \
  emitters/szlrecordio.cc \
  emitters/szlsample.cc \
  emitters/szlsampleresults.cc \
  emitters/szlset.cc \
  emitters/szlsetresults.cc \
  emitters/szlsketch.cc \
  emitters/szlsketch.h \
  emitters/szlsum.cc \
  emitters/szlsumresults.cc \
  emitters/szltext.cc \
  emitters/szltop.cc \
  emitters/szltopheap.cc \
  emitters/szltopheap.h \
  emitters/szltopresults.cc \
  emitters/szlunique.cc \
  emitters/szluniqueresults.cc \
  emitters/szlweightedsample.cc \
  emitters/szlweightedsampleadapter.cc \
  emitters/szlweightedsampleadapter.h \
  emitters/szlweightedsampleresults.cc \
  emitters/weighted-reservoir-sampler-impl.cc \
  emitters/weighted-reservoir-sampler-impl.h \
  emitters/weighted-reservoir-sampler.h



##### The szl program

szl_LDADD = libszl.la libszlemitters.la libszlintrinsics.la
szl_SOURCES = \
  app/szl.cc \
  app/szlemitterfactory.cc \
  app/szlemitterfactory.h \
  app/printemitter.cc \
  app/printemitter.h \
  app/szlutils.cc \
  app/szlutils.h


##### Tests - set up environment variables for scripts

export ABS_SRCDIR = $(abs_srcdir)
export SZL = $(abs_srcdir)/szl
export SZLEXEC = $(abs_srcdir)/.libs/szl
export SZL_TMP = /tmp
export PROTOCOL_COMPILER = $(PROTOC)
export PROTOCOL_COMPILER_PLUGIN = $(abs_srcdir)/protoc-gen-szl
export PROTOCOL_COMPILER_TMP = /tmp
export OBJDUMP_UTILITY = $(OBJDUMP)
if SZL_USES_MEMORY_LIMIT
  export SZL_USES_MEMORY_LIMIT = 1
endif


##### Tests - application level

app_test_programs = \
  eval_demo_unittest \
  mapreduce_demo_unittest \
  multiexe_unittest \
  sawzall_unittest

app_tests = $(app_test_programs)

app_test_libs = libszl.la

eval_demo_unittest_LDADD = $(app_test_libs) libszlintrinsics.la
eval_demo_unittest_SOURCES = app/tests/eval_demo_unittest.cc

mapreduce_demo_unittest_LDADD = $(app_test_libs) libszlemitters.la libszlintrinsics.la
mapreduce_demo_unittest_SOURCES = app/tests/mapreduce_demo_unittest.cc

multiexe_unittest_LDADD = $(app_test_libs)
multiexe_unittest_SOURCES = app/tests/multiexe_unittest.cc

sawzall_unittest_LDADD = $(app_test_libs)
sawzall_unittest_SOURCES = app/tests/sawzall_unittest.cc


##### Tests - engine and general

engine_test_programs = \
  assembler_unittest \
  assertion_unittest \
  debugger_test \
  docalls_test \
  error_handler_unittest \
  overload_unittest \
  protobytesskipped_unittest \
  prototobytes_unittest \
  utils_test \
  val_unittest

engine_tests = \
  $(engine_test_programs) \
  engine/tests/szl_large_composite_test.sh \
  engine/tests/szl_variable_inode_test.sh \
  engine/language_tests/szl_regtest.sh

engine_test_libs = libszl.la

assembler_unittest_LDADD = $(engine_test_libs)
assembler_unittest_SOURCES = engine/tests/assembler_unittest.cc

assertion_unittest_LDADD = $(engine_test_libs)
assertion_unittest_SOURCES = engine/tests/assertion_unittest.cc

debugger_test_LDADD = $(engine_test_libs)
debugger_test_SOURCES = engine/tests/debugger_test.cc

docalls_test_LDADD = $(engine_test_libs)
docalls_test_SOURCES = engine/tests/docalls_test.cc

error_handler_unittest_LDADD = $(engine_test_libs)
error_handler_unittest_SOURCES = engine/tests/error_handler_unittest.cc

overload_unittest_LDADD = $(engine_test_libs)
overload_unittest_SOURCES = engine/tests/overload_unittest.cc

protobytesskipped_unittest_LDADD = $(engine_test_libs)
protobytesskipped_unittest_SOURCES = engine/tests/protobytesskipped_unittest.cc

prototobytes_unittest_LDADD = $(engine_test_libs)
prototobytes_unittest_SOURCES = engine/tests/prototobytes_unittest.cc

utils_test_LDADD = $(engine_test_libs)
utils_test_SOURCES = engine/tests/utils_test.cc

val_unittest_LDADD = $(engine_test_libs)
val_unittest_SOURCES = engine/tests/val_unittest.cc


##### ELF support only in linux

if ELFGEN_UNITTEST
  elfgen_test_program = elfgen_unittest
  elfgen_test =  elfgen_unittest
  elfgen_unittest_LDADD = $(engine_test_libs)
  elfgen_unittest_SOURCES = engine/tests/elfgen_unittest.cc
else
  elfgen_unittest_program =
  elfgen_unittest_test =
endif


##### Tests - emitters

emitter_test_programs = \
  szlmaximum_unittest \
  szlquantile_unittest \
  szlrecordio_unittest \
  szlsample_unittest \
  szlset_unittest \
  szlsum_unittest \
  szltext_unittest \
  szltop_unittest \
  szlunique_unittest \
  szlweightedsample_unittest \
  szlquantile_regtest \
  szldistinctsample_unittest \
  szlbootstrapsum_unittest \
  szlcollection_unittest

emitter_tests = $(emitter_test_programs)

emitter_test_libs = libszl.la libszlemitters.la

szlmaximum_unittest_LDADD = $(emitter_test_libs)
szlmaximum_unittest_SOURCES = emitters/tests/szlmaximum_unittest.cc

szlquantile_unittest_LDADD = $(emitter_test_libs)
szlquantile_unittest_SOURCES = emitters/tests/szlquantile_unittest.cc

szlrecordio_unittest_LDADD = $(emitter_test_libs)
szlrecordio_unittest_SOURCES = emitters/tests/szlrecordio_unittest.cc

szlsample_unittest_LDADD = $(emitter_test_libs)
szlsample_unittest_SOURCES = emitters/tests/szlsample_unittest.cc

szlset_unittest_LDADD = $(emitter_test_libs)
szlset_unittest_SOURCES = emitters/tests/szlset_unittest.cc

szlsum_unittest_LDADD = $(emitter_test_libs)
szlsum_unittest_SOURCES = emitters/tests/szlsum_unittest.cc

szltext_unittest_LDADD = $(emitter_test_libs)
szltext_unittest_SOURCES = emitters/tests/szltop_unittest.cc

szltop_unittest_LDADD = $(emitter_test_libs)
szltop_unittest_SOURCES = emitters/tests/szltop_unittest.cc

szlunique_unittest_LDADD = $(emitter_test_libs)
szlunique_unittest_SOURCES = emitters/tests/szlunique_unittest.cc

szlweightedsample_unittest_LDADD = $(emitter_test_libs)
szlweightedsample_unittest_SOURCES = emitters/tests/szlweightedsample_unittest.cc

szlquantile_regtest_LDADD = $(emitter_test_libs)
szlquantile_regtest_SOURCES = emitters/tests/szlquantile_regtest.cc

szldistinctsample_unittest_LDADD = $(emitter_test_libs)
szldistinctsample_unittest_SOURCES = emitters/tests/szldistinctsample_unittest.cc

szlbootstrapsum_unittest_LDADD = $(emitter_test_libs)
szlbootstrapsum_unittest_SOURCES = emitters/tests/szlbootstrapsum_unittest.cc

szlcollection_unittest_LDADD = $(emitter_test_libs)
szlcollection_unittest_SOURCES = emitters/tests/szlcollection_unittest.cc


##### Tests - fmt

fmt_test_programs = \
  fltfmt_unittest \
  fmt_unittest \
  fmt_test

fmt_tests = \
  fltfmt_unittest \
  fmt_unittest \
  fmt/tests/fmt_test.sh

fmt_test_libs = libfmt.la

fltfmt_unittest_LDADD = $(fmt_test_libs)
fltfmt_unittest_SOURCES = fmt/tests/fltfmt_unittest.cc

fmt_unittest_LDADD = $(fmt_test_libs)
fmt_unittest_SOURCES = fmt/tests/fmt_unittest.cc

fmt_test_LDADD = $(fmt_test_libs)
fmt_test_SOURCES = fmt/tests/fmt_test.cc


##### Tests - emitvalues

emitvalues_test_programs = szlemitter_test
emitvalues_tests = ./szlemitter_test

emitvalues_test_libs = libszl.la libszlemitters.la

szlemitter_test_LDADD = $(emitvalues_test_libs)
szlemitter_test_SOURCES = emitvalues/tests/szlemitter_test.cc


##### Tests - intrinsics

intrinsics_test_programs = additionalinput_test
intrinsics_tests = ./additionalinput_test

intrinsics_test_libs = libszl.la libszlintrinsics.la

additionalinput_test_LDADD = $(intrinsics_test_libs)
additionalinput_test_SOURCES = intrinsics/tests/additionalinput_test.cc


##### Test scripts - to force distribution

EXTRA_DIST = \
  fmt/tests/fmt_test.sh \
  engine/language_tests/szl_regtest.sh \
  engine/tests/szl_large_composite_test.sh \
  engine/tests/szl_variable_inode_test.sh


##### Test data

testdatafiles = \
  engine/language_tests/README \
  engine/language_tests/base/arith.err \
  engine/language_tests/base/arith.out \
  engine/language_tests/base/arith.szl \
  engine/language_tests/base/array.err \
  engine/language_tests/base/array.out \
  engine/language_tests/base/array.szl \
  engine/language_tests/base/arrayadd.err \
  engine/language_tests/base/arrayadd.out \
  engine/language_tests/base/arrayadd.szl \
  engine/language_tests/base/arrayconv.err \
  engine/language_tests/base/arrayconv.out \
  engine/language_tests/base/arrayconv.szl \
  engine/language_tests/base/arrayconv1.err \
  engine/language_tests/base/arrayconv1.out \
  engine/language_tests/base/arrayconv1.szl \
  engine/language_tests/base/arrayconv2.err \
  engine/language_tests/base/arrayconv2.out \
  engine/language_tests/base/arrayconv2.szl \
  engine/language_tests/base/arrayconv3.err \
  engine/language_tests/base/arrayconv3.out \
  engine/language_tests/base/arrayconv3.szl \
  engine/language_tests/base/arraymapconv.sh \
  engine/language_tests/base/assert.err \
  engine/language_tests/base/assert.out \
  engine/language_tests/base/assert.szl \
  engine/language_tests/base/badclosures.err \
  engine/language_tests/base/badclosures.out \
  engine/language_tests/base/badclosures.szl \
  engine/language_tests/base/baddefness.err \
  engine/language_tests/base/baddefness.out \
  engine/language_tests/base/baddefness.szl \
  engine/language_tests/base/badfunstaticvars.err \
  engine/language_tests/base/badfunstaticvars.out \
  engine/language_tests/base/badfunstaticvars.szl \
  engine/language_tests/base/badfunvars.err \
  engine/language_tests/base/badfunvars.out \
  engine/language_tests/base/badfunvars.szl \
  engine/language_tests/base/badinc.err \
  engine/language_tests/base/badinc.out \
  engine/language_tests/base/badinc.szl \
  engine/language_tests/base/badindex.err \
  engine/language_tests/base/badindex.out \
  engine/language_tests/base/badindex.szl \
  engine/language_tests/base/badindex2.err \
  engine/language_tests/base/badindex2.out \
  engine/language_tests/base/badindex2.szl \
  engine/language_tests/base/badlit.err \
  engine/language_tests/base/badlit.out \
  engine/language_tests/base/badlit.szl \
  engine/language_tests/base/badnew.err \
  engine/language_tests/base/badnew.out \
  engine/language_tests/base/badnew.szl \
  engine/language_tests/base/badnew2.err \
  engine/language_tests/base/badnew2.out \
  engine/language_tests/base/badnew2.szl \
  engine/language_tests/base/badout.err \
  engine/language_tests/base/badout.out \
  engine/language_tests/base/badout.szl \
  engine/language_tests/base/badpackage.err \
  engine/language_tests/base/badpackage.out \
  engine/language_tests/base/badpackage.szl \
  engine/language_tests/base/badprint.err \
  engine/language_tests/base/badprint.out \
  engine/language_tests/base/badprint.szl \
  engine/language_tests/base/badrecursivetypes.err \
  engine/language_tests/base/badrecursivetypes.out \
  engine/language_tests/base/badrecursivetypes.szl \
  engine/language_tests/base/badtuplelocalstatics.err \
  engine/language_tests/base/badtuplelocalstatics.out \
  engine/language_tests/base/badtuplelocalstatics.szl \
  engine/language_tests/base/badtuplelocaltypes.err \
  engine/language_tests/base/badtuplelocaltypes.out \
  engine/language_tests/base/badtuplelocaltypes.szl \
  engine/language_tests/base/badunicode.err \
  engine/language_tests/base/badunicode.out \
  engine/language_tests/base/badunicode.szl \
  engine/language_tests/base/bytes.err \
  engine/language_tests/base/bytes.out \
  engine/language_tests/base/bytes.szl \
  engine/language_tests/base/comment.err \
  engine/language_tests/base/comment.out \
  engine/language_tests/base/comment.szl \
  engine/language_tests/base/compare.err \
  engine/language_tests/base/compare.out \
  engine/language_tests/base/compare.szl \
  engine/language_tests/base/compat.err \
  engine/language_tests/base/compat.out \
  engine/language_tests/base/compat.szl \
  engine/language_tests/base/composites.err \
  engine/language_tests/base/composites.out \
  engine/language_tests/base/composites.szl \
  engine/language_tests/base/condops.err \
  engine/language_tests/base/condops.out \
  engine/language_tests/base/condops.szl \
  engine/language_tests/base/constantfolding.err \
  engine/language_tests/base/constantfolding.out \
  engine/language_tests/base/constantfolding.szl \
  engine/language_tests/base/constantfolding2.err \
  engine/language_tests/base/constantfolding2.out \
  engine/language_tests/base/constantfolding2.szl \
  engine/language_tests/base/constantfolding3.err \
  engine/language_tests/base/constantfolding3.out \
  engine/language_tests/base/constantfolding3.szl \
  engine/language_tests/base/constantfolding4.err \
  engine/language_tests/base/constantfolding4.out \
  engine/language_tests/base/constantfolding4.szl \
  engine/language_tests/base/continue1.err \
  engine/language_tests/base/continue1.out \
  engine/language_tests/base/continue1.szl \
  engine/language_tests/base/continue2.err \
  engine/language_tests/base/continue2.out \
  engine/language_tests/base/continue2.szl \
  engine/language_tests/base/conv.err \
  engine/language_tests/base/conv.out \
  engine/language_tests/base/conv.szl \
  engine/language_tests/base/conv2.err \
  engine/language_tests/base/conv2.out \
  engine/language_tests/base/conv2.szl \
  engine/language_tests/base/defaultbytes.err \
  engine/language_tests/base/defaultbytes.out \
  engine/language_tests/base/defaultbytes.szl \
  engine/language_tests/base/defleak.err \
  engine/language_tests/base/defleak.out \
  engine/language_tests/base/defleak.szl \
  engine/language_tests/base/defness-U.err \
  engine/language_tests/base/defness-U.out \
  engine/language_tests/base/defness-U.szl \
  engine/language_tests/base/defness.err \
  engine/language_tests/base/defness.out \
  engine/language_tests/base/defness.szl \
  engine/language_tests/base/defness2.err \
  engine/language_tests/base/defness2.out \
  engine/language_tests/base/defness2.szl \
  engine/language_tests/base/defness3.err \
  engine/language_tests/base/defness3.out \
  engine/language_tests/base/defness3.szl \
  engine/language_tests/base/defness4.err \
  engine/language_tests/base/defness4.out \
  engine/language_tests/base/defness4.szl \
  engine/language_tests/base/dollar.err \
  engine/language_tests/base/dollar.out \
  engine/language_tests/base/dollar.szl \
  engine/language_tests/base/emit.err \
  engine/language_tests/base/emit.out \
  engine/language_tests/base/emit.szl \
  engine/language_tests/base/emit_bad.err \
  engine/language_tests/base/emit_bad.out \
  engine/language_tests/base/emit_bad.szl \
  engine/language_tests/base/printtree.szl \
  engine/language_tests/base/fibo.err \
  engine/language_tests/base/fibo.out \
  engine/language_tests/base/fibo.szl \
  engine/language_tests/base/finger.err \
  engine/language_tests/base/finger.out \
  engine/language_tests/base/finger.szl \
  engine/language_tests/base/float.err \
  engine/language_tests/base/float.out \
  engine/language_tests/base/float.szl \
  engine/language_tests/base/fnesting.err \
  engine/language_tests/base/fnesting.out \
  engine/language_tests/base/fnesting.szl \
  engine/language_tests/base/forloop.err \
  engine/language_tests/base/forloop.out \
  engine/language_tests/base/forloop.szl \
  engine/language_tests/base/formattype.err \
  engine/language_tests/base/formattype.out \
  engine/language_tests/base/formattype.szl \
  engine/language_tests/base/formerbugs.err \
  engine/language_tests/base/formerbugs.out \
  engine/language_tests/base/formerbugs.szl \
  engine/language_tests/base/formerbugs2.err \
  engine/language_tests/base/formerbugs2.out \
  engine/language_tests/base/formerbugs2.szl \
  engine/language_tests/base/functions.err \
  engine/language_tests/base/functions.out \
  engine/language_tests/base/functions.szl \
  engine/language_tests/base/functions2.err \
  engine/language_tests/base/functions2.out \
  engine/language_tests/base/functions2.szl \
  engine/language_tests/base/garbagecollection1.err \
  engine/language_tests/base/garbagecollection1.in \
  engine/language_tests/base/garbagecollection1.out \
  engine/language_tests/base/garbagecollection1.szl \
  engine/language_tests/base/garbagecollection2.err \
  engine/language_tests/base/garbagecollection2.out \
  engine/language_tests/base/garbagecollection2.szl \
  engine/language_tests/base/garbagecollection3.err \
  engine/language_tests/base/garbagecollection3.out \
  engine/language_tests/base/garbagecollection3.szl \
  engine/language_tests/base/goodclosures.err \
  engine/language_tests/base/goodclosures.out \
  engine/language_tests/base/goodclosures.szl \
  engine/language_tests/base/goodfunstaticvars.err \
  engine/language_tests/base/goodfunstaticvars.out \
  engine/language_tests/base/goodfunstaticvars.szl \
  engine/language_tests/base/goodfunvars.err \
  engine/language_tests/base/goodfunvars.out \
  engine/language_tests/base/goodfunvars.szl \
  engine/language_tests/base/goodlit.err \
  engine/language_tests/base/goodlit.out \
  engine/language_tests/base/goodlit.szl \
  engine/language_tests/base/goodout.err \
  engine/language_tests/base/goodout.out \
  engine/language_tests/base/goodout.szl \
  engine/language_tests/base/goodrecursivetypes.err \
  engine/language_tests/base/goodrecursivetypes.out \
  engine/language_tests/base/goodrecursivetypes.szl \
  engine/language_tests/base/goodrecursivetypesfunc.err \
  engine/language_tests/base/goodrecursivetypesfunc.out \
  engine/language_tests/base/goodrecursivetypesfunc.szl \
  engine/language_tests/base/goodrecursivetypesproto.err \
  engine/language_tests/base/goodrecursivetypesproto.out \
  engine/language_tests/base/goodrecursivetypesproto.szl \
  engine/language_tests/base/goodrecursivetypestree.err \
  engine/language_tests/base/goodrecursivetypestree.out \
  engine/language_tests/base/goodrecursivetypestree.szl \
  engine/language_tests/base/goodtuplelocalstatics.err \
  engine/language_tests/base/goodtuplelocalstatics.out \
  engine/language_tests/base/goodtuplelocalstatics.szl \
  engine/language_tests/base/goodtuplelocaltypes.err \
  engine/language_tests/base/goodtuplelocaltypes.out \
  engine/language_tests/base/goodtuplelocaltypes.szl \
  engine/language_tests/base/grep.err \
  engine/language_tests/base/grep.out \
  engine/language_tests/base/grep.szl \
  engine/language_tests/base/printtree.out \
  engine/language_tests/base/proto_append.szl \
  engine/language_tests/base/huge_map_literal \
  engine/language_tests/base/inc.err \
  engine/language_tests/base/inc.out \
  engine/language_tests/base/inc.szl \
  engine/language_tests/base/include.err \
  engine/language_tests/base/include.out \
  engine/language_tests/base/include.szl \
  engine/language_tests/base/include_b.err \
  engine/language_tests/base/include_b.out \
  engine/language_tests/base/include_b.szl \
  engine/language_tests/base/include_dir/a.szl \
  engine/language_tests/base/include_dir/b.szl \
  engine/language_tests/base/include_file \
  engine/language_tests/base/indexable.err \
  engine/language_tests/base/indexable.out \
  engine/language_tests/base/indexable.szl \
  engine/language_tests/base/init.err \
  engine/language_tests/base/init.out \
  engine/language_tests/base/init.szl \
  engine/language_tests/base/int.err \
  engine/language_tests/base/int.out \
  engine/language_tests/base/int.szl \
  engine/language_tests/base/intrinsic.err \
  engine/language_tests/base/intrinsic.out \
  engine/language_tests/base/intrinsic.szl \
  engine/language_tests/base/intrinsic_bad.err \
  engine/language_tests/base/intrinsic_bad.out \
  engine/language_tests/base/intrinsic_bad.szl \
  engine/language_tests/base/intrinsic_good.err \
  engine/language_tests/base/intrinsic_good.out \
  engine/language_tests/base/intrinsic_good.szl \
  engine/language_tests/base/limit_recursion-32.err \
  engine/language_tests/base/limit_recursion-32.out \
  engine/language_tests/base/limit_recursion-32.szl \
  engine/language_tests/base/limit_recursion-64.err \
  engine/language_tests/base/limit_recursion-64.out \
  engine/language_tests/base/limit_recursion-64.szl \
  engine/language_tests/base/line_directives.err \
  engine/language_tests/base/line_directives.out \
  engine/language_tests/base/line_directives.szl \
  engine/language_tests/base/line_directives2.err \
  engine/language_tests/base/line_directives2.out \
  engine/language_tests/base/line_directives2.szl \
  engine/language_tests/base/line_directives3.err \
  engine/language_tests/base/line_directives3.out \
  engine/language_tests/base/line_directives3.szl \
  engine/language_tests/base/map.err \
  engine/language_tests/base/map.out \
  engine/language_tests/base/map.szl \
  engine/language_tests/base/mapconv.err \
  engine/language_tests/base/mapconv.out \
  engine/language_tests/base/mapconv.szl \
  engine/language_tests/base/mapconv2.err \
  engine/language_tests/base/mapconv2.out \
  engine/language_tests/base/mapconv2.szl \
  engine/language_tests/base/mapkeys.err \
  engine/language_tests/base/mapkeys.out \
  engine/language_tests/base/mapkeys.szl \
  engine/language_tests/base/nancompare.err \
  engine/language_tests/base/nancompare.out \
  engine/language_tests/base/nancompare.szl \
  engine/language_tests/base/neglength.err \
  engine/language_tests/base/neglength.out \
  engine/language_tests/base/neglength.szl \
  engine/language_tests/base/new.err \
  engine/language_tests/base/new.out \
  engine/language_tests/base/new.szl \
  engine/language_tests/base/overflow.err \
  engine/language_tests/base/overflow.out \
  engine/language_tests/base/overflow.szl \
  engine/language_tests/base/package.err \
  engine/language_tests/base/package.out \
  engine/language_tests/base/package.szl \
  engine/language_tests/base/whenmultimap.szl \
  engine/language_tests/base/valuepropagation2.szl \
  engine/language_tests/base/proto_append.out \
  engine/language_tests/base/print_tables.szl \
  engine/language_tests/base/proto_keyword_good.szl \
  engine/language_tests/base/proto_keyword_good.out \
  engine/language_tests/base/peano.err \
  engine/language_tests/base/peano.out \
  engine/language_tests/base/peano.szl \
  engine/language_tests/base/precedence.err \
  engine/language_tests/base/precedence.out \
  engine/language_tests/base/precedence.szl \
  engine/language_tests/base/primes.err \
  engine/language_tests/base/primes.out \
  engine/language_tests/base/primes.szl \
  engine/language_tests/base/print_tables.err \
  engine/language_tests/base/print_tables.out \
  engine/language_tests/base/printingbugs.err \
  engine/language_tests/base/printingbugs.out \
  engine/language_tests/base/printingbugs.szl \
  engine/language_tests/base/printtree.err \
  engine/language_tests/base/regexucp.out \
  engine/language_tests/base/regexucp.szl \
  engine/language_tests/base/proto.err \
  engine/language_tests/base/proto.out \
  engine/language_tests/base/proto.szl \
  engine/language_tests/base/proto1.err \
  engine/language_tests/base/proto1.out \
  engine/language_tests/base/proto2.err \
  engine/language_tests/base/proto2.out \
  engine/language_tests/base/proto2.szl \
  engine/language_tests/base/proto3.err \
  engine/language_tests/base/proto3.out \
  engine/language_tests/base/proto3.szl \
  engine/language_tests/base/proto4.err \
  engine/language_tests/base/proto4.out \
  engine/language_tests/base/proto4.szl \
  engine/language_tests/base/proto5.err \
  engine/language_tests/base/proto5.out \
  engine/language_tests/base/proto5.szl \
  engine/language_tests/base/proto6.err \
  engine/language_tests/base/proto6.out \
  engine/language_tests/base/proto6.szl \
  engine/language_tests/base/proto7.err \
  engine/language_tests/base/proto7.out \
  engine/language_tests/base/proto7.szl \
  engine/language_tests/base/proto8.err \
  engine/language_tests/base/proto8.out \
  engine/language_tests/base/proto8.szl \
  engine/language_tests/base/proto_append.data \
  engine/language_tests/base/proto_append.err \
  engine/language_tests/base/proto_keyword_bad.szl \
  engine/language_tests/base/proto_keyword_bad.err \
  engine/language_tests/base/proto_keyword_bad.out \
  engine/language_tests/base/proto3.proto \
  engine/language_tests/base/proto_keyword_good.err \
  engine/language_tests/base/valuepropagation.szl \
  engine/language_tests/base/valuepropagation.sh \
  engine/language_tests/base/proto2.proto \
  engine/language_tests/base/proto_append.proto \
  engine/language_tests/base/ref.err \
  engine/language_tests/base/ref.out \
  engine/language_tests/base/ref.szl \
  engine/language_tests/base/refs.err \
  engine/language_tests/base/refs.out \
  engine/language_tests/base/refs.szl \
  engine/language_tests/base/regex.err \
  engine/language_tests/base/regex.out \
  engine/language_tests/base/regex.szl \
  engine/language_tests/base/regexp.err \
  engine/language_tests/base/regexp.out \
  engine/language_tests/base/regexp.szl \
  engine/language_tests/base/regexucp.err \
  engine/language_tests/base/saw.err \
  engine/language_tests/base/saw.out \
  engine/language_tests/base/saw.szl \
  engine/language_tests/base/shortcircuit.err \
  engine/language_tests/base/shortcircuit.out \
  engine/language_tests/base/shortcircuit.szl \
  engine/language_tests/base/slice.err \
  engine/language_tests/base/slice.out \
  engine/language_tests/base/slice.szl \
  engine/language_tests/base/sliceassign.err \
  engine/language_tests/base/sliceassign.out \
  engine/language_tests/base/sliceassign.szl \
  engine/language_tests/base/sliceassign2.err \
  engine/language_tests/base/sliceassign2.out \
  engine/language_tests/base/sliceassign2.szl \
  engine/language_tests/base/stackoverflow-32.err \
  engine/language_tests/base/stackoverflow-32.out \
  engine/language_tests/base/stackoverflow-32.szl \
  engine/language_tests/base/stackoverflow-64.err \
  engine/language_tests/base/stackoverflow-64.out \
  engine/language_tests/base/stackoverflow-64.szl \
  engine/language_tests/base/stackoverflow-U-32.err \
  engine/language_tests/base/stackoverflow-U-32.out \
  engine/language_tests/base/stackoverflow-U-32.szl \
  engine/language_tests/base/stackoverflow-U-64.err \
  engine/language_tests/base/stackoverflow-U-64.out \
  engine/language_tests/base/stackoverflow-U-64.szl \
  engine/language_tests/base/stackoverflow_b-32.err \
  engine/language_tests/base/stackoverflow_b-32.out \
  engine/language_tests/base/stackoverflow_b-32.szl \
  engine/language_tests/base/stackoverflow_c-32.err \
  engine/language_tests/base/stackoverflow_c-32.out \
  engine/language_tests/base/stackoverflow_c-32.szl \
  engine/language_tests/base/stackoverflow_d-32.err \
  engine/language_tests/base/stackoverflow_d-32.out \
  engine/language_tests/base/stackoverflow_d-32.szl \
  engine/language_tests/base/stacktrace.err \
  engine/language_tests/base/stacktrace.out \
  engine/language_tests/base/stacktrace.szl \
  engine/language_tests/base/stacktrace2.err \
  engine/language_tests/base/stacktrace2.out \
  engine/language_tests/base/stacktrace2.szl \
  engine/language_tests/base/statexpr.err \
  engine/language_tests/base/statexpr.out \
  engine/language_tests/base/statexpr.szl \
  engine/language_tests/base/statexpr_bad.err \
  engine/language_tests/base/statexpr_bad.out \
  engine/language_tests/base/statexpr_bad.szl \
  engine/language_tests/base/statexpr_badexec.err \
  engine/language_tests/base/statexpr_badexec.out \
  engine/language_tests/base/statexpr_badexec.szl \
  engine/language_tests/base/static.err \
  engine/language_tests/base/static.out \
  engine/language_tests/base/static.szl \
  engine/language_tests/base/statics2.err \
  engine/language_tests/base/statics2.out \
  engine/language_tests/base/statics2.szl \
  engine/language_tests/base/statics3.err \
  engine/language_tests/base/statics3.out \
  engine/language_tests/base/statics3.szl \
  engine/language_tests/base/statics4.err \
  engine/language_tests/base/statics4.out \
  engine/language_tests/base/statics4.szl \
  engine/language_tests/base/statics5.err \
  engine/language_tests/base/statics5.out \
  engine/language_tests/base/statics5.szl \
  engine/language_tests/base/string.err \
  engine/language_tests/base/string.out \
  engine/language_tests/base/string.szl \
  engine/language_tests/base/stringcompare.err \
  engine/language_tests/base/stringcompare.out \
  engine/language_tests/base/stringcompare.szl \
  engine/language_tests/base/stringconv.err \
  engine/language_tests/base/stringconv.out \
  engine/language_tests/base/stringconv.szl \
  engine/language_tests/base/switch.err \
  engine/language_tests/base/switch.out \
  engine/language_tests/base/switch.szl \
  engine/language_tests/base/switch2.err \
  engine/language_tests/base/switch2.out \
  engine/language_tests/base/switch2.szl \
  engine/language_tests/base/switch3.err \
  engine/language_tests/base/switch3.out \
  engine/language_tests/base/switch3.szl \
  engine/language_tests/base/time.err \
  engine/language_tests/base/time.out \
  engine/language_tests/base/time.szl \
  engine/language_tests/base/timezones1.err \
  engine/language_tests/base/timezones1.out \
  engine/language_tests/base/timezones1.szl \
  engine/language_tests/base/timezones2.err \
  engine/language_tests/base/timezones2.out \
  engine/language_tests/base/timezones2.szl \
  engine/language_tests/base/uint.err \
  engine/language_tests/base/uint.out \
  engine/language_tests/base/uint.szl \
  engine/language_tests/base/unicode.err \
  engine/language_tests/base/unicode.out \
  engine/language_tests/base/unicode.szl \
  engine/language_tests/base/untested.err \
  engine/language_tests/base/untested.out \
  engine/language_tests/base/untested.szl \
  engine/language_tests/base/upperlower.err \
  engine/language_tests/base/upperlower.out \
  engine/language_tests/base/upperlower.szl \
  engine/language_tests/base/utf8.err \
  engine/language_tests/base/utf8.out \
  engine/language_tests/base/utf8.szl \
  engine/language_tests/base/valuepropagation.err \
  engine/language_tests/base/valuepropagation.out \
  engine/language_tests/base/valuepropagation2.err \
  engine/language_tests/base/valuepropagation2.out \
  engine/language_tests/base/valuepropagation3-4.sh \
  engine/language_tests/base/valuepropagation3.err \
  engine/language_tests/base/valuepropagation3.out \
  engine/language_tests/base/valuepropagation3.szl \
  engine/language_tests/base/valuepropagation4.err \
  engine/language_tests/base/valuepropagation4.out \
  engine/language_tests/base/valuepropagation4.szl \
  engine/language_tests/base/valuepropagation5-6.sh \
  engine/language_tests/base/valuepropagation5.err \
  engine/language_tests/base/valuepropagation5.out \
  engine/language_tests/base/valuepropagation5.szl \
  engine/language_tests/base/var_decl.err \
  engine/language_tests/base/valuepropagation6.err \
  engine/language_tests/base/valuepropagation6.out \
  engine/language_tests/base/valuepropagation6.szl \
  engine/language_tests/base/var_decl.out \
  engine/language_tests/base/var_decl.szl \
  engine/language_tests/base/var_decl_bad.err \
  engine/language_tests/base/var_decl_bad.out \
  engine/language_tests/base/var_decl_bad.szl \
  engine/language_tests/base/when.err \
  engine/language_tests/base/when.out \
  engine/language_tests/base/when.szl \
  engine/language_tests/base/whencomplex.err \
  engine/language_tests/base/whencomplex.out \
  engine/language_tests/base/whencomplex.szl \
  engine/language_tests/base/whenmap.err \
  engine/language_tests/base/whenmap.out \
  engine/language_tests/base/whenmap.szl \
  engine/language_tests/base/whenmulti.err \
  engine/language_tests/base/whenmulti.out \
  engine/language_tests/base/whenmulti.szl \
  engine/language_tests/base/whenmultimap.err \
  engine/language_tests/base/whenmultimap.out \
  engine/language_tests/base/whensubstring.err \
  engine/language_tests/base/whensubstring.out \
  engine/language_tests/base/whensubstring.szl \
  engine/language_tests/base/passwd \
  engine/language_tests/base/proto1.proto \
  engine/language_tests/conversion/array_int_bytes.err \
  engine/language_tests/conversion/array_int_bytes.out \
  engine/language_tests/conversion/array_int_bytes.szl \
  engine/language_tests/conversion/basic_explicit_bad.err \
  engine/language_tests/conversion/basic_explicit_bad.out \
  engine/language_tests/conversion/basic_explicit_bad.szl \
  engine/language_tests/conversion/basic_explicit_bad_float.err \
  engine/language_tests/conversion/basic_explicit_bad_float.out \
  engine/language_tests/conversion/basic_explicit_bad_float.szl \
  engine/language_tests/conversion/basic_explicit_bad_int.err \
  engine/language_tests/conversion/basic_explicit_bad_int.out \
  engine/language_tests/conversion/basic_explicit_bad_int.szl \
  engine/language_tests/conversion/basic_explicit_bad_time.err \
  engine/language_tests/conversion/basic_explicit_bad_time.out \
  engine/language_tests/conversion/basic_explicit_bad_time.szl \
  engine/language_tests/conversion/basic_explicit_bad_time_0.err \
  engine/language_tests/conversion/basic_explicit_bad_time_0.out \
  engine/language_tests/conversion/basic_explicit_bad_time_0.szl \
  engine/language_tests/conversion/basic_explicit_bad_time_1.err \
  engine/language_tests/conversion/basic_explicit_bad_time_1.out \
  engine/language_tests/conversion/basic_explicit_bad_time_1.szl \
  engine/language_tests/conversion/basic_explicit_good.err \
  engine/language_tests/conversion/basic_explicit_good.out \
  engine/language_tests/conversion/basic_explicit_good.szl \
  engine/language_tests/conversion/basic_explicit_intbase.err \
  engine/language_tests/conversion/basic_explicit_intbase.out \
  engine/language_tests/conversion/basic_explicit_intbase.szl \
  engine/language_tests/conversion/basic_implicit_assign_bad.err \
  engine/language_tests/conversion/basic_implicit_assign_bad.out \
  engine/language_tests/conversion/basic_implicit_assign_bad.szl \
  engine/language_tests/conversion/basic_implicit_assign_good.err \
  engine/language_tests/conversion/basic_implicit_assign_good.out \
  engine/language_tests/conversion/basic_implicit_assign_good.szl \
  engine/language_tests/conversion/basic_implicit_initialize_bad.err \
  engine/language_tests/conversion/basic_implicit_initialize_bad.out \
  engine/language_tests/conversion/basic_implicit_initialize_bad.szl \
  engine/language_tests/conversion/basic_implicit_initialize_bad_float.err \
  engine/language_tests/conversion/basic_implicit_initialize_bad_float.out \
  engine/language_tests/conversion/basic_implicit_initialize_bad_float.szl \
  engine/language_tests/conversion/basic_implicit_initialize_bad_int.err \
  engine/language_tests/conversion/basic_implicit_initialize_bad_int.out \
  engine/language_tests/conversion/basic_implicit_initialize_bad_int.szl \
  engine/language_tests/conversion/basic_implicit_initialize_bad_time.err \
  engine/language_tests/conversion/basic_implicit_initialize_bad_time.out \
  engine/language_tests/conversion/basic_implicit_initialize_bad_time.szl \
  engine/language_tests/conversion/basic_implicit_initialize_good.err \
  engine/language_tests/conversion/basic_implicit_initialize_good.out \
  engine/language_tests/conversion/basic_implicit_initialize_good.szl \
  engine/language_tests/conversion/basic_test.err \
  engine/language_tests/conversion/basic_test.out \
  engine/language_tests/conversion/basic_test.szl \
  engine/language_tests/conversion/basic_to_composite_initialize_bad.err \
  engine/language_tests/conversion/basic_to_composite_initialize_bad.out \
  engine/language_tests/conversion/basic_to_composite_initialize_bad.szl \
  engine/language_tests/conversion/composite_bad.err \
  engine/language_tests/conversion/composite_bad.out \
  engine/language_tests/conversion/composite_bad.szl \
  engine/language_tests/conversion/composite_implicit_initialize.err \
  engine/language_tests/conversion/composite_implicit_initialize.out \
  engine/language_tests/conversion/composite_implicit_initialize.szl \
  engine/language_tests/conversion/composite_test.err \
  engine/language_tests/conversion/composite_test.out \
  engine/language_tests/conversion/composite_test.szl \
  engine/language_tests/conversion/composite_to_basic_explicit_bad.err \
  engine/language_tests/conversion/composite_to_basic_explicit_bad.out \
  engine/language_tests/conversion/composite_to_basic_explicit_bad.szl \
  engine/language_tests/conversion/composite_to_basic_explicit_good.err \
  engine/language_tests/conversion/incomplete_to_array.err \
  engine/language_tests/conversion/composite_to_basic_explicit_good.out \
  engine/language_tests/conversion/composite_to_basic_explicit_good.szl \
  engine/language_tests/conversion/composite_to_basic_initialize_bad.err \
  engine/language_tests/conversion/composite_to_basic_initialize_bad.out \
  engine/language_tests/conversion/composite_to_basic_initialize_bad.szl \
  engine/language_tests/conversion/composite_to_basic_initialize_good.err \
  engine/language_tests/conversion/composite_to_basic_initialize_good.out \
  engine/language_tests/conversion/composite_to_basic_initialize_good.szl \
  engine/language_tests/conversion/incomplete_to_array.out \
  engine/language_tests/conversion/incomplete_to_array.szl \
  engine/language_tests/conversion/int_bytes_bad.err \
  engine/language_tests/conversion/int_bytes_bad.out \
  engine/language_tests/conversion/int_bytes_bad.szl \
  engine/language_tests/conversion/repeatedbool.err \
  engine/language_tests/conversion/repeatedbool.out \
  engine/language_tests/conversion/repeatedbool.szl \
  engine/language_tests/conversion/string_bad.err \
  engine/language_tests/conversion/string_bad.out \
  engine/language_tests/conversion/string_bad.szl \
  engine/language_tests/conversion/string_good.err \
  engine/language_tests/conversion/string_good.out \
  engine/language_tests/conversion/string_good.szl \
  engine/language_tests/conversion/time_bad_1.err \
  engine/language_tests/conversion/time_bad_1.out \
  engine/language_tests/conversion/time_bad_1.szl \
  engine/language_tests/conversion/time_bad_2.err \
  engine/language_tests/conversion/time_bad_2.out \
  engine/language_tests/conversion/time_bad_2.szl \
  engine/language_tests/conversion/time_good.err \
  engine/language_tests/conversion/time_good.out \
  engine/language_tests/conversion/time_good.szl \
  engine/language_tests/conversion/with_convert_bad0.err \
  engine/language_tests/conversion/with_convert_bad0.out \
  engine/language_tests/conversion/with_convert_bad0.szl \
  engine/language_tests/conversion/with_convert_bad1.err \
  engine/language_tests/conversion/with_convert_bad1.out \
  engine/language_tests/conversion/with_convert_bad1.szl \
  engine/language_tests/conversion/with_convert_good.err \
  engine/language_tests/conversion/with_convert_good.out \
  engine/language_tests/conversion/with_convert_good.szl \
  engine/language_tests/declarations/function_bad.err \
  engine/language_tests/declarations/function_bad.out \
  engine/language_tests/declarations/function_bad.szl \
  engine/language_tests/declarations/function_good.err \
  engine/language_tests/declarations/function_good.out \
  engine/language_tests/declarations/function_good.szl \
  engine/language_tests/emitter/print_all_good.err \
  engine/language_tests/emitter/print_all_good.out \
  engine/language_tests/emitter/print_all_good.szl \
  engine/language_tests/emitter/tables_all_good.err \
  engine/language_tests/emitter/tables_all_good.out \
  engine/language_tests/emitter/tables_all_good.szl \
  engine/language_tests/emitter/tables_bootstrapsum_bad_element.err \
  engine/language_tests/emitter/tables_bootstrapsum_bad_element.out \
  engine/language_tests/emitter/tables_bootstrapsum_bad_element.szl \
  engine/language_tests/emitter/tables_distinctsample_bad_weight.err \
  engine/language_tests/emitter/tables_distinctsample_bad_weight.szl \
  engine/language_tests/emitter/tables_distinctsample_bad_weight.out \
  engine/language_tests/emitter/tables_inversehistogram_bad_weight.szl \
  engine/language_tests/emitter/tables_inversehistogram_bad_weight.err \
  engine/language_tests/emitter/tables_inversehistogram_bad_weight.out \
  engine/language_tests/emitter/tables_maximum_bad_weight.err \
  engine/language_tests/emitter/tables_maximum_bad_weight.out \
  engine/language_tests/emitter/tables_maximum_bad_weight.szl \
  engine/language_tests/emitter/tables_minimum_bad_weight.err \
  engine/language_tests/emitter/tables_minimum_bad_weight.out \
  engine/language_tests/emitter/tables_minimum_bad_weight.szl \
  engine/language_tests/emitter/tables_misc_good.err \
  engine/language_tests/emitter/tables_misc_good.out \
  engine/language_tests/emitter/tables_misc_good.szl \
  engine/language_tests/emitter/tables_mrcounter_bad_element.err \
  engine/language_tests/emitter/tables_mrcounter_bad_element.out \
  engine/language_tests/emitter/tables_mrcounter_bad_element.szl \
  engine/language_tests/emitter/tables_mrcounter_bad_format.err \
  engine/language_tests/emitter/tables_mrcounter_bad_format.out \
  engine/language_tests/emitter/tables_mrcounter_bad_format.szl \
  engine/language_tests/emitter/tables_mrcounter_bad_index.err \
  engine/language_tests/emitter/tables_mrcounter_bad_index.out \
  engine/language_tests/emitter/tables_mrcounter_bad_index.szl \
  engine/language_tests/emitter/tables_quantile_bad_element.szl \
  engine/language_tests/emitter/tables_quantile_bad_element.err \
  engine/language_tests/emitter/tables_quantile_bad_element.out \
  engine/language_tests/emitter/tables_recordio_bad_element.err \
  engine/language_tests/emitter/tables_recordio_bad_element.out \
  engine/language_tests/emitter/tables_recordio_bad_element.szl \
  engine/language_tests/emitter/tables_recordio_bad_index.err \
  engine/language_tests/emitter/tables_recordio_bad_index.out \
  engine/language_tests/emitter/tables_recordio_bad_index.szl \
  engine/language_tests/emitter/tables_runtime_bad_failed.err \
  engine/language_tests/emitter/tables_runtime_bad_failed.out \
  engine/language_tests/emitter/tables_runtime_bad_failed.szl \
  engine/language_tests/emitter/tables_runtime_bad_param_negative.err \
  engine/language_tests/emitter/tables_runtime_bad_param_negative.out \
  engine/language_tests/emitter/tables_runtime_bad_param_negative.szl \
  engine/language_tests/emitter/tables_runtime_bad_param_overflow.err \
  engine/language_tests/emitter/tables_runtime_bad_param_overflow.out \
  engine/language_tests/emitter/tables_runtime_bad_param_overflow.szl \
  engine/language_tests/emitter/tables_runtime_bad_param_undef.err \
  engine/language_tests/emitter/tables_runtime_bad_param_undef.out \
  engine/language_tests/emitter/tables_runtime_bad_param_undef.szl \
  engine/language_tests/emitter/tables_runtime_bad_recursivetypes.err \
  engine/language_tests/emitter/tables_runtime_bad_recursivetypes.out \
  engine/language_tests/emitter/tables_runtime_bad_recursivetypes.szl \
  engine/language_tests/emitter/tables_sum_bad_element.szl \
  engine/language_tests/emitter/tables_sum_bad_format.szl \
  engine/language_tests/emitter/tables_text_bad_element.szl \
  engine/language_tests/emitter/tables_text_bad_index.szl \
  engine/language_tests/emitter/tables_top_bad_weight.szl \
  engine/language_tests/emitter/tables_weightedsample_bad_weight.szl \
  engine/language_tests/emitter/tables_sum_bad_element.err \
  engine/language_tests/emitter/tables_sum_bad_element.out \
  engine/language_tests/emitter/tables_sum_bad_format.err \
  engine/language_tests/emitter/tables_sum_bad_format.out \
  engine/language_tests/emitter/tables_text_bad_element.err \
  engine/language_tests/emitter/tables_text_bad_element.out \
  engine/language_tests/emitter/tables_text_bad_index.err \
  engine/language_tests/emitter/tables_text_bad_index.out \
  engine/language_tests/emitter/tables_top_bad_weight.err \
  engine/language_tests/emitter/tables_top_bad_weight.out \
  engine/language_tests/emitter/tables_weightedsample_bad_weight.err \
  engine/language_tests/emitter/tables_weightedsample_bad_weight.out \
  engine/language_tests/expressions/arithmetic_bad.err \
  engine/language_tests/expressions/arithmetic_bad.out \
  engine/language_tests/expressions/arithmetic_bad.szl \
  engine/language_tests/expressions/arithmetic_good.err \
  engine/language_tests/expressions/arithmetic_good.out \
  engine/language_tests/expressions/arithmetic_good.szl \
  engine/language_tests/expressions/boolean_good.err \
  engine/language_tests/expressions/boolean_good.out \
  engine/language_tests/expressions/boolean_good.szl \
  engine/language_tests/expressions/comparison_good.err \
  engine/language_tests/expressions/comparison_good.out \
  engine/language_tests/expressions/comparison_good.szl \
  engine/language_tests/expressions/function_call_bad.err \
  engine/language_tests/expressions/function_call_bad.out \
  engine/language_tests/expressions/function_call_bad.szl \
  engine/language_tests/expressions/function_call_bad_runtime.err \
  engine/language_tests/expressions/function_call_bad_runtime.out \
  engine/language_tests/expressions/function_call_bad_runtime.szl \
  engine/language_tests/expressions/function_call_good.err \
  engine/language_tests/expressions/function_call_good.out \
  engine/language_tests/expressions/function_call_good.szl \
  engine/language_tests/fixed_crashes/crash0.err \
  engine/language_tests/fixed_crashes/crash0.out \
  engine/language_tests/fixed_crashes/crash0.szl \
  engine/language_tests/fixed_crashes/crash1.err \
  engine/language_tests/fixed_crashes/crash1.out \
  engine/language_tests/fixed_crashes/crash1.szl \
  engine/language_tests/fixed_crashes/crash10.err \
  engine/language_tests/fixed_crashes/crash10.out \
  engine/language_tests/fixed_crashes/crash10.szl \
  engine/language_tests/fixed_crashes/crash11.err \
  engine/language_tests/fixed_crashes/crash11.out \
  engine/language_tests/fixed_crashes/crash11.szl \
  engine/language_tests/fixed_crashes/crash12.err \
  engine/language_tests/fixed_crashes/crash12.out \
  engine/language_tests/fixed_crashes/crash12.szl \
  engine/language_tests/fixed_crashes/crash13.err \
  engine/language_tests/fixed_crashes/crash13.out \
  engine/language_tests/fixed_crashes/crash13.szl \
  engine/language_tests/fixed_crashes/crash14.err \
  engine/language_tests/fixed_crashes/crash14.out \
  engine/language_tests/fixed_crashes/crash14.szl \
  engine/language_tests/fixed_crashes/crash15.err \
  engine/language_tests/fixed_crashes/crash15.out \
  engine/language_tests/fixed_crashes/crash15.szl \
  engine/language_tests/fixed_crashes/crash16.err \
  engine/language_tests/fixed_crashes/crash16.out \
  engine/language_tests/fixed_crashes/crash16.szl \
  engine/language_tests/fixed_crashes/crash17.err \
  engine/language_tests/fixed_crashes/crash17.out \
  engine/language_tests/fixed_crashes/crash17.szl \
  engine/language_tests/fixed_crashes/crash18.err \
  engine/language_tests/fixed_crashes/crash18.out \
  engine/language_tests/fixed_crashes/crash18.szl \
  engine/language_tests/fixed_crashes/crash19.err \
  engine/language_tests/fixed_crashes/crash19.out \
  engine/language_tests/fixed_crashes/crash19.szl \
  engine/language_tests/fixed_crashes/crash2.err \
  engine/language_tests/fixed_crashes/crash2.out \
  engine/language_tests/fixed_crashes/crash2.szl \
  engine/language_tests/fixed_crashes/crash20.err \
  engine/language_tests/fixed_crashes/crash20.out \
  engine/language_tests/fixed_crashes/crash20.szl \
  engine/language_tests/fixed_crashes/crash21.err \
  engine/language_tests/fixed_crashes/crash21.out \
  engine/language_tests/fixed_crashes/crash21.szl \
  engine/language_tests/fixed_crashes/crash22.err \
  engine/language_tests/fixed_crashes/crash22.out \
  engine/language_tests/fixed_crashes/crash22.szl \
  engine/language_tests/fixed_crashes/crash23.err \
  engine/language_tests/fixed_crashes/crash23.in \
  engine/language_tests/fixed_crashes/crash23.out \
  engine/language_tests/fixed_crashes/crash23.szl \
  engine/language_tests/fixed_crashes/crash24.err \
  engine/language_tests/fixed_crashes/crash24.out \
  engine/language_tests/fixed_crashes/crash24.szl \
  engine/language_tests/fixed_crashes/crash3.err \
  engine/language_tests/fixed_crashes/crash3.out \
  engine/language_tests/fixed_crashes/crash3.szl \
  engine/language_tests/fixed_crashes/crash4.err \
  engine/language_tests/fixed_crashes/crash4.out \
  engine/language_tests/fixed_crashes/crash4.szl \
  engine/language_tests/fixed_crashes/crash5.err \
  engine/language_tests/fixed_crashes/crash5.out \
  engine/language_tests/fixed_crashes/crash5.szl \
  engine/language_tests/fixed_crashes/crash6.err \
  engine/language_tests/fixed_crashes/crash6.out \
  engine/language_tests/fixed_crashes/crash6.szl \
  engine/language_tests/fixed_crashes/crash7.err \
  engine/language_tests/fixed_crashes/crash7.out \
  engine/language_tests/fixed_crashes/crash7.szl \
  engine/language_tests/fixed_crashes/crash8.err \
  engine/language_tests/fixed_crashes/crash8.out \
  engine/language_tests/fixed_crashes/crash8.szl \
  engine/language_tests/fixed_crashes/crash9.err \
  engine/language_tests/fixed_crashes/crash9.out \
  engine/language_tests/fixed_crashes/crash9.szl \
  engine/language_tests/for_statements/bad_03.err \
  engine/language_tests/for_statements/bad_03.out \
  engine/language_tests/for_statements/bad_03.szl \
  engine/language_tests/for_statements/bad_04.err \
  engine/language_tests/for_statements/bad_04.out \
  engine/language_tests/for_statements/bad_04.szl \
  engine/language_tests/for_statements/bad_05.err \
  engine/language_tests/for_statements/bad_05.out \
  engine/language_tests/for_statements/bad_05.szl \
  engine/language_tests/for_statements/bad_07.err \
  engine/language_tests/for_statements/bad_07.out \
  engine/language_tests/for_statements/bad_07.szl \
  engine/language_tests/for_statements/bad_08.err \
  engine/language_tests/for_statements/bad_08.out \
  engine/language_tests/for_statements/bad_08.szl \
  engine/language_tests/for_statements/bad_09.err \
  engine/language_tests/for_statements/bad_09.out \
  engine/language_tests/for_statements/bad_09.szl \
  engine/language_tests/for_statements/bad_undef.err \
  engine/language_tests/for_statements/bad_undef.out \
  engine/language_tests/for_statements/bad_undef.szl \
  engine/language_tests/for_statements/good_01.err \
  engine/language_tests/for_statements/good_01.out \
  engine/language_tests/for_statements/good_01.szl \
  engine/language_tests/for_statements/good_02.err \
  engine/language_tests/for_statements/good_02.out \
  engine/language_tests/for_statements/good_02.szl \
  engine/language_tests/for_statements/good_03.err \
  engine/language_tests/for_statements/good_03.out \
  engine/language_tests/for_statements/good_03.szl \
  engine/language_tests/for_statements/good_03_a.err \
  engine/language_tests/for_statements/good_03_a.out \
  engine/language_tests/for_statements/good_03_a.szl \
  engine/language_tests/for_statements/good_04.err \
  engine/language_tests/for_statements/good_04.out \
  engine/language_tests/for_statements/good_04.szl \
  engine/language_tests/for_statements/good_04_a.err \
  engine/language_tests/for_statements/good_04_a.out \
  engine/language_tests/for_statements/good_04_a.szl \
  engine/language_tests/for_statements/good_05.err \
  engine/language_tests/for_statements/good_05.out \
  engine/language_tests/for_statements/good_05.szl \
  engine/language_tests/for_statements/good_06.err \
  engine/language_tests/for_statements/good_06.out \
  engine/language_tests/for_statements/good_06.szl \
  engine/language_tests/for_statements/good_06_a.err \
  engine/language_tests/for_statements/good_06_a.out \
  engine/language_tests/for_statements/good_06_a.szl \
  engine/language_tests/for_statements/good_07.err \
  engine/language_tests/for_statements/good_07.out \
  engine/language_tests/for_statements/good_07.szl \
  engine/language_tests/for_statements/good_08.err \
  engine/language_tests/for_statements/good_08.out \
  engine/language_tests/for_statements/good_08.szl \
  engine/language_tests/for_statements/good_09.err \
  engine/language_tests/for_statements/good_09.out \
  engine/language_tests/for_statements/good_09.szl \
  engine/language_tests/for_statements/good_10_a.err \
  engine/language_tests/for_statements/good_10_a.out \
  engine/language_tests/for_statements/good_10_a.szl \
  engine/language_tests/for_statements/good_10_b.err \
  engine/language_tests/for_statements/good_10_b.out \
  engine/language_tests/for_statements/good_10_b.szl \
  engine/language_tests/for_statements/good_11.err \
  engine/language_tests/for_statements/good_11.out \
  engine/language_tests/for_statements/good_11.szl \
  engine/language_tests/for_statements/good_12.err \
  engine/language_tests/for_statements/good_12.out \
  engine/language_tests/for_statements/good_12.szl \
  engine/language_tests/for_statements/good_13.err \
  engine/language_tests/for_statements/good_13.out \
  engine/language_tests/for_statements/good_13.szl \
  engine/language_tests/for_statements/good_14.err \
  engine/language_tests/for_statements/good_14.out \
  engine/language_tests/for_statements/good_14.szl \
  engine/language_tests/for_statements/good_15.err \
  engine/language_tests/for_statements/good_15.out \
  engine/language_tests/for_statements/good_15.szl \
  engine/language_tests/if_statements/bad.err \
  engine/language_tests/if_statements/bad.out \
  engine/language_tests/if_statements/bad.szl \
  engine/language_tests/if_statements/good.err \
  engine/language_tests/if_statements/good.out \
  engine/language_tests/if_statements/good.szl \
  engine/language_tests/if_statements/good_return_else.err \
  engine/language_tests/if_statements/good_return_else.out \
  engine/language_tests/if_statements/good_return_else.szl \
  engine/language_tests/if_statements/good_return_if.err \
  engine/language_tests/if_statements/good_return_if.out \
  engine/language_tests/if_statements/good_return_if.szl \
  engine/language_tests/if_statements/good_return_ifelse.err \
  engine/language_tests/if_statements/good_return_ifelse.out \
  engine/language_tests/if_statements/good_return_ifelse.szl \
  engine/language_tests/intrinsics/EMAIL_ADDR \
  engine/language_tests/intrinsics/IP_ADDR \
  engine/language_tests/intrinsics/URL_DATA \
  engine/language_tests/intrinsics/format_01.szl \
  engine/language_tests/intrinsics/saw_01.out \
  engine/language_tests/intrinsics/saw_01.szl \
  engine/language_tests/intrinsics/format_bad_01.szl \
  engine/language_tests/intrinsics/resourcestats-32.szl \
  engine/language_tests/intrinsics/resourcestats-64.szl \
  engine/language_tests/intrinsics/saw_02.out \
  engine/language_tests/intrinsics/csv_01.err \
  engine/language_tests/intrinsics/csv_01.out \
  engine/language_tests/intrinsics/csv_01.szl \
  engine/language_tests/intrinsics/csv_02.err \
  engine/language_tests/intrinsics/csv_02.out \
  engine/language_tests/intrinsics/csv_02.szl \
  engine/language_tests/intrinsics/fingerprintof_01.err \
  engine/language_tests/intrinsics/fingerprintof_01.out \
  engine/language_tests/intrinsics/fingerprintof_01.szl \
  engine/language_tests/intrinsics/format_01.err \
  engine/language_tests/intrinsics/format_01.out \
  engine/language_tests/intrinsics/saw_02.szl \
  engine/language_tests/intrinsics/format_bad_01.err \
  engine/language_tests/intrinsics/format_bad_01.out \
  engine/language_tests/intrinsics/saw_03.szl \
  engine/language_tests/intrinsics/len_01.err \
  engine/language_tests/intrinsics/len_01.out \
  engine/language_tests/intrinsics/len_01.szl \
  engine/language_tests/intrinsics/len_bad_01.err \
  engine/language_tests/intrinsics/len_bad_01.out \
  engine/language_tests/intrinsics/len_bad_01.szl \
  engine/language_tests/intrinsics/match_01.err \
  engine/language_tests/intrinsics/match_01.out \
  engine/language_tests/intrinsics/match_01.szl \
  engine/language_tests/intrinsics/math_01.err \
  engine/language_tests/intrinsics/math_01.out \
  engine/language_tests/intrinsics/math_01.szl \
  engine/language_tests/intrinsics/math_02.err \
  engine/language_tests/intrinsics/math_02.out \
  engine/language_tests/intrinsics/math_02.szl \
  engine/language_tests/intrinsics/math_04.err \
  engine/language_tests/intrinsics/math_04.out \
  engine/language_tests/intrinsics/math_04.szl \
  engine/language_tests/intrinsics/math_bad_10.err \
  engine/language_tests/intrinsics/math_bad_10.out \
  engine/language_tests/intrinsics/math_bad_10.szl \
  engine/language_tests/intrinsics/mathwrap.err \
  engine/language_tests/intrinsics/mathwrap.out \
  engine/language_tests/intrinsics/mathwrap.szl \
  engine/language_tests/intrinsics/max_min_01.err \
  engine/language_tests/intrinsics/max_min_01.out \
  engine/language_tests/intrinsics/max_min_01.szl \
  engine/language_tests/intrinsics/regex_01.err \
  engine/language_tests/intrinsics/regex_01.out \
  engine/language_tests/intrinsics/regex_01.szl \
  engine/language_tests/intrinsics/regex_bad_01.err \
  engine/language_tests/intrinsics/regex_bad_01.out \
  engine/language_tests/intrinsics/regex_bad_01.szl \
  engine/language_tests/intrinsics/regex_bad_02.err \
  engine/language_tests/intrinsics/regex_bad_02.out \
  engine/language_tests/intrinsics/regex_bad_02.szl \
  engine/language_tests/intrinsics/regex_bad_03.err \
  engine/language_tests/intrinsics/regex_bad_03.out \
  engine/language_tests/intrinsics/regex_bad_03.szl \
  engine/language_tests/intrinsics/regex_bad_04.err \
  engine/language_tests/intrinsics/regex_bad_04.out \
  engine/language_tests/intrinsics/regex_bad_04.szl \
  engine/language_tests/intrinsics/regex_bad_05.err \
  engine/language_tests/intrinsics/regex_bad_05.out \
  engine/language_tests/intrinsics/regex_bad_05.szl \
  engine/language_tests/intrinsics/regex_bad_06.err \
  engine/language_tests/intrinsics/regex_bad_06.out \
  engine/language_tests/intrinsics/regex_bad_06.szl \
  engine/language_tests/intrinsics/regularExp \
  engine/language_tests/intrinsics/resourcestats-32.err \
  engine/language_tests/intrinsics/resourcestats-32.out \
  engine/language_tests/intrinsics/saw_05.out \
  engine/language_tests/intrinsics/resourcestats-64.err \
  engine/language_tests/intrinsics/resourcestats-64.out \
  engine/language_tests/intrinsics/saw_05.szl \
  engine/language_tests/intrinsics/saw_bad_03.err \
  engine/language_tests/intrinsics/saw_bad_03.out \
  engine/language_tests/intrinsics/saw_bad_03.szl \
  engine/language_tests/intrinsics/sawzall_regExp.out \
  engine/language_tests/intrinsics/saw_01.err \
  engine/language_tests/intrinsics/saw_02.err \
  engine/language_tests/intrinsics/saw_03.err \
  engine/language_tests/intrinsics/saw_03.out \
  engine/language_tests/intrinsics/saw_04.err \
  engine/language_tests/intrinsics/saw_04.out \
  engine/language_tests/intrinsics/saw_04.szl \
  engine/language_tests/intrinsics/saw_05.err \
  engine/language_tests/intrinsics/saw_07.err \
  engine/language_tests/intrinsics/saw_07.out \
  engine/language_tests/intrinsics/saw_07.szl \
  engine/language_tests/intrinsics/saw_08.err \
  engine/language_tests/intrinsics/saw_08.out \
  engine/language_tests/intrinsics/saw_08.szl \
  engine/language_tests/intrinsics/saw_bad_05.err \
  engine/language_tests/intrinsics/saw_bad_05.out \
  engine/language_tests/intrinsics/saw_bad_05.szl \
  engine/language_tests/intrinsics/saw_bad_06.err \
  engine/language_tests/intrinsics/saw_bad_06.out \
  engine/language_tests/intrinsics/saw_bad_06.szl \
  engine/language_tests/intrinsics/saw_bad_07.err \
  engine/language_tests/intrinsics/saw_bad_07.out \
  engine/language_tests/intrinsics/saw_bad_07.szl \
  engine/language_tests/intrinsics/sawzall_regExp.err \
  engine/language_tests/intrinsics/sawzall_regExp.szl \
  engine/language_tests/intrinsics/string_01.err \
  engine/language_tests/intrinsics/string_01.out \
  engine/language_tests/intrinsics/string_01.szl \
  engine/language_tests/intrinsics/string_bad_01.err \
  engine/language_tests/intrinsics/string_bad_01.out \
  engine/language_tests/intrinsics/string_bad_01.szl \
  engine/language_tests/intrinsics/time_01.err \
  engine/language_tests/intrinsics/time_01.out \
  engine/language_tests/intrinsics/time_01.szl \
  engine/language_tests/intrinsics/time_bad_01.err \
  engine/language_tests/intrinsics/time_bad_01.out \
  engine/language_tests/intrinsics/time_bad_01.szl \
  engine/language_tests/intrinsics/time_bad_02.err \
  engine/language_tests/intrinsics/time_bad_02.out \
  engine/language_tests/intrinsics/time_bad_02.szl \
  engine/language_tests/intrinsics/inprotocount.err \
  engine/language_tests/intrinsics/inprotocount.out \
  engine/language_tests/intrinsics/inprotocount.szl \
  engine/language_tests/intrinsics/zipdata \
  engine/language_tests/intrinsics/ziplib.err \
  engine/language_tests/intrinsics/ziplib.out \
  engine/language_tests/intrinsics/ziplib.szl \
  engine/language_tests/limits/float_limit_01.err \
  engine/language_tests/limits/float_limit_01.out \
  engine/language_tests/limits/float_limit_01.szl \
  engine/language_tests/limits/float_limit_02.err \
  engine/language_tests/limits/float_limit_02.out \
  engine/language_tests/limits/float_limit_02.szl \
  engine/language_tests/limits/szl_line_limit.err \
  engine/language_tests/limits/szl_line_limit.out \
  engine/language_tests/limits/szl_line_limit.szl \
  engine/language_tests/misc/array_index.err \
  engine/language_tests/misc/array_index.out \
  engine/language_tests/misc/array_index.szl \
  engine/language_tests/misc/bytes_bad.err \
  engine/language_tests/misc/bytes_bad.out \
  engine/language_tests/misc/bytes_bad.szl \
  engine/language_tests/misc/emit_debug_combination.err \
  engine/language_tests/misc/emit_debug_combination.out \
  engine/language_tests/misc/emit_debug_combination.szl \
  engine/language_tests/misc/empty_lines \
  engine/language_tests/misc/encoding_comment.err \
  engine/language_tests/misc/encoding_comment.out \
  engine/language_tests/misc/encoding_comment.szl \
  engine/language_tests/misc/encoding_string.err \
  engine/language_tests/misc/encoding_string.out \
  engine/language_tests/misc/encoding_string.szl \
  engine/language_tests/misc/exprstat_undef.err \
  engine/language_tests/misc/exprstat_undef.out \
  engine/language_tests/misc/exprstat_undef.szl \
  engine/language_tests/misc/goodlit.err \
  engine/language_tests/misc/goodlit.out \
  engine/language_tests/misc/goodlit.szl \
  engine/language_tests/misc/lineNum_off_01.err \
  engine/language_tests/misc/lineNum_off_01.out \
  engine/language_tests/misc/lineNum_off_01.szl \
  engine/language_tests/misc/lineNum_off_02.err \
  engine/language_tests/misc/lineNum_off_02.out \
  engine/language_tests/misc/lineNum_off_02.szl \
  engine/language_tests/misc/string_as_array_bad1.err \
  engine/language_tests/misc/string_as_array_bad1.out \
  engine/language_tests/misc/string_as_array_bad1.szl \
  engine/language_tests/misc/string_as_array_good1.err \
  engine/language_tests/misc/string_as_array_good1.out \
  engine/language_tests/misc/string_as_array_good1.szl \
  engine/language_tests/operators/arithmetic_bad.err \
  engine/language_tests/operators/arithmetic_bad.out \
  engine/language_tests/operators/arithmetic_bad.szl \
  engine/language_tests/operators/arithmetic_good.err \
  engine/language_tests/operators/arithmetic_good.out \
  engine/language_tests/operators/arithmetic_good.szl \
  engine/language_tests/operators/associativity_01.err \
  engine/language_tests/operators/associativity_01.out \
  engine/language_tests/operators/associativity_01.szl \
  engine/language_tests/operators/associativity_02.err \
  engine/language_tests/operators/associativity_02.out \
  engine/language_tests/operators/associativity_02.szl \
  engine/language_tests/operators/associativity_03.err \
  engine/language_tests/operators/associativity_03.out \
  engine/language_tests/operators/associativity_03.szl \
  engine/language_tests/operators/associativity_04.err \
  engine/language_tests/operators/associativity_04.out \
  engine/language_tests/operators/associativity_04.szl \
  engine/language_tests/operators/associativity_bad_01.err \
  engine/language_tests/operators/associativity_bad_01.out \
  engine/language_tests/operators/associativity_bad_01.szl \
  engine/language_tests/operators/bad_outputvar.err \
  engine/language_tests/operators/bad_outputvar.out \
  engine/language_tests/operators/bad_outputvar.szl \
  engine/language_tests/operators/boolean_bad.err \
  engine/language_tests/operators/boolean_bad.out \
  engine/language_tests/operators/boolean_bad.szl \
  engine/language_tests/operators/boolean_good.err \
  engine/language_tests/operators/boolean_good.out \
  engine/language_tests/operators/boolean_good.szl \
  engine/language_tests/operators/compare_bad.err \
  engine/language_tests/operators/compare_bad.out \
  engine/language_tests/operators/compare_bad.szl \
  engine/language_tests/operators/compare_good.err \
  engine/language_tests/operators/compare_good.out \
  engine/language_tests/operators/compare_good.szl \
  engine/language_tests/operators/precedence_01.err \
  engine/language_tests/operators/precedence_01.out \
  engine/language_tests/operators/precedence_01.szl \
  engine/language_tests/operators/precedence_02.err \
  engine/language_tests/operators/precedence_02.out \
  engine/language_tests/operators/precedence_02.szl \
  engine/language_tests/proto/conversion_parsedmessage_bytes_1.err \
  engine/language_tests/proto/conversion_parsedmessage_bytes_1.out \
  engine/language_tests/proto/conversion_parsedmessage_bytes_1.szl \
  engine/language_tests/proto/conversion_parsedmessage_bytes_2.err \
  engine/language_tests/proto/conversion_parsedmessage_bytes_2.out \
  engine/language_tests/proto/conversion_parsedmessage_bytes_2.szl \
  engine/language_tests/proto/conversion_tuple_bytes_1.err \
  engine/language_tests/proto/conversion_tuple_bytes_1.out \
  engine/language_tests/proto/conversion_tuple_bytes_1.szl \
  engine/language_tests/proto/conversion_tuple_bytes_2.err \
  engine/language_tests/proto/conversion_tuple_bytes_2.out \
  engine/language_tests/proto/conversion_tuple_bytes_2.szl \
  engine/language_tests/proto/conversion_tuple_bytes_good.err \
  engine/language_tests/proto/conversion_tuple_bytes_good.out \
  engine/language_tests/proto/conversion_tuple_bytes_good.szl \
  engine/language_tests/proto/conversion_tuple_bytes_good_1.err \
  engine/language_tests/proto/conversion_tuple_bytes_good_1.out \
  engine/language_tests/proto/conversion_tuple_bytes_good_1.szl \
  engine/language_tests/proto/conversion_tuple_bytes_good_10.err \
  engine/language_tests/proto/conversion_tuple_bytes_good_10.out \
  engine/language_tests/proto/conversion_tuple_bytes_good_10.szl \
  engine/language_tests/proto/conversion_tuple_bytes_good_11.err \
  engine/language_tests/proto/conversion_tuple_bytes_good_11.out \
  engine/language_tests/proto/conversion_tuple_bytes_good_11.szl \
  engine/language_tests/proto/conversion_tuple_bytes_good_2.err \
  engine/language_tests/proto/conversion_tuple_bytes_good_2.out \
  engine/language_tests/proto/conversion_tuple_bytes_good_2.szl \
  engine/language_tests/proto/conversion_tuple_bytes_good_3.err \
  engine/language_tests/proto/conversion_tuple_bytes_good_3.out \
  engine/language_tests/proto/conversion_tuple_bytes_good_3.szl \
  engine/language_tests/proto/conversion_tuple_bytes_good_4.err \
  engine/language_tests/proto/conversion_tuple_bytes_good_4.out \
  engine/language_tests/proto/conversion_tuple_bytes_good_4.szl \
  engine/language_tests/proto/conversion_tuple_bytes_good_5.err \
  engine/language_tests/proto/conversion_tuple_bytes_good_5.out \
  engine/language_tests/proto/conversion_tuple_bytes_good_5.szl \
  engine/language_tests/proto/conversion_tuple_bytes_good_6.err \
  engine/language_tests/proto/conversion_tuple_bytes_good_6.out \
  engine/language_tests/proto/conversion_tuple_bytes_good_6.szl \
  engine/language_tests/proto/conversion_tuple_bytes_good_7.err \
  engine/language_tests/proto/conversion_tuple_bytes_good_7.out \
  engine/language_tests/proto/conversion_tuple_bytes_good_7.szl \
  engine/language_tests/proto/conversion_tuple_bytes_good_8.err \
  engine/language_tests/proto/conversion_tuple_bytes_good_8.out \
  engine/language_tests/proto/conversion_tuple_bytes_good_8.szl \
  engine/language_tests/proto/conversion_tuple_bytes_good_9.err \
  engine/language_tests/proto/conversion_tuple_bytes_good_9.out \
  engine/language_tests/proto/conversion_tuple_bytes_good_9.szl \
  engine/language_tests/proto/default_value_bad_1.err \
  engine/language_tests/proto/default_value_bad_1.out \
  engine/language_tests/proto/default_value_bad_1.szl \
  engine/language_tests/proto/default_value_good_1.err \
  engine/language_tests/proto/default_value_good_1.out \
  engine/language_tests/proto/default_value_good_1.szl \
  engine/language_tests/proto/proto_compiler_bad.proto \
  engine/language_tests/proto/proto_compiler_bad.szl \
  engine/language_tests/proto/proto_compiler_bad.err \
  engine/language_tests/proto/proto_types.err \
  engine/language_tests/proto/proto_tuple_good_1.szl \
  engine/language_tests/proto/no_field_name.err \
  engine/language_tests/proto/no_field_name.out \
  engine/language_tests/proto/no_field_name.szl \
  engine/language_tests/proto/proto_compiler_bad.out \
  engine/language_tests/proto/proto_tuple_bad_1.err \
  engine/language_tests/proto/proto_tuple_bad_1.out \
  engine/language_tests/proto/proto_tuple_bad_1.szl \
  engine/language_tests/proto/proto_tuple_bad_2.err \
  engine/language_tests/proto/proto_tuple_bad_2.out \
  engine/language_tests/proto/proto_tuple_bad_2.szl \
  engine/language_tests/proto/proto_tuple_bad_3.err \
  engine/language_tests/proto/proto_tuple_bad_3.out \
  engine/language_tests/proto/proto_tuple_bad_3.szl \
  engine/language_tests/proto/proto_tuple_bad_4.err \
  engine/language_tests/proto/proto_tuple_bad_4.out \
  engine/language_tests/proto/proto_tuple_bad_4.szl \
  engine/language_tests/proto/proto_tuple_bad_5.err \
  engine/language_tests/proto/proto_tuple_bad_5.out \
  engine/language_tests/proto/proto_tuple_bad_5.szl \
  engine/language_tests/proto/proto_tuple_bad_6.err \
  engine/language_tests/proto/proto_tuple_bad_6.out \
  engine/language_tests/proto/proto_tuple_bad_6.szl \
  engine/language_tests/proto/proto_tuple_bad_7.err \
  engine/language_tests/proto/proto_tuple_bad_7.out \
  engine/language_tests/proto/proto_tuple_bad_7.szl \
  engine/language_tests/proto/proto_tuple_good_1.err \
  engine/language_tests/proto/proto_tuple_good_1.out \
  engine/language_tests/proto/proto_tuple_good_2.err \
  engine/language_tests/proto/proto_tuple_good_2.out \
  engine/language_tests/proto/proto_tuple_good_2.szl \
  engine/language_tests/proto/proto_tuple_good_3.err \
  engine/language_tests/proto/proto_tuple_good_3.out \
  engine/language_tests/proto/proto_tuple_good_3.szl \
  engine/language_tests/proto/proto_types.out \
  engine/language_tests/proto/proto_types.szl \
  engine/language_tests/proto/tag_bad_1.err \
  engine/language_tests/proto/tag_bad_1.out \
  engine/language_tests/proto/tag_bad_1.szl \
  engine/language_tests/proto/tag_bad_2.err \
  engine/language_tests/proto/tag_bad_2.out \
  engine/language_tests/proto/tag_bad_2.szl \
  engine/language_tests/proto/tag_bad_4.err \
  engine/language_tests/proto/tag_bad_4.out \
  engine/language_tests/proto/tag_bad_4.szl \
  engine/language_tests/proto/tag_good_1.err \
  engine/language_tests/proto/tag_good_1.out \
  engine/language_tests/proto/tag_good_1.szl \
  engine/language_tests/proto/type.err \
  engine/language_tests/proto/type.out \
  engine/language_tests/proto/type.szl \
  engine/language_tests/proto/unrecognized_tag.err \
  engine/language_tests/proto/unrecognized_tag.out \
  engine/language_tests/proto/unrecognized_tag.szl \
  engine/language_tests/proto/proto_compiler_bad.proto \
  engine/language_tests/regress \
  engine/language_tests/regular_expression/reg_01.err \
  engine/language_tests/regular_expression/reg_01.out \
  engine/language_tests/regular_expression/reg_01.szl \
  engine/language_tests/regular_expression/reg_02.err \
  engine/language_tests/regular_expression/reg_02.out \
  engine/language_tests/regular_expression/reg_02.szl \
  engine/language_tests/regular_expression/reg_03.err \
  engine/language_tests/regular_expression/reg_03.out \
  engine/language_tests/regular_expression/reg_03.szl \
  engine/language_tests/regular_expression/reg_04.err \
  engine/language_tests/regular_expression/reg_04.out \
  engine/language_tests/regular_expression/reg_04.szl \
  engine/language_tests/regular_expression/reg_05.err \
  engine/language_tests/regular_expression/reg_05.out \
  engine/language_tests/regular_expression/reg_05.szl \
  engine/language_tests/regular_expression/reg_06.err \
  engine/language_tests/regular_expression/reg_06.out \
  engine/language_tests/regular_expression/reg_06.szl \
  engine/language_tests/regular_expression/reg_07.err \
  engine/language_tests/regular_expression/reg_07.out \
  engine/language_tests/regular_expression/reg_07.szl \
  engine/language_tests/regular_expression/reg_08.err \
  engine/language_tests/regular_expression/reg_08.out \
  engine/language_tests/regular_expression/reg_08.szl \
  engine/language_tests/regular_expression/reg_09.err \
  engine/language_tests/regular_expression/reg_09.out \
  engine/language_tests/regular_expression/reg_09.szl \
  engine/language_tests/regular_expression/reg_10.err \
  engine/language_tests/regular_expression/reg_10.out \
  engine/language_tests/regular_expression/reg_10.szl \
  engine/language_tests/regular_expression/reg_11.err \
  engine/language_tests/regular_expression/reg_11.out \
  engine/language_tests/regular_expression/reg_11.szl \
  engine/language_tests/regular_expression/reg_12.err \
  engine/language_tests/regular_expression/reg_12.out \
  engine/language_tests/regular_expression/reg_12.szl \
  engine/language_tests/regular_expression/reg_13.err \
  engine/language_tests/regular_expression/reg_13.out \
  engine/language_tests/regular_expression/reg_13.szl \
  engine/language_tests/regular_expression/reg_14.err \
  engine/language_tests/regular_expression/reg_14.out \
  engine/language_tests/regular_expression/reg_14.szl \
  engine/language_tests/regular_expression/reg_15.err \
  engine/language_tests/regular_expression/reg_15.out \
  engine/language_tests/regular_expression/reg_15.szl \
  engine/language_tests/regular_expression/reg_16.err \
  engine/language_tests/regular_expression/reg_16.out \
  engine/language_tests/regular_expression/reg_16.szl \
  engine/language_tests/regular_expression/reg_bad_01.err \
  engine/language_tests/regular_expression/reg_bad_01.out \
  engine/language_tests/regular_expression/reg_bad_01.szl \
  engine/language_tests/regular_expression/reg_bad_02.err \
  engine/language_tests/regular_expression/reg_bad_02.out \
  engine/language_tests/regular_expression/reg_bad_02.szl \
  engine/language_tests/regular_expression/reg_bad_03.err \
  engine/language_tests/regular_expression/reg_bad_03.out \
  engine/language_tests/regular_expression/reg_bad_03.szl \
  engine/language_tests/regular_expression/reg_bad_04.err \
  engine/language_tests/regular_expression/reg_bad_04.out \
  engine/language_tests/regular_expression/reg_bad_04.szl \
  engine/language_tests/regular_expression/reg_bad_05.err \
  engine/language_tests/regular_expression/reg_bad_05.out \
  engine/language_tests/regular_expression/reg_bad_05.szl \
  engine/language_tests/regular_expression/reg_bad_06.err \
  engine/language_tests/regular_expression/reg_bad_06.out \
  engine/language_tests/regular_expression/reg_bad_06.szl \
  engine/language_tests/regular_expression/reg_bad_07.err \
  engine/language_tests/regular_expression/reg_bad_07.out \
  engine/language_tests/regular_expression/reg_bad_07.szl \
  engine/language_tests/regular_expression/reg_bad_08.err \
  engine/language_tests/regular_expression/reg_bad_08.out \
  engine/language_tests/regular_expression/reg_bad_08.szl \
  engine/language_tests/regular_expression/reg_bad_09.err \
  engine/language_tests/regular_expression/reg_bad_09.out \
  engine/language_tests/regular_expression/reg_bad_09.szl \
  engine/language_tests/regular_expression/reg_bad_10.err \
  engine/language_tests/regular_expression/reg_bad_10.out \
  engine/language_tests/regular_expression/reg_bad_10.szl \
  engine/language_tests/regular_expression/reg_bad_11.err \
  engine/language_tests/regular_expression/reg_bad_11.out \
  engine/language_tests/regular_expression/reg_bad_11.szl \
  engine/language_tests/regular_expression/reg_bad_12.err \
  engine/language_tests/regular_expression/reg_bad_12.out \
  engine/language_tests/regular_expression/reg_bad_12.szl \
  engine/language_tests/regular_expression/reg_bad_13.err \
  engine/language_tests/regular_expression/reg_bad_13.out \
  engine/language_tests/regular_expression/reg_bad_13.szl \
  engine/language_tests/regular_expression/reg_bad_14.err \
  engine/language_tests/regular_expression/reg_bad_14.out \
  engine/language_tests/regular_expression/reg_bad_14.szl \
  engine/language_tests/regular_expression/reg_bad_15.err \
  engine/language_tests/regular_expression/reg_bad_15.out \
  engine/language_tests/regular_expression/reg_bad_15.szl \
  engine/language_tests/regular_expression/reg_bad_16.err \
  engine/language_tests/regular_expression/reg_bad_16.out \
  engine/language_tests/regular_expression/reg_bad_16.szl \
  engine/language_tests/regular_expression/reg_bad_17.err \
  engine/language_tests/regular_expression/reg_bad_17.out \
  engine/language_tests/regular_expression/reg_bad_17.szl \
  engine/language_tests/regular_expression/reg_bad_18.err \
  engine/language_tests/regular_expression/reg_bad_18.out \
  engine/language_tests/regular_expression/reg_bad_18.szl \
  engine/language_tests/regular_expression/reg_bad_19.err \
  engine/language_tests/regular_expression/reg_bad_19.out \
  engine/language_tests/regular_expression/reg_bad_19.szl \
  engine/language_tests/regular_expression/reg_bad_20.err \
  engine/language_tests/regular_expression/reg_bad_20.out \
  engine/language_tests/regular_expression/reg_bad_20.szl \
  engine/language_tests/regular_expression/reg_bad_21.err \
  engine/language_tests/regular_expression/reg_bad_21.out \
  engine/language_tests/regular_expression/reg_bad_21.szl \
  engine/language_tests/regular_expression/reg_bad_22.err \
  engine/language_tests/regular_expression/reg_bad_22.out \
  engine/language_tests/regular_expression/reg_bad_22.szl \
  engine/language_tests/regular_expression/reg_bad_23.err \
  engine/language_tests/regular_expression/reg_bad_23.out \
  engine/language_tests/regular_expression/reg_bad_23.szl \
  engine/language_tests/regular_expression/reg_bad_24.err \
  engine/language_tests/regular_expression/reg_bad_24.out \
  engine/language_tests/regular_expression/reg_bad_24.szl \
  engine/language_tests/regular_expression/reg_bad_25.err \
  engine/language_tests/regular_expression/reg_bad_25.out \
  engine/language_tests/regular_expression/reg_bad_25.szl \
  engine/language_tests/regular_expression/reg_bad_26.err \
  engine/language_tests/regular_expression/reg_bad_26.out \
  engine/language_tests/regular_expression/reg_bad_26.szl \
  engine/language_tests/regular_expression/reg_bad_27.err \
  engine/language_tests/regular_expression/reg_bad_27.out \
  engine/language_tests/regular_expression/reg_bad_27.szl \
  engine/language_tests/regular_expression/reg_bad_28.err \
  engine/language_tests/regular_expression/reg_bad_28.out \
  engine/language_tests/regular_expression/reg_bad_28.szl \
  engine/language_tests/regular_expression/reg_bad_29.err \
  engine/language_tests/regular_expression/reg_bad_29.out \
  engine/language_tests/regular_expression/reg_bad_29.szl \
  engine/language_tests/regular_expression/reg_bad_30.err \
  engine/language_tests/regular_expression/reg_bad_30.out \
  engine/language_tests/regular_expression/reg_bad_30.szl \
  engine/language_tests/update \
  engine/language_tests/sort/sort_01.err \
  engine/language_tests/sort/sort_01.out \
  engine/language_tests/sort/sort_01.szl \
  engine/language_tests/sort/sort_02.err \
  engine/language_tests/sort/sort_02.out \
  engine/language_tests/sort/sort_02.szl \
  engine/language_tests/sort/sort_03.err \
  engine/language_tests/sort/sort_03.out \
  engine/language_tests/sort/sort_03.szl \
  engine/language_tests/sort/sort_04.err \
  engine/language_tests/sort/sort_04.out \
  engine/language_tests/sort/sort_04.szl \
  engine/language_tests/sort/sort_bad_01.err \
  engine/language_tests/sort/sort_bad_01.out \
  engine/language_tests/sort/sort_bad_01.szl \
  engine/language_tests/sort/sort_bad_02.err \
  engine/language_tests/sort/sort_bad_02.out \
  engine/language_tests/sort/sort_bad_02.szl \
  engine/language_tests/sort/sort_bad_03.err \
  engine/language_tests/sort/sort_bad_03.out \
  engine/language_tests/sort/sort_bad_03.szl \
  engine/language_tests/sort/sort_bad_04.err \
  engine/language_tests/sort/sort_bad_04.out \
  engine/language_tests/sort/sort_bad_04.szl \
  engine/language_tests/switch_statements/array_bad.err \
  engine/language_tests/switch_statements/array_bad.out \
  engine/language_tests/switch_statements/array_bad.szl \
  engine/language_tests/switch_statements/bad_1.err \
  engine/language_tests/switch_statements/bad_1.out \
  engine/language_tests/switch_statements/bad_1.szl \
  engine/language_tests/switch_statements/good_1.err \
  engine/language_tests/switch_statements/good_1.out \
  engine/language_tests/switch_statements/good_1.szl \
  engine/language_tests/switch_statements/good_2.err \
  engine/language_tests/switch_statements/good_2.out \
  engine/language_tests/switch_statements/good_2.szl \
  engine/language_tests/switch_statements/map_bad.err \
  engine/language_tests/switch_statements/map_bad.out \
  engine/language_tests/switch_statements/map_bad.szl \
  engine/language_tests/switch_statements/tuple.err \
  engine/language_tests/switch_statements/tuple.out \
  engine/language_tests/switch_statements/tuple.szl \
  engine/language_tests/switch_statements/undefined_tag.err \
  engine/language_tests/switch_statements/undefined_tag.out \
  engine/language_tests/switch_statements/undefined_tag.szl \
  engine/language_tests/setpaths \
  engine/language_tests/type_declarations/array.err \
  engine/language_tests/type_declarations/array.out \
  engine/language_tests/type_declarations/array.szl \
  engine/language_tests/type_declarations/array_baddecl.err \
  engine/language_tests/type_declarations/array_baddecl.out \
  engine/language_tests/type_declarations/array_baddecl.szl \
  engine/language_tests/type_declarations/array_badoperation.err \
  engine/language_tests/type_declarations/array_badoperation.out \
  engine/language_tests/type_declarations/array_badoperation.szl \
  engine/language_tests/type_declarations/declarations_arrays_depth2.err \
  engine/language_tests/type_declarations/declarations_arrays_depth2.out \
  engine/language_tests/type_declarations/declarations_arrays_depth2.szl \
  engine/language_tests/type_declarations/declarations_depth1.err \
  engine/language_tests/type_declarations/declarations_depth1.out \
  engine/language_tests/type_declarations/declarations_depth1.szl \
  engine/language_tests/type_declarations/declarations_maps_depth2.err \
  engine/language_tests/type_declarations/declarations_maps_depth2.out \
  engine/language_tests/type_declarations/declarations_maps_depth2.szl \
  engine/language_tests/type_declarations/declarations_tuples_depth2.err \
  engine/language_tests/type_declarations/declarations_tuples_depth2.out \
  engine/language_tests/type_declarations/declarations_tuples_depth2.szl \
  engine/language_tests/type_declarations/declare_array.err \
  engine/language_tests/type_declarations/declare_array.out \
  engine/language_tests/type_declarations/declare_array.szl \
  engine/language_tests/type_declarations/equality_bad.err \
  engine/language_tests/type_declarations/equality_bad.out \
  engine/language_tests/type_declarations/equality_bad.szl \
  engine/language_tests/type_declarations/equality_depth1_bad.err \
  engine/language_tests/type_declarations/equality_depth1_bad.out \
  engine/language_tests/type_declarations/equality_depth1_bad.szl \
  engine/language_tests/type_declarations/equality_depth1_good.err \
  engine/language_tests/type_declarations/equality_depth1_good.out \
  engine/language_tests/type_declarations/equality_depth1_good.szl \
  engine/language_tests/type_declarations/equality_good.err \
  engine/language_tests/type_declarations/equality_good.out \
  engine/language_tests/type_declarations/equality_good.szl \
  engine/language_tests/type_declarations/equality_var_decl.err \
  engine/language_tests/type_declarations/equality_var_decl.out \
  engine/language_tests/type_declarations/equality_var_decl.szl \
  engine/language_tests/type_declarations/file_10 \
  engine/language_tests/type_declarations/file_11 \
  engine/language_tests/type_declarations/file_11.ref \
  engine/language_tests/type_declarations/map.err \
  engine/language_tests/type_declarations/map.out \
  engine/language_tests/type_declarations/map.szl \
  engine/language_tests/type_declarations/non_unique_words.ref \
  engine/language_tests/type_declarations/numbytes.ref \
  engine/language_tests/type_declarations/numchars.ref \
  engine/language_tests/type_declarations/numlines.ref \
  engine/language_tests/type_declarations/numwords.ref \
  engine/language_tests/type_declarations/outputType_bad.err \
  engine/language_tests/type_declarations/outputType_bad.out \
  engine/language_tests/type_declarations/outputType_bad.szl \
  engine/language_tests/type_declarations/outputType_bad_file.err \
  engine/language_tests/type_declarations/outputType_bad_file.out \
  engine/language_tests/type_declarations/outputType_bad_file.szl \
  engine/language_tests/type_declarations/outputType_bad_restrict.err \
  engine/language_tests/type_declarations/outputType_bad_restrict.out \
  engine/language_tests/type_declarations/outputType_bad_restrict.szl \
  engine/language_tests/type_declarations/outputType_good.err \
  engine/language_tests/type_declarations/outputType_good.out \
  engine/language_tests/type_declarations/outputType_good.szl \
  engine/language_tests/type_declarations/outputType_good_fileops.err \
  engine/language_tests/type_declarations/outputType_good_fileops.out \
  engine/language_tests/type_declarations/outputType_good_fileops.szl \
  engine/language_tests/type_declarations/redeclare_basic.err \
  engine/language_tests/type_declarations/redeclare_basic.out \
  engine/language_tests/type_declarations/redeclare_basic.szl \
  engine/language_tests/type_declarations/static_bad.err \
  engine/language_tests/type_declarations/static_bad.out \
  engine/language_tests/type_declarations/static_bad.szl \
  engine/language_tests/type_declarations/static_good.err \
  engine/language_tests/type_declarations/static_good.out \
  engine/language_tests/type_declarations/static_good.szl \
  engine/language_tests/type_declarations/tuple_tag_bad_1.szl \
  engine/language_tests/type_declarations/tuple.err \
  engine/language_tests/type_declarations/tuple.out \
  engine/language_tests/type_declarations/tuple.szl \
  engine/language_tests/type_declarations/tuple_big.err \
  engine/language_tests/type_declarations/tuple_big.out \
  engine/language_tests/type_declarations/tuple_big.szl \
  engine/language_tests/type_declarations/tuple_tag_bad_1.err \
  engine/language_tests/type_declarations/tuple_tag_bad_1.out \
  engine/language_tests/type_declarations/tuple_tag_bad_2.err \
  engine/language_tests/type_declarations/tuple_tag_bad_2.out \
  engine/language_tests/type_declarations/tuple_tag_bad_2.szl \
  engine/language_tests/type_declarations/tuple_tag_good_1.err \
  engine/language_tests/type_declarations/tuple_tag_good_1.out \
  engine/language_tests/type_declarations/tuple_tag_good_1.szl \
  engine/language_tests/type_declarations/unique_word_count.ref \
  engine/language_tests/type_declarations/variables.err \
  engine/language_tests/type_declarations/variables.out \
  engine/language_tests/type_declarations/variables.szl \
  engine/language_tests/type_declarations/variables_bad.err \
  engine/language_tests/type_declarations/variables_bad.out \
  engine/language_tests/type_declarations/variables_bad.szl \
  engine/language_tests/type_declarations/variables_good.err \
  engine/language_tests/type_declarations/variables_good.out \
  engine/language_tests/type_declarations/variables_good.szl \
  engine/language_tests/type_declarations/variables_redeclare.err \
  engine/language_tests/type_declarations/variables_redeclare.out \
  engine/language_tests/type_declarations/variables_redeclare.szl \
  engine/language_tests/type_declarations/variables_reserved.err \
  engine/language_tests/type_declarations/variables_reserved.out \
  engine/language_tests/type_declarations/variables_reserved.szl \
  engine/language_tests/unreachable/format_reference.err \
  engine/language_tests/unreachable/format_reference.out \
  engine/language_tests/unreachable/format_reference.szl \
  engine/language_tests/unreachable/reachable.err \
  engine/language_tests/unreachable/reachable.out \
  engine/language_tests/unreachable/reachable.szl \
  engine/language_tests/unreachable/static_initializers.err \
  engine/language_tests/unreachable/static_initializers.out \
  engine/language_tests/unreachable/static_initializers.szl \
  engine/language_tests/unreachable/tables.err \
  engine/language_tests/unreachable/tables.out \
  engine/language_tests/unreachable/tables.szl \
  engine/language_tests/unreachable/unreachable.err \
  engine/language_tests/unreachable/unreachable.out \
  engine/language_tests/unreachable/unreachable.szl \
  engine/language_tests/unreachable/unused_fields.err \
  engine/language_tests/unreachable/unused_fields.out \
  engine/language_tests/unreachable/unused_fields.szl \
  engine/language_tests/when_statements/bad.err \
  engine/language_tests/when_statements/bad.out \
  engine/language_tests/when_statements/bad.szl \
  engine/language_tests/when_statements/good.err \
  engine/language_tests/when_statements/good.out \
  engine/language_tests/when_statements/good.szl \
  engine/language_tests/while_statements/bad.err \
  engine/language_tests/while_statements/bad.out \
  engine/language_tests/while_statements/bad.szl \
  engine/language_tests/while_statements/bad_undef.err \
  engine/language_tests/while_statements/bad_undef.out \
  engine/language_tests/while_statements/bad_undef.szl \
  engine/language_tests/while_statements/good.err \
  engine/language_tests/while_statements/good.out \
  engine/language_tests/while_statements/good.szl \
  engine/language_tests/while_statements/good_return.err \
  engine/language_tests/while_statements/good_return.out \
  engine/language_tests/while_statements/good_return.szl \
  fmt/tests/fmt_test.err \
  fmt/tests/fmt_test.out

##### Miscellaneious non-code

docdatafiles = \
  fmt/LICENSE \
  fmt/NOTICE \
  fmt/README \
  fmt/README.google \
  fmt/fmtinstallflag.3 \
  fmt/print.3


##### Targets and tests

lib_LTLIBRARIES = libszl.la libszlemitters.la libszlintrinsics.la

bin_PROGRAMS = protoc-gen-szl szl

check_PROGRAMS = \
  $(app_test_programs) \
  $(engine_test_programs) \
  $(elfgen_test_program) \
  $(emitter_test_programs) \
  $(fmt_test_programs) \
  $(emitvalues_test_programs) \
  $(intrinsics_test_programs)

TESTS = \
  $(app_tests) \
  $(engine_tests) \
  $(elfgen_test) \
  $(emitter_tests) \
  $(fmt_tests) \
  $(emitvalues_tests) \
  $(intrinsics_tests)

dist_noinst_DATA = $(testdatafiles) $(docdatafiles)
