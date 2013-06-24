@echo off
SET PATH=D:\gcc\bin;%PATH%
SET BEGINLIBPATH=D:\gcc\lib;
SET C_INCLUDE_PATH=D:\gcc\include;
SET LIBRARY_PATH=D:\gcc\lib;
rem SET EMXOMFLD_TYPE=VAC308
SET EMXOMFLD_TYPE=
SET EMXOMFLD_LINKER=ILINK
                
make
del lda????? 2>NUL
pause
