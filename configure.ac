## Process this file with autoconf to produce configure.
## In general, the safest way to proceed is to run ./autogen.sh

AC_PREREQ(2.64)

# Note: if you change the version, you must also update it in public/porting.h
AC_INIT([Szl],[1.0],[szl-users@googlegroups.com],[szl])
AC_CONFIG_SRCDIR(src/app/szl.cc)
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE

# Checks for standard programs.
AC_PROG_CC
AC_PROG_CXX
AC_LANG([C++])
AC_USE_SYSTEM_EXTENSIONS
AM_CONDITIONAL(GCC, test "$GCC" = yes)   # let the Makefile know if we're gcc

AC_MSG_CHECKING([C++ compiler flags...])
AS_IF([test "x${ac_cv_env_CXXFLAGS_set}" = "x"],[
  AS_IF([test "$GCC" = "yes"],[
    SZL_OPT_FLAG="-O2 -fno-strict-aliasing"
    WARN_FLAGS="-Wall -Wwrite-strings -Woverloaded-virtual -Wno-sign-compare"
    CXXFLAGS="${CXXFLAGS} -g ${WARN_FLAGS}"
  ])

  # Szl contains several checks that are intended to be used only
  # for debugging and which might hurt performance.  Most users are probably
  # end users who don't want these checks, so add -DNDEBUG by default.
  # Also turn off warnings about signed vs unsigned comparisons.
  CXXFLAGS="$CXXFLAGS -DNDEBUG"

  AC_MSG_RESULT([use default: $SZL_OPT_FLAG $CXXFLAGS])
],[
  AC_MSG_RESULT([use user-supplied: $CXXFLAGS])
])

AC_SUBST(SZL_OPT_FLAG)

SZL_EXTRA_LDFLAGS="";
AX_CHECK_LINKER_FLAGS([-Wl,--no-as-needed],
        [SZL_EXTRA_LDFLAGS="-Wl,--no-as-needed"], [])
AC_SUBST([SZL_EXTRA_LDFLAGS])
 

AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_INSTALL
AC_PROG_LIBTOOL 

# Set up defines for endianness.
szl_big_endian=1
szl_little_endian=2
AC_C_BIGENDIAN(szl_byte_order=${szl_big_endian}, szl_byte_order=${szl_little_endian}, szl_byte_order=0, szl_byte_order=0)
SZL_DEFINES="-DSZL_BIG_ENDIAN=${szl_big_endian} -DSZL_LITTLE_ENDIAN=${szl_little_endian} -DSZL_BYTE_ORDER=${szl_byte_order}"
AC_SUBST(SZL_DEFINES)

# Check for protoc
AC_PATH_PROG([PROTOC], [protoc])
AS_IF([test "x$PROTOC" = "x"],
      [AC_MSG_FAILURE([no protocol compiler was found])])

# Check for objdump
AC_PATH_PROG([OBJDUMP], [objdump])
AS_IF([test "x$OBJDUMP" = "x"],
      [AC_MSG_FAILURE([no objdump utility was found (required for elfgen_unittest)])])


# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h float.h inttypes.h limits.h malloc.h memory.h stdint.h stdlib.h string.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_OFF_T
AC_C_RESTRICT
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_TYPE_UINTPTR_T
AC_CHECK_TYPES([ptrdiff_t])

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([dup2 getcwd gethostname getpagesize gettimeofday memchr memmove memset munmap pow realpath setenv sqrt strcasecmp strchr strdup strerror strrchr strstr strtol strtoul strtoull sysinfo])


# Special handling of pthread.
ACX_PTHREAD
CFLAGS="$PTHREAD_CFLAGS $CFLAGS"
CXXFLAGS="$PTHREAD_CFLAGS $CXXFLAGS"
LIBS="$PTHREAD_LIBS $LIBS"

# Special handling of hash/set clases.
AC_CXX_STL_HASH


# Check for PCRE library.
AC_SEARCH_LIBS([pcre_compile], [pcre], [],
	       [AC_MSG_FAILURE([PCRE library missing])])

# Check for MD5 in openssl library.
AC_SEARCH_LIBS([MD5_Init], [crypto], [],
	       [AC_MSG_FAILURE([SSL crypto library (needed for MD5) missing])])

# Check for ICU librares.
AC_MSG_CHECKING([for ICU unicode library])
LIBS="-licuuc $LIBS"
AC_LINK_IFELSE(
  AC_LANG_PROGRAM(
      [[#include <unicode/ustring.h>]],
      [[u_strToUTF8(NULL, 0, NULL, NULL, 0, NULL);]]
  ),
  [AC_MSG_RESULT([yes])],
  [AC_MSG_FAILURE([no working ICU unicode library was found])]
)
AC_MSG_CHECKING([for ICU timezone library])
LIBS="-licui18n $LIBS"
AC_LINK_IFELSE(
  AC_LANG_PROGRAM(
      [[#include <unicode/timezone.h>]],
      [[icu::TimeZone::createTimeZone("UTF");]]
  ),
  [AC_MSG_RESULT([yes])],
  [AC_MSG_FAILURE([no working ICU timezone library was found])]
)

# Check for mallinfo
AC_SEARCH_LIBS([mallinfo], [malloc])

# Check protobuf header version.
AC_MSG_CHECKING([protobuf header version])
AC_COMPILE_IFELSE(
  AC_LANG_PROGRAM([[
      #include <google/protobuf/stubs/common.h>
      #if !defined(GOOGLE_PROTOBUF_VERSION) || \
                   (GOOGLE_PROTOBUF_VERSION < 2003000)
      # error protobuf version too old
      #endif
      ]], []),
  [AC_MSG_RESULT([yes (2.3.0 or later)])],
  [AC_MSG_FAILURE([protocol buffer headers missing or too old (requires 2.3.0)])]
)

# Check for protobuf library.
AC_MSG_CHECKING([for protobuf library])
LIBS="-lprotobuf $LIBS"
AC_LINK_IFELSE(
  AC_LANG_PROGRAM([[
      #include <google/protobuf/io/zero_copy_stream_impl_lite.h>
      ]],[[
      int data;
      // requires vtable from library
      google::protobuf::io::ArrayOutputStream ais(&data, 1);
      ]]),
  [AC_MSG_RESULT([yes])],
  [AC_MSG_FAILURE([no working protocol buffer library was found])]
)


# Check for protocol compiler library.
AC_MSG_CHECKING([for protocol compiler library])
LIBS="-lprotoc $LIBS"
AC_LINK_IFELSE(
  AC_LANG_PROGRAM([[
      #include <google/protobuf/compiler/plugin.h>
      ]],[[
      google::protobuf::compiler::PluginMain(0, NULL, NULL);
      ]]),
  [AC_MSG_RESULT([yes])],
  [AC_MSG_FAILURE([no working protocol compiler library was found])]
)


# Check for zlib header version and for zlib.
AC_MSG_CHECKING([zlib version])
AC_COMPILE_IFELSE(
  AC_LANG_PROGRAM([[
      #include <zlib.h>
      #if !defined(ZLIB_VERNUM) || (ZLIB_VERNUM < 0x1204)
      # error zlib version too old
      #endif
      ]], []), [
    AC_MSG_RESULT([yes (1.2.0.4 or later)])

    # Also need to add -lz to the linker flags and make sure this succeeds.
    AC_SEARCH_LIBS([zlibVersion], [z], [ ],
		 [AC_MSG_FAILURE([no working zlib library was found])
    ])
  ], [
    AC_MSG_FAILURE([headers missing or too old (requires 1.2.0.4)])
])


# OS-dependent checks
AC_MSG_CHECKING([is linux])
AS_IF([grep -q "^linux" <<< "$target_os"],
  [AC_DEFINE([OS_LINUX], [], [see VirtualProcessSize in utilities/sysutils.cc])
   AC_MSG_RESULT([linux; --memory_limit should work])
   AC_DEFINE([SZL_USES_MEMORY_LIMIT], [], [--memory_imit and GC work])
   AM_CONDITIONAL(SZL_USES_MEMORY_LIMIT,true)
   AM_CONDITIONAL(ELFGEN_UNITTEST,true)
  ],[
   AC_MSG_RESULT([not linux; --memory_limit will not work])
   AM_CONDITIONAL(SZL_USES_MEMORY_LIMIT,false)
   AM_CONDITIONAL(ELFGEN_UNITTEST,false)
  ]
)
AC_MSG_CHECKING([is OS X])
AS_IF([grep -q "^darwin" <<< "$target_os"],
  [AC_DEFINE([OS_MACOSX], [], [see utilities/port_ieee.h])
   AC_MSG_RESULT([mac osx; will declare ieee754_float])
  ],[
   AC_MSG_RESULT([not mac osx; will get ieee754_float from include files])
  ]
)

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
