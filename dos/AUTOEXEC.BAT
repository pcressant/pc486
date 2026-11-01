@ECHO OFF
REM Memoire superieure : S3RES d'abord (95 Ko, seulement si EPAUDOS.INI choisit
REM Synthe3), puis SMARTDRV et Epaudos (LH) dans ce qui reste.
SET BLASTER=A220 I5 D1 H5 T6
IF EXIST C:\S3RES.EXE LH C:\S3RES.EXE
REM SMARTDRV sans cache en ‚criture : arrˆter la VM ne perd pas de donn‚es
C:\WINDOWS\SMARTDRV.EXE C
PROMPT $P$G
PATH C:\WINDOWS;C:\DOS
SET TEMP=C:\TEMP
MODE CON CODEPAGE PREPARE=((850) C:\DOS\EGA.CPI)
MODE CON CODEPAGE SELECT=850
KEYB FR,,C:\DOS\KEYBOARD.SYS
GOTO %CONFIG%

:DOS
REM Synthese choisie dans C:\EPAUDOS.INI (Inser+E d'Epaudos) : Synthese=Apollo,
REM Apollo II sur COM1 ; Synthese=Synthe3, Sound Blaster (S3RES, charge plus haut).
LH C:\EPAUDOS.COM
GOTO FIN

:WIN
REM Epaudos en veille sous Windows : il lit l'invite MS-DOS avec Epaulard
REM S3RES present : Epaulard parle aussi par Synthe3 (EPAUS3.DLL, pilote audio)
LH C:\EPAUDOS.COM
WIN

:FIN
