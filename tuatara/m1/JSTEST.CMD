@echo off
rem Tuatara M1: run the SpiderMonkey shell checks. Output goes to the
rem screen and to JSTEST.LOG.
type BUILD.TXT
js.exe JSTEST.JS > JSTEST.LOG
if errorlevel 1 goto failed
type JSTEST.LOG
echo All checks passed.
goto end
:failed
type JSTEST.LOG
echo Some checks failed (or js.exe did not start). See JSTEST.LOG.
:end
