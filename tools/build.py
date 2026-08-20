"""Build the shareable tools. One command, run it after changing any art.

    py tools/build.py

Reads the art in assets/, regenerates the data bundles, and writes self-contained pages into
tools/build/ that work from a double-click, a local server, or a share link.

    assets/          ->  tools/build/attach_data.json  ->  tools/build/attach_editor.html
                     ->  tools/build/scene_data.js     ->  tools/build/playhouse.html

Nothing in tools/build/ is committed; it all rebuilds from here.
"""
import io, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "src")
BUILD = os.path.join(HERE, "build")
PY = sys.executable


def run(script):
    print(f"--- {script}")
    r = subprocess.run([PY, os.path.join(HERE, script)], cwd=HERE,
                       capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode:
        sys.stderr.write(r.stderr)
        raise SystemExit(f"{script} failed")
    return r.stdout


def inline(page, marker, data_file, out_name):
    """Drop a data bundle into a page at its marker and write the result."""
    shell = io.open(os.path.join(SRC, page), encoding="utf-8").read()
    if marker not in shell:
        raise SystemExit(f"{page}: marker {marker} missing")
    data = io.open(os.path.join(BUILD, data_file), encoding="utf-8").read()
    out = os.path.join(BUILD, out_name)
    tmp = out + ".tmp"
    with io.open(tmp, "w", encoding="utf-8") as fh:
        fh.write(shell.replace(marker, data, 1))
    os.replace(tmp, out)
    mb = os.path.getsize(out) / 1024 / 1024
    print(f"    {out_name}  {mb:.2f} MB")
    return out


os.makedirs(BUILD, exist_ok=True)
run("mkdata.py")
run("mkscene.py")

print("--- pages")
# THE DEVICE'S OWN PAGE IS A PAGE TOO. It is served from the bunbun's SPIFFS, so building it
# here is not enough - it has to be pushed. Copy it in and say so loudly; deploy_builder.py
# does the pushing and verifies what actually landed.
import shutil as _sh
_sh.copyfile(os.path.join(HERE, "device_import.html"),
             os.path.join(BUILD, "builder.html"))
print("    builder.html  %.2f MB   (device page - run: py tools/deploy_builder.py --push)"
      % (os.path.getsize(os.path.join(BUILD, "builder.html")) / 1e6))
# the attach editor takes raw JSON, so wrap it into the global the page reads
raw = io.open(os.path.join(BUILD, "attach_data.json"), encoding="utf-8").read()
io.open(os.path.join(BUILD, "_attach_data.js"), "w", encoding="utf-8").write(
    "window.ATTACH_DATA=" + raw + ";")
inline("attach_editor.html", "/*ATTACH_DATA*/", "_attach_data.js", "attach_editor.html")
inline("scene_shell.html", "/*SCENE_DATA*/", "scene_data.js", "playhouse.html")
inline("scene_tool.html", "/*ATTACH_DATA*/", "_attach_data.js", "scene_tool.html")

print("\nBuilt into tools/build/ - open either .html directly, or publish it.")

# `py tools/build.py --serve` rebuilds and then serves THIS folder, so the URL in the browser
# can never be a stale copy sitting somewhere else. That mistake cost an evening once.
if "--serve" in sys.argv:
    import http.server, socketserver, functools
    port = 8000
    for a in sys.argv:
        if a.startswith("--port="):
            port = int(a.split("=", 1)[1])
    # Jon spent an evening on an hours-old page because the browser kept serving its cached
    # copy while the server had the new one. A dev server has no business caching.
    class NoCache(http.server.SimpleHTTPRequestHandler):
        def end_headers(self):
            self.send_header("Cache-Control", "no-store, must-revalidate")
            self.send_header("Pragma", "no-cache")
            self.send_header("Expires", "0")
            super().end_headers()

        # THE KID-PROOF UNDO (council 2026-08-17, ES-4 binding condition): before the Send
        # button merges anything, it POSTs /stash?ip=<device> here fire-and-forget; this
        # server fetches the unit's CURRENT pak and archives it into the recovery vault. An
        # overwritten table can always come back, and the vault's recovery drill stays
        # honest (the device pak - kids' art included - is IN the vault after every port).
        def do_POST(self):
            if not self.path.startswith("/stash"):
                self.send_error(404)
                return
            try:
                import urllib.request, urllib.parse, datetime, pathlib
                q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
                ip = q.get("ip", [""])[0]
                if not ip:
                    self.send_error(400, "no ip")
                    return
                data = urllib.request.urlopen(f"http://{ip}/api/ota/assets", timeout=60).read()
                vault = pathlib.Path(r"C:/Users/Jon/bunbun-dr/pak-archive")
                vault.mkdir(parents=True, exist_ok=True)
                stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
                out = vault / f"bunbun-{ip.replace('.', '-')}-{stamp}.pak"
                out.write_bytes(data)
                # keep the newest 20 - a vault, not a landfill
                paks = sorted(vault.glob("*.pak"), key=lambda f: f.stat().st_mtime)
                for old_f in paks[:-20]:
                    old_f.unlink()
                self.send_response(200)
                self.end_headers()
                self.wfile.write(f"stashed {out.name} ({len(data)//1024} KB)".encode())
                print(f"  stash: {out.name} ({len(data)//1024} KB)")
            except Exception as e:
                self.send_error(500, str(e)[:80])

    handler = functools.partial(NoCache, directory=BUILD)
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", port), handler) as httpd:
        print(f"\nserving {BUILD}")
        print(f"  http://localhost:{port}/attach_editor.html")
        print(f"  http://localhost:{port}/playhouse.html")
        print("ctrl-c to stop")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nstopped")
