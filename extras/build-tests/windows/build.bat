rem Build script for appveyor

rem Initialize Visual Studio variables
if "%TARGET%" == "Visual Studio 2017" call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars32.bat"

rem Installing tools
cinst unrar -y
cinst unzip -y
cinst innosetup -y
curl -fsS -o dlltool.exe https://www.unrealircd.org/files/dev/win/dlltool.exe

rem Installing UnrealIRCd dependencies
cd \projects
mkdir unrealircd-deps
cd unrealircd-deps
curl -fsS -o SetACL.exe https://www.unrealircd.org/files/dev/win/SetACL.exe
curl -fsS -o unrealircd-libraries-devel.zip https://www.unrealircd.org/files/dev/win/libs/unrealircd-libraries-devel.zip
unzip unrealircd-libraries-devel.zip

cd \projects\unrealircd

rem Now the actual build
call extras\build-tests\windows\compilecmd\%SHORTNAME%.bat

rem The above command will fail, due to missing symbol file
rem However the symbol file can only be generated after the above command
rem So... we create the symbolfile...
nmake -f makefile.win32 SYMBOLFILE

rem And we re-run the exact same command:
call extras\build-tests\windows\compilecmd\%SHORTNAME%.bat
if %ERRORLEVEL% NEQ 0 EXIT /B 1

rem Convert c:\dev to c:\projects\unrealircd-deps
rem TODO: should use environment variable in innosetup script?
sed -i "s/c:\\\\dev/c:\\\\projects\\\\unrealircd-deps/gi" src\win32\unrealinst.iss

rem Build installer file
"c:\Program Files (x86)\Inno Setup 5\iscc.exe" /Q- src\win32\unrealinst.iss
if %ERRORLEVEL% NEQ 0 EXIT /B 1

rem Show some proof
ren mysetup.exe unrealircd-dev-build.exe
dir unrealircd-dev-build.exe
sha256sum unrealircd-dev-build.exe

rem Upload artifact
appveyor PushArtifact unrealircd-dev-build.exe
if %ERRORLEVEL% NEQ 0 EXIT /B 1
