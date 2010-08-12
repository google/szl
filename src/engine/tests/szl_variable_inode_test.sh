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

# Tests that include handling uses realpath check as a fallback to device/i-no
# check used to idenitfy duplicate files. This is necessary with systems where
# file's inode is not guaranteed to stay constant (e.g. srcfs).
#
# This test needs to be separate from szl_regtest because it will dynamically
# generate a large number of includes and then change the inode of the 
# include file. We don't want this to interfere with other tests.
# 

if [[ "$0" == */* ]] ; then
  cd "${0%/*}/"
fi

if [ -z "$SZL" ] ; then
  SZL="szl"                
fi

if [ -z "$SZL_TMP" ] ; then
  SZL_TMP="."                
fi

change_inode() {
  filename=$1
  cp -f ${filename} ${filename}.tmp
  mv -f ${filename}.tmp ${filename}
}

rm -f "$SZL_TMP"/x.szl "$SZL_TMP"/results "$SZL_TMP"/szl_test.szl
# Construct the file whose inode will change.
echo "type x = { a: int, b: int };" > "$SZL_TMP"/x.szl

# Construct a test script which consists of nothing but 1000 includes of x.szl.
for i in `seq 1 1000`; do
  echo include \"x.szl\" >> "$SZL_TMP"/szl_test.szl
done

for i in `seq 1 10`; do
  "$SZL" "$SZL_TMP"/szl_test.szl \
    --szl_includepath="$SZL_TMP" 2>> "$SZL_TMP"/results &

  for j in `seq 1 100`; do
    change_inode "$SZL_TMP"/x.szl
  done
done

wait

if grep -q redeclaration "$SZL_TMP"/results ; then
  echo "FAIL - failed in szl changing inode test" >&2
else
  echo PASS
fi

rm -f "$SZL_TMP"/x.szl "$SZL_TMP"/results "$SZL_TMP"/szl_test.szl
