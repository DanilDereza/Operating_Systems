@echo off

set PROJECT_DIR="C:\Users\danil\Desktop\4_course\OS\Lab_1\wd_win\src"
set BRANCH="main"
set BUILD_DIR="C:\Users\danil\Desktop\4_course\OS\Lab_1\wd_win\build"
set BUILD_COMMAND="cmake -G 'MinGW Makefiles' ../"
set SLEEP_INTERVAL=5

:LOOP
for /f "tokens=*" %%i in ('git -C "%PROJECT_DIR%" rev-parse %BRANCH%') do set LOCAL_COMMIT=%%i
for /f "tokens=1" %%i in ('git -C "%PROJECT_DIR%" ls-remote origin %BRANCH%') do set REMOTE_COMMIT=%%i

if "%LOCAL_COMMIT%" neq "%REMOTE_COMMIT%" (
    git -C "%PROJECT_DIR%" fetch origin || exit /b 1
    git -C "%PROJECT_DIR%" reset --hard origin/"%BRANCH%" || exit /b 1
    cd /d "%BUILD_DIR%" || exit /b 1
    %BUILD_COMMAND% || rem
)
timeout /t %SLEEP_INTERVAL% >nul
goto LOOP