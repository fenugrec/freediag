@ECHO off
REM -----------------------------------------
SETLOCAL enableDelayedExpansion
REM -----------------------------------------

REM -----------------------------------------
SET __EXIT_CODE=0

REM -----------------------------------------
WHERE cppcheck.exe > NUL 2>&1
IF %ERRORLEVEL% NEQ 0 (
   ECHO cppcheck.exe not found.
   EXIT /B 1
)

FOR /R . %%F IN (*.cppcheck) DO (
	echo %%F
	cppcheck ^
	--enable=warning ^
	--enable=style ^
	--enable=performance ^
	--enable=information ^
	--suppress=missingInclude ^
	--suppress=missingSystemInclude ^
	--suppress=unmatchedSuppression ^
	--error-exitcode=1 ^
	--inline-suppr ^
	"--project=%%F" > NUL || GOTO ERROR
)

REM -----------------------------------------
:ERROR
set __EXIT_CODE=1

REM -----------------------------------------
EXIT /B %__EXIT_CODE%
REM -----------------------------------------