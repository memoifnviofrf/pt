#!/bin/sh
# 本物のJUCEなしで、プラグイン層の自分のコードを構文チェックする(スタブ使用)。
cd "$(dirname "$0")/../.."
fail=0
for f in Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/UI/PitchEditorView.cpp Source/AudioFileManager.cpp Source/ProjectManager.cpp; do
  echo "--- $f"
  ${CXX:-g++} -std=c++17 -fsyntax-only -Wall -Wextra -D_USE_MATH_DEFINES -ITools/syntax_check -ISource "$f" || fail=1
done
exit $fail
