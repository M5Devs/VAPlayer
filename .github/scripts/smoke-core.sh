#!/bin/sh
#
# Boot a staged vaplayer-core and confirm it reaches the QMP handshake.
#
# This catches a class of failure that "-device help" does not: the binary can
# list its devices and still be unable to start a machine because its firmware
# bundle is missing.  The script exits non-zero unless the core answers
# query-status over QMP.
#
# Usage: smoke-core.sh <path-to-vaplayer-core>

set -eu

core="${1:?usage: smoke-core.sh <path-to-vaplayer-core>}"
if [ ! -x "$core" ]; then
    echo "smoke-core: $core is missing or not executable" >&2
    exit 1
fi

work="$(mktemp -d)"
sock="$work/qmp.sock"
log="$work/core.log"
pid=

cleanup() {
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

# -serial none keeps the guest quiet; TCG is used because hosted runners have
# no /dev/kvm.
"$core" \
    -M q35 -accel tcg -smp 1 -m 128 \
    -display none -no-reboot -serial none \
    -qmp "unix:$sock,server=on,wait=off" \
    >"$log" 2>&1 &
pid=$!

# The socket appears once the machine is built; the handshake then has to
# succeed.  Poll rather than sleep a fixed amount so a slow runner still passes.
python3 - "$sock" <<'PY'
import json, os, socket, sys, time

sock_path = sys.argv[1]
deadline = time.time() + 30

while time.time() < deadline:
    if os.path.exists(sock_path):
        try:
            conn = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            conn.settimeout(10)
            conn.connect(sock_path)
            break
        except OSError:
            conn = None
    time.sleep(0.25)
else:
    sys.exit("smoke-core: the QMP socket never appeared")

def read_reply():
    buf = b""
    while b"\n" not in buf:
        chunk = conn.recv(4096)
        if not chunk:
            raise SystemExit("smoke-core: the core closed the connection")
        buf += chunk
    return json.loads(buf.split(b"\n", 1)[0])

try:
    read_reply()  # greeting
    conn.sendall(json.dumps({"execute": "qmp_capabilities"}).encode() + b"\n")
    read_reply()
    conn.sendall(json.dumps({"execute": "query-status"}).encode() + b"\n")
    reply = read_reply()
finally:
    conn.close()

if "return" not in reply:
    raise SystemExit("smoke-core: query-status failed: %s" % reply)

print("smoke-core: ok, status =", reply["return"].get("status"))
PY
