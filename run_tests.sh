#!/bin/sh
# JUCE不要のDSPテストを g++/clang++ で一括ビルド・実行する(Linux/macOS/WSL)。
set -e
cd "$(dirname "$0")"
mkdir -p build_tests
fail=0
for t in PitchDetector NoteSegmenter PitchShifter PitchEditor VibratoTiming Session Util; do
  ${CXX:-g++} -O2 -std=c++17 -Wall -Wextra Tests/${t}Test.cpp -o build_tests/${t}Test
  echo "=== ${t}Test ==="
  ./build_tests/${t}Test || fail=1
done
exit $fail
