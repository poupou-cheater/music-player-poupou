@echo off
setlocal enabledelayedexpansion

set "VERSION=%~1"
if "%VERSION%"=="" (
    set /p VERSION="Version (ex: v1.0.0-beta.1) [Entree = v1.0.0-beta.1]: "
)
if "%VERSION%"=="" set "VERSION=v1.0.0-beta.1"

set "CMAKE_EXE=cmake"
where cmake >nul 2>&1
if errorlevel 1 (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    ) else if exist "C:\Program Files\CMake\bin\cmake.exe" (
        set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"
    )
)

echo [1/3] Arret de StreamPulse.exe s'il est ouvert...
taskkill /F /IM StreamPulse.exe >nul 2>&1

echo [2/3] Compilation Release...
"%CMAKE_EXE%" --build build --config Release -j 8
if errorlevel 1 (
    echo Erreur de compilation !
    exit /b 1
)

echo [3/3] Creation du package dist\StreamPulse-%VERSION%-win64.zip...
if not exist "dist" mkdir "dist"
if exist "dist\_stage" rmdir /S /Q "dist\_stage"
mkdir "dist\_stage"

copy /Y "build\Release\StreamPulse.exe" "dist\_stage\StreamPulse.exe" >nul
if exist "README.md" copy /Y "README.md" "dist\_stage\README.md" >nul
if exist "tools" xcopy /E /I /Y "tools" "dist\_stage\tools" >nul

set "ZIP_PATH=dist\StreamPulse-%VERSION%-win64.zip"
if exist "%ZIP_PATH%" del /F /Q "%ZIP_PATH%"

powershell -NoProfile -Command "Compress-Archive -Path 'dist\_stage\*' -DestinationPath '%ZIP_PATH%' -Force"
rmdir /S /Q "dist\_stage"

if exist "%ZIP_PATH%" (
    echo.
    echo OK : %ZIP_PATH% pret !
) else (
    echo Echec creation du zip.
    exit /b 1
)
endlocal
