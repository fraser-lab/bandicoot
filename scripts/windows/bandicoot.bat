@echo off
rem Bandicoot launcher. The Start menu and desktop shortcuts run this file.
rem Settings for this installation can be added below, before the line that
rem starts Bandicoot.exe.

set "BANDICOOT_DIR=%~dp0"
set "BANDICOOT_DIR=%BANDICOOT_DIR:~0,-1%"

rem Preferences and backups live in the installation folder, as in WinCoot.
set "COOT_HOME=%BANDICOOT_DIR%"
set "COOT_BACKUP_DIR=%BANDICOOT_DIR%\coot-backup"

"%BANDICOOT_DIR%\bin\Bandicoot.exe" %*
