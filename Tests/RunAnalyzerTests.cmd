@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0.."
if not defined JUCE_MODULES set "JUCE_MODULES=%USERPROFILE%\JUCE\modules"
if not exist "%JUCE_MODULES%\juce_core\juce_core.h" (
    echo Set JUCE_MODULES to your JUCE modules directory.
    exit /b 1
)
if not defined VSCMD_VER (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
    if not defined VSINSTALL exit /b 1
    call "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat"
    if errorlevel 1 exit /b 1
)
msbuild "Builds\VisualStudio2022\SimpleEQ.sln" /m:2 /p:Configuration=Debug /p:Platform=x64 /verbosity:minimal /nologo
if errorlevel 1 exit /b 1
set "TESTOUT=%TEMP%\SimpleEQ-AnalyzerTests"
if not exist "%TESTOUT%" mkdir "%TESTOUT%"
cl /nologo /std:c++20 /EHsc /MDd /D_DEBUG /DDEBUG /DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 /DJUCE_STRICT_REFCOUNTEDPOINTER=1 /I"Source" /I"JuceLibraryCode" /I"%JUCE_MODULES%" /FI"JucePluginDefines.h" "Tests\AnalyzerIntegrationTests.cpp" /Fo"%TESTOUT%\AnalyzerIntegrationTests.obj" /Fe"%TESTOUT%\AnalyzerIntegrationTests.exe" /link "Builds\VisualStudio2022\x64\Debug\Shared Code\SimpleEQ.lib" shell32.lib ole32.lib oleaut32.lib gdi32.lib comdlg32.lib /INCREMENTAL:NO
if errorlevel 1 exit /b 1
"%TESTOUT%\AnalyzerIntegrationTests.exe" "%TESTOUT%\analyzer.png"
exit /b %ERRORLEVEL%
