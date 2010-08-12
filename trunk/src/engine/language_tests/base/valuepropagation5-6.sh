#!/bin/bash

# Copyright 2010 Google Inc.
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#      http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ------------------------------------------------------------------------

# Generate test program for value and definedness propagation in "loops".
# This script must be executed in the "testdata/base" directory.
# It generates the files valuepropagation{3,4}.{szl,err,out}

# See "valuepropagation.sh" for general comments.

source ./valuepropagation.sh

# We do not get warnings within the loop for x113 and x131 because
# of the loose check on function calls modifying variables outside their
# scope; in these cases the existence of a function that modifies the variable,
# plus a function call before the reference, makes us assume the variable
# could have been modified.

# If we start tracking whether functions ever undefine outer variables,
# the test will need to include an explicit ___undefine() in the "unknown"
# case, else we will not consider that the variable could become undefined.

# Internally, the script uses stdout (file descriptor 1) to write the
# test program (.szl file) and file descriptor 3 to write the expected
# error output (.err file).  Two distinct descriptors are needed because
# there is overlap between the script code that generates each part.
# (The script uses stderr in the normal way to report script errors.)


# Emits a variable declaration for each combination to be tested.

function emit_initial_state() {
  list=('z:int=unknown;' 'z:int;' 'z:int=known;' 'z:int=123;' 'z:int=456;')
  for i in 0 1 2 3 4; do
    for j in 0 1 2 3 4 5 ; do
      if $ignore_undefs || (( $i != 1 || $j != 3 )) ; then
        for k in 0 1 2 3 4 5 ; do
          if $ignore_undefs || (( $i != 1 || $k != 3 )) ; then
            emit_line "$1${list[$i]//z/x$i$j$k}"
          fi
        done
      fi
    done
  done
}
 

# Emits code for the tested state changes in the "then" part of the first "if".

function emit_first_alternative {
  list=('' 'function(){z=unknown;}();' '___undefine(z);' 'z;' 'z=123;' 'z=456;')
  for i in 0 1 2 3 4 ; do
    for j in 1 2 3 4 5 ; do
      if $ignore_undefs || (( $i != 1 || $j != 3 )) ; then
        for k in 0 1 2 3 4 5 ; do
          if $ignore_undefs || (( $i != 1 || $k != 3 )) ; then
            emit_line "$1${list[$j]//z/x$i$j$k}"
            if $ignore_undefs &&
                (( $i == 1 && $j == 3 && k != 1 && k != 4 && k != 5 )) ; then
              echo "$filename:$line_number: warning: variable x$i$j$k will" \
                   "always have an undefined value at this point" >&3
            fi
          fi
        done
      fi
    done
  done
}


# Emits code for the tested state changes in the "then" part of the second "if".

function emit_second_alternative {
  list=('' 'function(){z=unknown;}();' '___undefine(z);' 'z;' 'z=123;' 'z=456;')
  for i in 0 1 2 3 4 ; do
    for j in 0 1 2 3 4 5 ; do
      if $ignore_undefs || (( $i != 1 || $j != 3 )) ; then
        for k in 1 2 3 4 5 ; do
          if $ignore_undefs || (( $i != 1 || $k != 3 )) ; then
            emit_line "$1${list[$k]//z/x$i$j$k}"
            if $ignore_undefs &&
                (( $i == 1 && $k == 3 && j != 1 && j != 4 && j != 5 )) ; then
              echo "$filename:$line_number: warning: variable x$i$j$k will" \
                   "always have an undefined value at this point" >&3
            fi
          fi
        done
      fi
    done
  done
}


# Emits code to test the value of each variable and emits the expected output.

function emit_checks {
  for i in 0 1 2 3 4 ; do
    for j in 0 1 2 3 4 5 ; do
      if $ignore_undefs || (( $i != 1 || $j != 3 )) ; then
        for k in 0 1 2 3 4 5 ; do
          if $ignore_undefs || (( $i != 1 || $k != 3 )) ; then
            local var=x$i$j$k

	    # Emit code to test the value of the variable.
	    # Line numbers are incremented in "emit_var_err".

            echo "${1}def($var);"
            echo "${1}1/($var-123);"
            echo "${1}1/($var-456);"

            # Emit the expected error output.
            # Each alternative may be executed zero or more times in any order.
            # We simulate that by optionally executing each alternative three
	    # times accounting for both consecutive and alternating cases.

            local state temp
            state=$i
            temp=`apply_state_change $state $j`
            state=`merge_alternative_states $state $temp`
            temp=`apply_state_change $state $k`
            state=`merge_alternative_states $state $temp`
            temp=`apply_state_change $state $j`
            state=`merge_alternative_states $state $temp`
            temp=`apply_state_change $state $k`
            state=`merge_alternative_states $state $temp`
            temp=`apply_state_change $state $k`
            state=`merge_alternative_states $state $temp`
            temp=`apply_state_change $state $j`
            state=`merge_alternative_states $state $temp`
            temp=`apply_state_change $state $j`
            state=`merge_alternative_states $state $temp`
            emit_var_err $var $state >&3
          fi
        done
      fi
    done
  done
}

# Emit the test code.

function emit_test() {
  line_number=0  # global
  emit_line "# This test automatically generated by ${0/*\/}, do not modify."
  emit_line
  emit_line "static known: int = nrand(2);"
  emit_line "unknown: int = 1 / nrand(2);"
  emit_line
  emit_line "f: function() {"
  emit_line
  emit_line "  # Put each variable in state 'i' from its name 'xijk'."
  emit_line
  emit_initial_state "  "
  emit_line
  emit_line "  # Use a loop to test state propagation from the previous loop."
  emit_line
  emit_line "  while (nrand(2) != 0) {"
  emit_line
  emit_line "    if (nrand(2) == 0) {"
  emit_line
  emit_line "      # Change each variable's state based on 'j' in 'xijk'."
  emit_line
  emit_first_alternative "      "
  emit_line "    }"
  emit_line
  emit_line "    if (nrand(2) == 0) {"
  emit_line
  emit_line "      # Change each variable's state based on 'k' in 'xijk'."
  emit_line
  emit_second_alternative "      "
  emit_line "    }"
  emit_line "  }"
  emit_line
  emit_checks "  "
  emit_line "};"
}


# Emit tests and expected error output for normal and ignore_undefs cases.
# Some expected error output is emitted while generating the test code,
# so it is captured through fd4 at that stage.

function emit() {
  filename=base/$1.szl
  ignore_undefs=$2
  emit_test > $1.szl 3> $1.err
  echo -n > $1.out
}

emit valuepropagation5 false
emit valuepropagation6 true
