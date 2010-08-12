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

# Common code to generate test programs for value and definedness propagation.

# First argument is the file name to be emitted in expected warning messages.
# Second argument is whether to ignore undefs.
# The test program is written to stdout; the expected error output to stderr.

# The state of a variable is one of:
#   0 unknown
#   1 known undefined
#   2 known defined
#   3 1st known value
#   4 2nd known value
# The state change is one of:
#   0 no change (because there is no reference to the variable)
#   1 unknown change
#   2 now undefined
#   3 reference
#      under --noignore_undefs, now known defined but no value change
#      under --ignore_undefs, expect no change to the state
#   4 now defined to 1st value
#   5 now defined to 2nd value
# Under "--noignore_undefs" change 3 should not be possible when the
# initial state is 1 (known undefined).

# The tests track the final computed state of a variable for a given
# initial state and two state changes, e.g. different uses of a variable
# in the "then" and "else" part of an "if" statement.  Since all the variables
# are independent, they are grouped together so that the first state change
# for all variables is one block and the second state change for all
# variables is on another block.

# Variables in the generated tests are named "xijk", e.g. "x123".
# The digits indicate what combination of initial state and state changes
# will be applied to this variable in the test.
#   x  is the letter "x" as a common base for the generated variable names
#   i  is a digit in [0-4] indicating the initial state of this variable
#   j  is a digit in [0-5] indicating the way in which the state of this
#        variable may be changed in the first block of the test code
#   k  is a digit in [0-5] indicating the way in which the state of this
#        variable may be changed in the second block of the test code

# Currently we assume that a function that modifies a variable outside the
# function's scope (not necessarily global) might cause the variable to
# become undefined.  This is a very unusual case and so this assumption
# is very conservative.  We might want to consider keeping track of whether
# a given function can ever cause any outer scope variable to become
# undefined.  If we make that change, we will need to update this test
# to include an explicit ___undefine() in the "unknown change" case,
# else we will not consider that the variable could become undefined.
# We might also then split "unknown change" into "changed to an unknown
# defined value" and "change to an unknown, possibly undefined value".


# Emits a single line and increments "line_number".

function emit_line {
  echo "$1"
  ((line_number++))
}


# Merges two alternative states, yielding a result on stdout.

function merge_alternative_states {
  # Both $1 and $2 are variable states.
  if (( $1 == $2 )) ; then
    echo $1  # same => that state
  elif (( $1 >= 2 && $2 >= 2 )) ; then
    echo 2  # known defined, known value (either order) => known defined
  else
    echo 0  # all other cases => unknown
  fi
}


# Applies a state change to a state, yielding a new state on stdout.

function apply_state_change {
  local state=$1    # old state
  local change=$2   # change to apply
  if (( $change == 0 )) ; then
    echo $state  # no change
  elif (( $change == 1 )) ; then
    echo 0   # now unknown
  elif (( $change == 2 )) ; then
    echo 1   # now undefined
  elif (( $change == 3 )) ; then
    if $ignore_undefs ; then
      echo $state  # has no effect
    else
      if (( $state == 3  || $state == 4 )) ; then
        echo $state # same value as before
      else
        echo 2  # defined, but do not know value
      fi
    fi
  elif (( $change == 4 )) ; then
    echo 3
  else # (( $change == 5 )) ; then
    echo 4
  fi
}


# Emits the expected output from checking the state of a variable.
# Uses the global variables $filename and $line_number.

function emit_var_err() {   # args: var to check, its expected state
  local var=$1    # the variable to check
  local state=$2  # the expected state
  if (( $state == 0 )) ; then
    # unknown; no warnings
    ((line_number++))
    ((line_number++))
    ((line_number++))
  elif (( $state == 1 )) ; then
    # known undefined
    ((line_number++))
    echo "$filename:$line_number: warning: unnecessary def(): argument has value ($var) which is known to be undefined"
    ((line_number++))
    echo "$filename:$line_number: warning: variable $var will always have an undefined value at this point"
    ((line_number++))
    # with --noignore_undefs never get undef warning on 3rd line
    if $ignore_undefs ; then
      echo "$filename:$line_number: warning: variable $var will always have an undefined value at this point"
    fi
  elif (( $state == 2)) ; then
    # known defined
    ((line_number++))
    echo "$filename:$line_number: warning: unnecessary def(): argument has value ($var) which is known to be defined"
    ((line_number++))
    ((line_number++))
  elif (( $state == 3)) ; then
    # known defined as first value
    ((line_number++))
    echo "$filename:$line_number: warning: unnecessary def(): argument has value (123) which is known to be defined"
    ((line_number++))
    echo "$filename:$line_number: warning: divide by zero"
    ((line_number++))
  elif (( $state == 4)) ; then
    # known defined as second value
    ((line_number++))
    echo "$filename:$line_number: warning: unnecessary def(): argument has value (456) which is known to be defined"
    ((line_number++))
    ((line_number++))
    echo "$filename:$line_number: warning: divide by zero"
  else
    # should not reach here
    echo "Problem in the script" >&2
    exit 1
  fi
}
