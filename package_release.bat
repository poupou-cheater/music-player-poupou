@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "CUR_VER=1.0.0"
for /f "tokens=2 delims==" %%A in ('findstr /C:"APP_VERSION" "src\library\Models.hpp"') do set "RAW_VER=%%A"
if defined RAW_VER (
    for /f "tokens=1 delims=;" %%B in ("!RAW_VER!") do set "CUR_VER=%%~B"
    set "CUR_VER=!CUR_VER: =!"
    set "CUR_VER=!CUR_VER:"=!"
)

set "VERSION=%~1"
if "%VERSION%"=="" (
    set /p VERSION="Version (actuelle: %CUR_VER%, ex: 1.0.1) [Entree = %CUR_VER%]: "
)
if "%VERSION%"=="" set "VERSION=%CUR_VER%"

set "CLEAN_VER=%VERSION%"
if /i "%VERSION:~0,1%"=="v" set "CLEAN_VER=%VERSION:~1%"

echo [1/4] Mise a jour de APP_VERSION = "%CLEAN_VER%" dans src\library\Models.hpp...
set "NEW_VER=%CLEAN_VER%"
powershell -NoProfile -Command "$p='src\library\Models.hpp'; $q=[char]34; $c=[IO.File]::ReadAllText($p); $pat='(inline constexpr const char\*\s+APP_VERSION\s*=\s*'+$q+')[^'+$q+']*('+$q+';)'; $n=[regex]::Replace($c, $pat, { param($m) $m.Groups[1].Value + $env:NEW_VER + $m.Groups[2].Value }, 1); if ($n -notmatch ('APP_VERSION\s*=\s*'+$q+[regex]::Escape($env:NEW_VER)+$q)) { exit 1 }; [IO.File]::WriteAllText($p, $n, (New-Object System.Text.UTF8Encoding($false)))"
if errorlevel 1 (
    echo Erreur mise a jour de APP_VERSION dans src\library\Models.hpp !
    if "%~1"=="" pause
    exit /b 1
)

set "CMAKE_EXE=cmake"
where cmake >nul 2>&1
if errorlevel 1 (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    ) else if exist "C:\Program Files\CMake\bin\cmake.exe" (
        set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"
    )
)

echo [2/4] Arret de StreamPulse.exe s'il est ouvert...
taskkill /F /IM StreamPulse.exe >nul 2>&1

echo [3/4] Compilation Release...
"%CMAKE_EXE%" --build build --config Release -j 8
if errorlevel 1 (
    echo Erreur de compilation !
    if "%~1"=="" pause
    exit /b 1
)

echo [4/4] Creation du package dist\StreamPulse-v%CLEAN_VER%-win64.zip...
if not exist "dist" mkdir "dist"
if exist "dist\_stage" rmdir /S /Q "dist\_stage"
mkdir "dist\_stage"

copy /Y "build\Release\StreamPulse.exe" "dist\_stage\StreamPulse.exe" >nul
if exist "README.md" copy /Y "README.md" "dist\_stage\README.md" >nul
if exist "tools" xcopy /E /I /Y "tools" "dist\_stage\tools" >nul

set "ZIP_PATH=dist\StreamPulse-v%CLEAN_VER%-win64.zip"
if exist "%ZIP_PATH%" del /F /Q "%ZIP_PATH%"

powershell -NoProfile -Command "Compress-Archive -Path 'dist\_stage\*' -DestinationPath '%ZIP_PATH%' -Force"
rmdir /S /Q "dist\_stage"

if exist "%ZIP_PATH%" (
    echo.
    echo OK : %ZIP_PATH% pret ! ^(APP_VERSION=%CLEAN_VER%^)
) else (
    echo Echec creation du zip.
    if "%~1"=="" pause
    exit /b 1
)
if "%~1"=="" pause
endlocal
