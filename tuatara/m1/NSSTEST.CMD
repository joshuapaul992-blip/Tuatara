@echo off
rem This Source Code Form is subject to the terms of the Mozilla Public
rem License, v. 2.0. If a copy of the MPL was not distributed with this
rem file, You can obtain one at http://mozilla.org/MPL/2.0/.
rem
rem NSSTEST: NSS on this machine, in two parts.
rem   1. No network needed: create a key database, make a self-signed
rem      certificate (random numbers, EC key generation, signing), list it.
rem   2. Needs TCP/IP: a TLS 1.3 handshake on this machine, between selfserv
rem      (in a second window) and tstclnt.
rem Results go to the screen and to NSSTEST.TXT.

if exist DB\cert9.db goto havedb
if not exist DB mkdir DB
echo === 1. creating the key database > NSSTEST.TXT
certutil -N -d DB --empty-password >> NSSTEST.TXT 2>&1
if errorlevel 1 goto fail1
:havedb
rem The noise file replaces certutil's prompt for random keystrokes; the
rem real randomness comes from the system (or ntcompat's pool on NT 3.51).
echo nt351 noise for certutil 0123456789 abcdefghijklmnopqrstuvwxyz > NOISE.TXT
echo === 1. making a self-signed certificate >> NSSTEST.TXT
certutil -D -d DB -n tuatara > NUL 2>&1
certutil -S -d DB -n tuatara -s "CN=localhost" -x -t "CTu,u,u" -k ec -q nistp256 -z NOISE.TXT -v 12 >> NSSTEST.TXT 2>&1
if errorlevel 1 goto fail1
certutil -L -d DB >> NSSTEST.TXT 2>&1
echo PASS part 1: key database, EC key and certificate created >> NSSTEST.TXT
type NSSTEST.TXT

echo.
echo Part 2 needs TCP/IP. In a SECOND command prompt in this folder, run:
echo     selfserv -d DB -n tuatara -p 4433 -V tls1.3:tls1.3
echo then press a key here to connect to it. (Ctrl+C to skip.)
pause > NUL
echo GET / HTTP/1.0> REQUEST.TXT
echo.>> REQUEST.TXT
echo === 2. TLS 1.3 handshake with selfserv on this machine >> NSSTEST.TXT
tstclnt -h localhost -p 4433 -d DB -V tls1.3:tls1.3 -A REQUEST.TXT -C >> NSSTEST.TXT 2>&1
if errorlevel 1 goto fail2
echo PASS part 2: TLS 1.3 handshake completed >> NSSTEST.TXT
type NSSTEST.TXT
goto end

:fail1
echo FAIL part 1 (see the lines above) >> NSSTEST.TXT
type NSSTEST.TXT
goto end
:fail2
echo FAIL part 2 (see the lines above) >> NSSTEST.TXT
type NSSTEST.TXT
:end
