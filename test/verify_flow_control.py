from __future__ import print_function

import hashlib
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SERVER_TRACE = ROOT / "server.event.trace"
CLIENT_TRACE = ROOT / "client.event.trace"
SEND_FILE = ROOT / "flow_send.bin"
RECV_FILE = ROOT / "flow_recv.bin"


def event_sizes(path, event):
    pattern = re.compile(r"\[%s\].*\bsize:(\d+)" % event)
    values = []
    with path.open("r", encoding="utf-8") as trace:
        for line in trace:
            match = pattern.search(line)
            if match:
                values.append(int(match.group(1)))
    return values


def probe_count(path):
    pattern = re.compile(r"\[SEND\].*\blength:1\]$")
    with path.open("r", encoding="utf-8") as trace:
        return sum(1 for line in trace if pattern.search(line.rstrip("\n")))


def has_zero_then_recovery(values):
    try:
        zero_index = values.index(0)
    except ValueError:
        return False
    return any(value > 0 for value in values[zero_index + 1 :])


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(chunk)
    return checksum.hexdigest()


def main():
    required = (SERVER_TRACE, CLIENT_TRACE, SEND_FILE, RECV_FILE)
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        print("FAIL missing files: %s" % ", ".join(missing))
        return 1

    rwnd = event_sizes(SERVER_TRACE, "RWND")
    swnd = event_sizes(CLIENT_TRACE, "SWND")
    probes = probe_count(CLIENT_TRACE)
    same_size = SEND_FILE.stat().st_size == RECV_FILE.stat().st_size
    same_hash = same_size and digest(SEND_FILE) == digest(RECV_FILE)

    checks = [
        ("server RWND reached zero", 0 in rwnd),
        ("server RWND recovered", has_zero_then_recovery(rwnd)),
        ("client SWND reached zero", 0 in swnd),
        ("client SWND recovered", has_zero_then_recovery(swnd)),
        ("one-byte probes observed", probes > 0),
        ("files have equal size and SHA-256", same_hash),
    ]
    for name, passed in checks:
        print("%s %s" % ("PASS" if passed else "FAIL", name))
    print("rwnd_events=%d rwnd_min=%s rwnd_max=%s" %
          (len(rwnd), min(rwnd) if rwnd else "n/a", max(rwnd) if rwnd else "n/a"))
    print("swnd_events=%d swnd_min=%s swnd_max=%s probes=%d" %
          (len(swnd), min(swnd) if swnd else "n/a", max(swnd) if swnd else "n/a", probes))
    print("send_bytes=%d recv_bytes=%d" %
          (SEND_FILE.stat().st_size, RECV_FILE.stat().st_size))
    return 0 if all(passed for _, passed in checks) else 1


if __name__ == "__main__":
    sys.exit(main())
