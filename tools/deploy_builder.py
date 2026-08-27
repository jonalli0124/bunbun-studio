"""Push the device's own /build page to a bunbun, and PROVE it landed.

Why this exists: `/build` is served from the device's SPIFFS at /spiffs/www/builder.html, and
nothing ever wrote it except a human with curl. On 2026-08-19 that cost hours - the M11
mismatch warning had been in tools/device_import.html the whole time, while the device served
a 17,839-byte copy that predated it. The page looked broken; it had simply never been
deployed.

The failure mode is the dangerous one: source is right, device is stale, and nothing says so.
So this script does not just upload - it re-fetches afterwards and compares, and it is loud
when they differ.

    py tools/deploy_builder.py                    # check every known bunbun, change nothing
    py tools/deploy_builder.py --push             # update any that are out of date
    py tools/deploy_builder.py --push bunbun-6D1C.local
"""
import hashlib
import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "device_import.html")
PATH = "/spiffs/www/builder.html"
# Every unit we know about. A host that is simply switched off reports "unreachable" and
# does not fail the run; a host MISSING from this list is never checked at all, which is
# how D468 sat on a 486-byte builder stub without anyone noticing.
DEFAULT_HOSTS = ["192.168.1.148", "192.168.1.233", "bunbun-6D1C.local",
                 "bunbun-D468.local"]


def fetch(url, timeout=25, data=None):
    req = urllib.request.Request(url, data=data,
                                 method="POST" if data is not None else "GET")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    push = "--push" in sys.argv
    hosts = args or DEFAULT_HOSTS

    local = open(SRC, "rb").read()
    lsum = hashlib.sha256(local).hexdigest()[:12]
    print("source  tools/device_import.html  %d bytes  sha %s\n" % (len(local), lsum))

    bad = 0
    for h in hosts:
        try:
            served = fetch("http://%s/build" % h)
        except Exception as e:
            print("  %-22s unreachable (%s)" % (h, str(e)[:40]))
            bad += 1
            continue
        ssum = hashlib.sha256(served).hexdigest()[:12]
        if ssum == lsum:
            print("  %-22s up to date (%d bytes)" % (h, len(served)))
            continue
        print("  %-22s STALE: device has %d bytes (sha %s), source has %d (sha %s)"
              % (h, len(served), ssum, len(local), lsum))
        if not push:
            bad += 1
            print("     -> run with --push to update it")
            continue
        try:
            fetch("http://%s/api/fs/upload?path=%s" % (h, PATH), timeout=90, data=local)
            after = fetch("http://%s/build" % h)
            asum = hashlib.sha256(after).hexdigest()[:12]
            if asum == lsum:
                print("     -> pushed and VERIFIED (%d bytes)" % len(after))
            else:
                print("     -> PUSHED BUT STILL WRONG: got %d bytes sha %s" % (len(after), asum))
                bad += 1
        except Exception as e:
            print("     -> push failed: %s" % str(e)[:60])
            bad += 1

    if bad:
        print("\n%d device(s) not serving the current builder page." % bad)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
