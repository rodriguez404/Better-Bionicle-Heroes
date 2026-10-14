@echo off
rem Builds the mod and packs a release archive: dist\BetterBionicleHeroes-<version>.zip
rem   package.cmd [version]        (default version: dev)
rem The archive is extracted straight into the game folder (next to Bionicle.exe) and contains
rem everything needed, including Ultimate ASI Loader (dinput8.dll).
setlocal
set "ROOT=%~dp0"
set "VERSION=%~1"
if "%VERSION%"=="" set "VERSION=dev"
set "UAL=%ROOT%third_party\ultimate-asi-loader\dinput8.dll"

if not exist "%UAL%" (
  echo Missing %UAL%
  echo Put the x86 dinput8.dll from an Ultimate ASI Loader release there:
  echo   https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases
  exit /b 1
)

call "%ROOT%build.cmd" || exit /b 1

set "STAGE=%ROOT%dist\stage"
set "ZIP=%ROOT%dist\BetterBionicleHeroes-%VERSION%.zip"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\BetterBionicleHeroes-licenses" || exit /b 1

copy /y "%ROOT%build\Release\BetterBionicleHeroes.asi" "%STAGE%\" >nul || exit /b 1
copy /y "%ROOT%BetterBionicleHeroes.ini" "%STAGE%\" >nul || exit /b 1
copy /y "%UAL%" "%STAGE%\dinput8.dll" >nul || exit /b 1
copy /y "%ROOT%README.md" "%STAGE%\BetterBionicleHeroes-README.md" >nul || exit /b 1
copy /y "%ROOT%third_party\ultimate-asi-loader\LICENSE" "%STAGE%\BetterBionicleHeroes-licenses\Ultimate-ASI-Loader.txt" >nul || exit /b 1
copy /y "%ROOT%third_party\minhook\LICENSE.txt" "%STAGE%\BetterBionicleHeroes-licenses\MinHook.txt" >nul || exit /b 1

if exist "%ZIP%" del "%ZIP%"
rem tar (bsdtar, part of Windows 10+) writes standard zip entries ("/" separators)
pushd "%STAGE%" || exit /b 1
"%SystemRoot%\System32\tar.exe" -a -cf "%ZIP%" * || (popd & exit /b 1)
popd
rmdir /s /q "%STAGE%"
echo Created %ZIP%
