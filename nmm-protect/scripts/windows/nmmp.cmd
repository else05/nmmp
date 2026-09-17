@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "CALL_DIR=%CD%"
set "SCRIPT_DIR=%~dp0"
set "APK="
set "RULES="
set "MAPPING="
set "ENV_FILE="

if "%~1"=="" goto :help

:parse_args
if "%~1"=="" goto :load_configuration
if /I "%~1"=="-h" goto :help
if /I "%~1"=="--help" goto :help
if /I "%~1"=="/?" goto :help
if /I "%~1"=="-r" goto :read_rules
if /I "%~1"=="--rules" goto :read_rules
if /I "%~1"=="-m" goto :read_mapping
if /I "%~1"=="--mapping" goto :read_mapping
if /I "%~1"=="-e" goto :read_env
if /I "%~1"=="--env" goto :read_env
if not defined APK (
    set "APK=%~1"
    shift
    goto :parse_args
)
echo [protect] Unexpected positional argument: %~1
exit /b 2

:read_rules
if "%~2"=="" (
    echo [protect] Missing value after %~1.
    exit /b 2
)
set "RULES=%~2"
shift
shift
goto :parse_args

:read_mapping
if "%~2"=="" (
    echo [protect] Missing value after %~1.
    exit /b 2
)
set "MAPPING=%~2"
shift
shift
goto :parse_args

:read_env
if "%~2"=="" (
    echo [protect] Missing value after %~1.
    exit /b 2
)
set "ENV_FILE=%~2"
shift
shift
goto :parse_args

:load_configuration
if not defined APK (
    echo [protect] Missing APK path.
    exit /b 2
)

if not defined JAVA_EXE set "JAVA_EXE=java"
if not defined ANDROID_HOME set "ANDROID_HOME=D:\Android\SDK"
if not defined ANDROID_SDK_HOME set "ANDROID_SDK_HOME=%ANDROID_HOME%"
if not defined ANDROID_NDK_HOME set "ANDROID_NDK_HOME=%ANDROID_HOME%\ndk\27.0.12077973"
if not defined CMAKE_PATH set "CMAKE_PATH=%ANDROID_HOME%\cmake\3.22.1"
if not defined CMAKE_BUILD_PARALLEL_LEVEL set "CMAKE_BUILD_PARALLEL_LEVEL=6"
if not defined NMMP_JAR set "NMMP_JAR=D:\Android\SDK_WSL\nmmp\vm-protect.jar"
if not defined NMMP_PROTECTION_PROFILE set "NMMP_PROTECTION_PROFILE=enforce"
if not defined NMMP_DIAGNOSTICS set "NMMP_DIAGNOSTICS=false"
set "NMMP_TEST_SEED="

if not defined ENV_FILE if exist "%SCRIPT_DIR%nmmp.env" set "ENV_FILE=%SCRIPT_DIR%nmmp.env"
if defined ENV_FILE (
    for %%I in ("%ENV_FILE%") do set "ENV_FILE=%%~fI"
    if not exist "%ENV_FILE%" (
        echo [protect] Environment file not found: %ENV_FILE%
        exit /b 2
    )
    for %%I in ("%ENV_FILE%") do set "ENV_DIR=%%~dpI"
    for /f "usebackq eol=# tokens=1,* delims==" %%A in ("%ENV_FILE%") do if not "%%A"=="" set "%%A=%%B"
)

set "USING_DEFAULT_ARTIFACT_KEYS="
if not defined NMMP_ARTIFACT_PRIVATE_KEY if not defined NMMP_ARTIFACT_PUBLIC_KEY (
    set "NMMP_ARTIFACT_PRIVATE_KEY=%SCRIPT_DIR%artifact-private.pk8"
    set "NMMP_ARTIFACT_PUBLIC_KEY=%SCRIPT_DIR%artifact-public.spki"
    set "USING_DEFAULT_ARTIFACT_KEYS=1"
)

if defined ENV_DIR (
    pushd "%ENV_DIR%" || exit /b 2
    for %%I in ("%ANDROID_HOME%") do set "ANDROID_HOME=%%~fI"
    for %%I in ("%ANDROID_SDK_HOME%") do set "ANDROID_SDK_HOME=%%~fI"
    for %%I in ("%ANDROID_NDK_HOME%") do set "ANDROID_NDK_HOME=%%~fI"
    for %%I in ("%CMAKE_PATH%") do set "CMAKE_PATH=%%~fI"
    for %%I in ("%NMMP_JAR%") do set "NMMP_JAR=%%~fI"
    if defined NMMP_ARTIFACT_PRIVATE_KEY for %%I in ("%NMMP_ARTIFACT_PRIVATE_KEY%") do set "NMMP_ARTIFACT_PRIVATE_KEY=%%~fI"
    if defined NMMP_ARTIFACT_PUBLIC_KEY for %%I in ("%NMMP_ARTIFACT_PUBLIC_KEY%") do set "NMMP_ARTIFACT_PUBLIC_KEY=%%~fI"
    if defined NMMP_SENSITIVE_METHODS_FILE for %%I in ("%NMMP_SENSITIVE_METHODS_FILE%") do set "NMMP_SENSITIVE_METHODS_FILE=%%~fI"
    popd
)

for %%I in ("%APK%") do (
    set "APK=%%~fI"
    set "APK_DIR=%%~dpI"
    set "APK_NAME=%%~nI"
)
if not defined RULES if exist "%CALL_DIR%\convertRules.txt" set "RULES=%CALL_DIR%\convertRules.txt"
if defined RULES for %%I in ("%RULES%") do set "RULES=%%~fI"
if defined MAPPING for %%I in ("%MAPPING%") do set "MAPPING=%%~fI"

if defined USING_DEFAULT_ARTIFACT_KEYS echo [protect] WARNING: no artifact key pair was configured; using the default key pair in %SCRIPT_DIR%

call :validate
set "RESULT=%ERRORLEVEL%"
if "%RESULT%"=="0" goto :validated
endlocal & exit /b %RESULT%

:validated

echo [protect] Work dir:   %CALL_DIR%
echo [protect] APK:        %APK%
if defined RULES echo [protect] Rules:      %RULES%
if defined MAPPING echo [protect] Mapping:    %MAPPING%
if defined ENV_FILE echo [protect] Env:        %ENV_FILE%
echo [protect] JAR:        %NMMP_JAR%
echo [protect] Profile:    %NMMP_PROTECTION_PROFILE%
echo [protect] Diagnostics:%NMMP_DIAGNOSTICS%
echo.

cd /d "%CALL_DIR%"
if defined MAPPING (
    call :run_java apk "%APK%" "%RULES%" "%MAPPING%"
) else if defined RULES (
    call :run_java apk "%APK%" "%RULES%"
) else (
    call :run_java apk "%APK%"
)
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
    echo [protect] Failed with exit code %RESULT%.
    endlocal & exit /b %RESULT%
)

set "DEFAULT_OUTPUT=%APK_DIR%build\%APK_NAME%-protect.apk"
if not exist "%DEFAULT_OUTPUT%" (
    echo [protect] Expected output not found: %DEFAULT_OUTPUT%
    endlocal & exit /b 4
)
for /f %%I in ('powershell.exe -NoProfile -Command "Get-Date -Format yyyyMMddHHmmss"') do set "STAMP=%%I"
if not defined STAMP (
    echo [protect] Failed to create output timestamp.
    endlocal & exit /b 4
)
set "FINAL_OUTPUT=%CALL_DIR%\%APK_NAME%_protect%STAMP%.apk"
move /Y "%DEFAULT_OUTPUT%" "%FINAL_OUTPUT%" >nul
if errorlevel 1 (
    echo [protect] Failed to move output: %DEFAULT_OUTPUT%
    endlocal & exit /b 4
)
echo.
echo [protect] Output: %FINAL_OUTPUT%
endlocal & exit /b 0

:validate
if not exist "%APK%" (
    echo [protect] APK not found: %APK%
    exit /b 2
)
if defined RULES if not exist "%RULES%" (
    echo [protect] Rules file not found: %RULES%
    exit /b 2
)
if defined MAPPING if not exist "%MAPPING%" (
    echo [protect] Mapping file not found: %MAPPING%
    exit /b 2
)
if defined MAPPING if not defined RULES (
    echo [protect] Mapping requires rules.
    exit /b 2
)
if not exist "%NMMP_JAR%" (
    echo [protect] JAR not found: %NMMP_JAR%
    exit /b 3
)
if not exist "%CMAKE_PATH%\bin\cmake.exe" (
    echo [protect] CMake not found: %CMAKE_PATH%\bin\cmake.exe
    exit /b 3
)
if not exist "%CMAKE_PATH%\bin\ninja.exe" (
    echo [protect] Ninja not found: %CMAKE_PATH%\bin\ninja.exe
    exit /b 3
)
if not exist "%ANDROID_NDK_HOME%\toolchains\llvm\prebuilt\windows-x86_64\bin\clang.exe" (
    echo [protect] Windows NDK Clang not found.
    exit /b 3
)
if defined NMMP_ARTIFACT_PRIVATE_KEY if not defined NMMP_ARTIFACT_PUBLIC_KEY (
    echo [protect] NMMP_ARTIFACT_PRIVATE_KEY and NMMP_ARTIFACT_PUBLIC_KEY must be configured together.
    exit /b 3
)
if not defined NMMP_ARTIFACT_PRIVATE_KEY if defined NMMP_ARTIFACT_PUBLIC_KEY (
    echo [protect] NMMP_ARTIFACT_PRIVATE_KEY and NMMP_ARTIFACT_PUBLIC_KEY must be configured together.
    exit /b 3
)
if not defined NMMP_ARTIFACT_PRIVATE_KEY (
    echo [protect] No artifact key pair is available.
    exit /b 3
)
if not exist "%NMMP_ARTIFACT_PRIVATE_KEY%" (
    echo [protect] Private key not found: %NMMP_ARTIFACT_PRIVATE_KEY%
    exit /b 3
)
if not exist "%NMMP_ARTIFACT_PUBLIC_KEY%" (
    echo [protect] Public key not found: %NMMP_ARTIFACT_PUBLIC_KEY%
    exit /b 3
)
if /I not "%NMMP_PROTECTION_PROFILE%"=="observe" if /I not "%NMMP_PROTECTION_PROFILE%"=="enforce" (
    echo [protect] NMMP_PROTECTION_PROFILE must be observe or enforce.
    exit /b 3
)
if /I not "%NMMP_DIAGNOSTICS%"=="true" if /I not "%NMMP_DIAGNOSTICS%"=="false" (
    echo [protect] NMMP_DIAGNOSTICS must be true or false.
    exit /b 3
)
if defined NMMP_SENSITIVE_METHODS_FILE if not exist "%NMMP_SENSITIVE_METHODS_FILE%" (
    echo [protect] Sensitive-method file not found: %NMMP_SENSITIVE_METHODS_FILE%
    exit /b 3
)
"%JAVA_EXE%" -version >nul 2>&1
if errorlevel 1 (
    echo [protect] Java not found: %JAVA_EXE%
    exit /b 3
)
exit /b 0

:run_java
if defined NMMP_SENSITIVE_METHODS_FILE (
    "%JAVA_EXE%" "-Dnmmp.artifact.privateKey=%NMMP_ARTIFACT_PRIVATE_KEY%" "-Dnmmp.artifact.publicKey=%NMMP_ARTIFACT_PUBLIC_KEY%" "-Dnmmp.protectionProfile=%NMMP_PROTECTION_PROFILE%" "-Dnmmp.diagnostics=%NMMP_DIAGNOSTICS%" "-Dnmmp.sensitiveMethodsFile=%NMMP_SENSITIVE_METHODS_FILE%" -jar "%NMMP_JAR%" %*
) else (
    "%JAVA_EXE%" "-Dnmmp.artifact.privateKey=%NMMP_ARTIFACT_PRIVATE_KEY%" "-Dnmmp.artifact.publicKey=%NMMP_ARTIFACT_PUBLIC_KEY%" "-Dnmmp.protectionProfile=%NMMP_PROTECTION_PROFILE%" "-Dnmmp.diagnostics=%NMMP_DIAGNOSTICS%" -jar "%NMMP_JAR%" %*
)
exit /b %ERRORLEVEL%

:help
echo Windows APK protector ^(O-MVLL and WSL are not used^)
echo.
echo Usage:
echo   nmmp ^<apk^> [-r ^<rules^>] [-m ^<mapping^>] [-e ^<env-file^>]
echo.
echo Options:
echo   -r, --rules    Conversion rules; defaults to .\convertRules.txt when present.
echo   -m, --mapping  ProGuard/R8 mapping; requires rules.
echo   -e, --env      KEY=VALUE environment file; relative values use its directory.
echo   -h, --help     Show this help.
echo.
echo Example:
echo   nmmp app-release.apk -r convertRules.txt -m mapping.txt -e D:\config\nmmp.env
echo.
echo Output:
echo   ^<calling-dir^>\^<original-name^>_protectyyyyMMddHHmmss.apk
exit /b 0
