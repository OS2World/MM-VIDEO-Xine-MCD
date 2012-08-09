cd SRC\XINE-ENGINE
make -f makefile.os2
if errorlevel 1 GOTO ERROR
cd ..\XINE-UTILS
make -f makefile.os2
if errorlevel 1 GOTO ERROR
cd ..\AUDIO_OUT
make -f makefile.os2
if errorlevel 1 GOTO ERROR
cd ..\DEMUXERS
make -f makefile.os2
if errorlevel 1 GOTO ERROR
cd ..\INPUT
make -f makefile.os2
if errorlevel 1 GOTO ERROR
cd ..\LIBMPEG2
make -f makefile.os2
if errorlevel 1 GOTO ERROR
cd ..\LIBMAD
make -f makefile.os2
if errorlevel 1 GOTO ERROR
cd ..\LIBFAAD
make -f makefile.os2
if errorlevel 1 GOTO ERROR
cd ..\LIBFFMPEG\LIBAVCODEC\i386
make -f makefile.os2
if errorlevel 1 GOTO ERROR
cd ..\LIBPOSTPROC
make -f makefile.os2
if errorlevel 1 GOTO ERROR
cd ..
make -f makefile.os2
if errorlevel 1 GOTO ERROR
cd ..
make -f makefile.os2
if errorlevel 1 GOTO ERROR
cd ..\VIDEO_OUT
make -f makefile.os2
if errorlevel 1 GOTO ERROR
cd ..\MCD
make -f makefile.os2
if errorlevel 1 GOTO ERROR
CD ..\..
make -f makefile.os2
GOTO END
:ERROR
CD ..\..
:END
