#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# Check that the NSS build completes a TLS 1.3 handshake: start a local
# OpenSSL server that only speaks TLS 1.3, connect with NSS's tstclnt, and
# look for the protocol in OpenSSL's status page.
#
#   tls13-test.sh DIR      (DIR holds tstclnt.exe and the NSS/NSPR DLLs)
#
# NTCOMPAT_EMULATE is passed through, so the same test can run as on NT 3.51.

set -uo pipefail
dir=$1
cd "$dir"
tmp=$(mktemp -d)
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 \
    -subj "/CN=localhost" -keyout "$tmp/key.pem" -out "$tmp/cert.pem" 2>/dev/null

openssl s_server -accept 4433 -tls1_3 -www -cert "$tmp/cert.pem" -key "$tmp/key.pem" \
    > "$tmp/server.log" 2>&1 &
server=$!
sleep 3

# -D: no certificate database; -o: accept the self-signed certificate.
printf 'GET / HTTP/1.0\r\n\r\n' > "$tmp/request"
timeout 60 ./tstclnt.exe -h 127.0.0.1 -p 4433 -4 -D -o -V tls1.3:tls1.3 -A "$tmp/request" \
    > "$tmp/client.out" 2> "$tmp/client.err"
status=$?
kill $server 2>/dev/null

echo "=== tstclnt exit status: $status"
echo "=== tstclnt stderr"; cat "$tmp/client.err"
echo "=== server status page (protocol and cipher)"
grep -aE "Protocol|Cipher|New, " "$tmp/client.out" | head -5

if grep -aq "TLSv1.3" "$tmp/client.out"; then
    echo "PASS: TLS 1.3 handshake completed"
    exit 0
fi
echo "FAIL: no TLS 1.3 session"
echo "=== client output"; head -c 2000 "$tmp/client.out"
echo "=== server log"; cat "$tmp/server.log"
exit 1
