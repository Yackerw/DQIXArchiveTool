@echo off
setlocal ENABLEDELAYEDEXPANSION

REM Build gp2.js/gp2.wasm using Docker Desktop + emscripten/emsdk
REM No server is started; this only compiles and writes artifacts to .\public

set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%
for %%i in ("%SCRIPT_DIR%\..") do set PARENT_DIR=%%~fi


if not exist "%SCRIPT_DIR%\public" (
  mkdir "%SCRIPT_DIR%\public"
)

echo Building WebAssembly via Emscripten in Docker...
docker run --rm -v "%PARENT_DIR%:/work" -w /work emscripten/emsdk:3.1.56 bash -lc "emcc ArchiveTool/CompressA.cpp ArchiveTool/CompressB.cpp ArchiveTool/CompressC.cpp ArchiveTool/Reader.cpp ArchiveTool/gp2.cpp ArchiveTool/wasm_bridge.cpp -std=c++17 -O3 -sASSERTIONS=1 -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=1 -sEXPORTED_FUNCTIONS=['_ProcessFile'] -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS'] -sNO_EXIT_RUNTIME=1 -sINVOKE_RUN=0 -o /work/web/public/gp2.js"


if %ERRORLEVEL% NEQ 0 (
  echo Build failed.
  exit /b 1
)

echo.
echo Build complete. Files generated:
echo   %SCRIPT_DIR%\public\gp2.js
echo   %SCRIPT_DIR%\public\gp2.wasm
echo.
echo Usage:
echo   - Open public\index.html in a modern browser, or host /public on GitHub Pages.
echo   - Drag-and-drop a GP2 file to process locally in your browser. No uploads.
echo.

endlocal
