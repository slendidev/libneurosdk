@echo off
setlocal EnableDelayedExpansion

where git >nul 2>nul
if %errorlevel% neq 0 (
	echo Git is not installed or not in PATH. Exiting...
	exit /b 1
)

:: Parse command-line arguments
set release=0
set example=0
:parse_args
	if "%~1"=="" goto after_args
	if "%~1"=="-release" set release=1
	if "%~1"=="-example" set example=1
	shift
	goto parse_args
:after_args

set VERSION=v0.1.1

	echo	Fetching mongoose.
	curl	-LO https://raw.githubusercontent.com/cesanta/mongoose/refs/heads/master/mongoose.c
	curl	-LO https://raw.githubusercontent.com/cesanta/mongoose/refs/heads/master/mongoose.h
	move	/Y mongoose.c src\
	move	/Y mongoose.h src\

	echo	Fetching JSON library.
	curl	-LO https://raw.githubusercontent.com/sheredom/json.h/refs/heads/master/json.h
	move	/Y json.h src\

	if "%release%"=="1" (
		set "OPTIM=/O2"
	) else (
		set "OPTIM=/Od /Zi /D DEBUG=1"
	)

:: Retrieve current Git commit hash
for /f "delims=" %%i in ('git rev-parse HEAD') do set GITHASH=%%i

	echo	Building library.
	cl	/std:c17 /c /W4 %OPTIM% /I include ^
		/D LIB_BUILD_HASH=\"%GITHASH%\" ^
		/D LIB_VERSION=\"%VERSION%\" ^
		src\neurosdk.c /Fo:libneurosdk.obj

	echo	Building object file.
	cl /std:c17 /c /W4 %OPTIM% /I include ^
		/D LIB_BUILD_HASH=\"%GITHASH%\" ^
		/D LIB_VERSION=\"%VERSION%\" ^
		/D LIBNEUROSDK_EXPORTS ^
		src\neurosdk.c /Fo:libneurosdk.obj

	echo	Building shared library.
	link /DLL /OUT:libneurosdk.dll libneurosdk.obj /IMPLIB:libneurosdk.lib

	echo	Building static library.
	lib /OUT:libneurosdk_static.lib libneurosdk.obj

	echo	Built libneurosdk.dll, libneurosdk.lib, and libneurosdk_static.lib.

	if "%example%"=="1" (
		echo	Building examples.

		cl	/std:c17 %OPTIM% /I include examples\simple.c libneurosdk_static.lib
		move	/Y simple.exe examples\simple.exe

		cl	/std:c17 %OPTIM% /I include examples\tictactoe.c libneurosdk_static.lib
		move	/Y tictactoe.exe examples\tictactoe.exe
	)

endlocal
