#!/usr/bin/env bash
# Builds and runs the hand_sim and deck_tags tests with the MSVC compiler (run from edopro-src)
set -euo pipefail
cd "$(dirname "$0")/.."
VC="C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat"
cat > build-tools/run_tests.bat <<EOF
@echo off
call "$VC" >nul
cl /nologo /std:c++17 /EHsc /O2 build-tools\\test_hand_sim.cpp gframe\\hand_sim.cpp /Fe:build-tools\\test_hand_sim.exe /Fo:build-tools\\ >nul || exit /b 1
if exist gframe\\deck_tags.cpp cl /nologo /std:c++17 /EHsc /O2 build-tools\\test_deck_tags.cpp gframe\\deck_tags.cpp gframe\\hand_sim.cpp /Fe:build-tools\\test_deck_tags.exe /Fo:build-tools\\ >nul || exit /b 1
EOF
cmd //c "build-tools\\run_tests.bat"
./build-tools/test_hand_sim.exe
[ -f build-tools/test_deck_tags.exe ] && ./build-tools/test_deck_tags.exe || true
