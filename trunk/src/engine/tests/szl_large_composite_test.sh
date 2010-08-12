#! /bin/bash

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

# Test the Sawzall compiler on large composite values that are initialized
# in pieces.  The tests should pass in limited stack space.
#
# This test is separate from szl_regtest because it dynamically generates
# some large .szl files and limits the stack. We don't want this to interfere
# with other tests.

if [[ "$0" == */* ]] ; then
  cd "${0%/*}/"
fi

if [ -z "$SZL" ] ; then
  SZL="szl"                
fi

if [ -z "$SZL_TMP" ] ; then
  SZL_TMP="."                
fi

# Limit stack space to 64 KB
ulimit -S -s 64

PASSED=1

# compare actual results with expected results
function compare {
  if ! cmp -s "$SZL_TMP"/test.err "$SZL_TMP"/$1.err; then
    echo regress: stderr for $1 $2
    diff "$SZL_TMP"/test.err "$SZL_TMP"/$1.err
    PASSED=0
  fi
  if ! cmp -s "$SZL_TMP"/test.out "$SZL_TMP"/$1.out; then
    echo regress: stdout for $1 $2
    diff "$SZL_TMP"/test.out "$SZL_TMP"/$1.out
    PASSED=0
  fi
}

# Construct test inputs on the fly to avoid checking in large files.

function seq {
  for (( i = $1; i <= $2; i++ )) ; do
    echo $i
  done
}

# A large array of 100,000 ints.

rm -f "$SZL_TMP"/large_array.szl "$SZL_TMP"/large_array.out \
      "$SZL_TMP"/large_array.err
echo "static arr: array of int = {" >> "$SZL_TMP"/large_array.szl
for i in "" `seq 1 9999`; do
  for j in `seq 0 9`; do
    echo -n "${i}${j}, " >> "$SZL_TMP"/large_array.szl
  done
  echo "" >> "$SZL_TMP"/large_array.szl
done
echo "};" >> "$SZL_TMP"/large_array.szl
echo "emit stdout <- string(arr[0]);" >> "$SZL_TMP"/large_array.szl
echo "emit stdout <- string(arr[50000]);" >> "$SZL_TMP"/large_array.szl
echo "emit stdout <- string(arr[99999]);" >> "$SZL_TMP"/large_array.szl

echo "0"     >> "$SZL_TMP"/large_array.out
echo "50000" >> "$SZL_TMP"/large_array.out
echo "99999" >> "$SZL_TMP"/large_array.out

touch "$SZL_TMP"/large_array.err

"$SZL" --native "$SZL_TMP"/large_array.szl \
  > "$SZL_TMP"/test.out 2> "$SZL_TMP"/test.err
if [ $? != 0 ]; then
  echo "Failed test of large array with native code generation."
  PASSED=0
fi
compare large_array native

"$SZL" --nonative "$SZL_TMP"/large_array.szl \
  > "$SZL_TMP"/test.out 2> "$SZL_TMP"/test.err
if [ $? != 0 ]; then
  echo "Failed test of large array with interpreter."
  PASSED=0
fi
compare large_array nonative

# A large tuple with 10,000 int elements.  We settle for 10,000 because
# really large tuples are prohibitively expensive to parse.

rm -f "$SZL_TMP"/large_tuple.szl "$SZL_TMP"/large_tuple.out \
      "$SZL_TMP"/large_tuple.err
echo "type tuple_type = {" >> "$SZL_TMP"/large_tuple.szl
for i in "" `seq 1 999`; do
  for j in `seq 0 9`; do
    echo -n "e${i}${j}: int, " >> "$SZL_TMP"/large_tuple.szl
  done
  echo "" >> "$SZL_TMP"/large_tuple.szl
done
echo "};" >> "$SZL_TMP"/large_tuple.szl

echo "static t: tuple_type = {" >> "$SZL_TMP"/large_tuple.szl
for i in "" `seq 1 999`; do
  for j in `seq 0 9`; do
    echo -n "${i}${j}, " >> "$SZL_TMP"/large_tuple.szl
  done
  echo "" >> "$SZL_TMP"/large_tuple.szl
done
echo "};" >> "$SZL_TMP"/large_tuple.szl
echo "emit stdout <- string(t.e0);" >> "$SZL_TMP"/large_tuple.szl
echo "emit stdout <- string(t.e5000);" >> "$SZL_TMP"/large_tuple.szl
echo "emit stdout <- string(t.e9999);" >> "$SZL_TMP"/large_tuple.szl

echo "0"    >> "$SZL_TMP"/large_tuple.out
echo "5000" >> "$SZL_TMP"/large_tuple.out
echo "9999" >> "$SZL_TMP"/large_tuple.out

touch "$SZL_TMP"/large_tuple.err

$"$SZL" --native "$SZL_TMP"/large_tuple.szl \
  > "$SZL_TMP"/test.out 2> "$SZL_TMP"/test.err
if [ $? != 0 ]; then
  echo "Failed test of large tuple with native code generation."
  PASSED=0
fi
compare large_tuple native

$"$SZL" --nonative "$SZL_TMP"/large_tuple.szl \
  > "$SZL_TMP"/test.out 2> "$SZL_TMP"/test.err
if [ $? != 0 ]; then
  echo "Failed test of large tuple with interpreter."
  PASSED=0
fi
compare large_tuple nonative

# A large map with 100,000 ints mapping to strings.

rm -f "$SZL_TMP"/large_map.szl "$SZL_TMP"/large_map.out \
      "$SZL_TMP"/large_map.err
echo "static m: map [int] of string = {" >> "$SZL_TMP"/large_map.szl
for i in "" `seq 1 9999`; do
  for j in `seq 0 9`; do
    echo -n "${i}${j}:\"${i}${j}\", " >> "$SZL_TMP"/large_map.szl
  done
  echo "" >> "$SZL_TMP"/large_map.szl
done
echo "};" >> "$SZL_TMP"/large_map.szl
echo "emit stdout <- m[0];" >> "$SZL_TMP"/large_map.szl
echo "emit stdout <- m[50000];" >> "$SZL_TMP"/large_map.szl
echo "emit stdout <- m[99999];" >> "$SZL_TMP"/large_map.szl

echo "0"     >> "$SZL_TMP"/large_map.out
echo "50000" >> "$SZL_TMP"/large_map.out
echo "99999" >> "$SZL_TMP"/large_map.out

touch "$SZL_TMP"/large_map.err

$"$SZL" --native "$SZL_TMP"/large_map.szl \
  > "$SZL_TMP"/test.out 2> "$SZL_TMP"/test.err
if [ $? != 0 ]; then
  echo "Failed test of large map with native code generation."
  PASSED=0
fi
compare large_map native

$"$SZL" --nonative "$SZL_TMP"/large_map.szl \
  > "$SZL_TMP"/test.out 2> "$SZL_TMP"/test.err
if [ $? != 0 ]; then
  echo "Failed test of large map with interpreter."
  PASSED=0
fi
compare large_map nonative

if [ $PASSED == 1 ]; then
  echo "PASS"
  exit 0
else
  echo "FAIL"
  exit 1
fi

for x in test.{err,out} large_array.{szl,err,out} \
    large_tuple.{szl,err,out} large_map.{szl,err,out} ; do
  rm -f "$SZL_TMP"/$x
done
