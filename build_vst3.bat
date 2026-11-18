@echo off
rem ============================================================
rem  VOCAL TUNE STUDIO - Windows one-shot build script
rem  Usage:  build_vst3.bat            (JUCEをGitHubから取得してビルド)
rem          build_vst3.bat C:\JUCE    (ローカルのJUCEフォルダを使う=オフライン可)
rem ============================================================
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul || (echo [ERROR] CMake not found. Install CMake 3.22+ and add it to PATH. & exit /b 1)

set JUCE_ARG=
if not "%~1"=="" (
  if not exist "%~1\CMakeLists.txt" (echo [ERROR] "%~1" is not a JUCE source folder. & exit /b 1)
  set JUCE_ARG=-DFETCHCONTENT_SOURCE_DIR_JUCE="%~1" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
)

echo.
echo === 1/4 Configure ===
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 %JUCE_ARG% || (echo [ERROR] Configure failed. & exit /b 1)

echo.
echo === 2/4 Build DSP tests + CLI ===
cmake --build build --config Release --target vts_cli PitchDetectorTest NoteSegmenterTest PitchShifterTest PitchEditorTest VibratoTimingTest SessionTest UtilTest || (echo [ERROR] Test build failed. & exit /b 1)

echo.
echo === 3/4 Run DSP tests ===
ctest --test-dir build -C Release --output-on-failure || (echo [ERROR] DSP tests failed. & exit /b 1)

echo.
echo === 4/4 Build VST3 ===
cmake --build build --config Release --target VocalTuneStudio_VST3 || (echo [ERROR] VST3 build failed. Send me the first error lines. & exit /b 1)

set OUT=build\VocalTuneStudio_artefacts\Release\VST3\VOCAL TUNE STUDIO.vst3
echo.
echo ============================================================
echo  Done. VST3 bundle:
echo    %OUT%
echo  Install: copy the whole "VOCAL TUNE STUDIO.vst3" folder to
echo    C:\Program Files\Common Files\VST3\   (administrator rights needed)
echo ============================================================
endlocal
