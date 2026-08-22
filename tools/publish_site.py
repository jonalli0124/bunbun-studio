"""Rebuild the tools and publish them to GitHub Pages (the gh-pages branch).

    python tools/publish_site.py

Needs a GitHub token with repo scope in the GITHUB_TOKEN environment variable
(or a path to one in GITHUB_TOKEN_FILE). Never store a token inside this repo.
"""
import os, pathlib, shutil, subprocess, sys, tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent
REMOTE = "github.com/jonalli0124/bunbun-studio.git"

def tok():
    t = os.environ.get("GITHUB_TOKEN")
    if not t and os.environ.get("GITHUB_TOKEN_FILE"):
        t = pathlib.Path(os.environ["GITHUB_TOKEN_FILE"]).read_text().strip()
    if not t:
        sys.exit("set GITHUB_TOKEN (or GITHUB_TOKEN_FILE) first")
    return t

def main():
    subprocess.run([sys.executable, str(REPO / "tools" / "build.py")], check=True)
    site = pathlib.Path(tempfile.mkdtemp()) / "site"
    site.mkdir()
    for f in (REPO / "tools" / "build").glob("*.html"):
        shutil.copy(f, site / f.name)
    shutil.copytree(REPO / "docs", site / "docs")
    sp = REPO / "tools" / "build" / "species"
    if sp.exists():
        shutil.copytree(sp, site / "species")
    # THE WEB FLASHER, at /flash/. This copy list is an allowlist, so a directory that is not
    # named here reaches the public REPO through sync_public.py and still never appears on the
    # SITE - which is exactly how the setup link a family is sent would 404 while every file
    # looked present in git. It carries ~9MB of firmware images; the page is useless without
    # them, so they travel together.
    fl = REPO / "tools" / "flash"
    if fl.exists():
        shutil.copytree(fl, site / "flash")
    index = REPO / "tools" / "site_index.html"
    shutil.copy(index, site / "index.html")
    env = dict(os.environ, GIT_AUTHOR_NAME="Bunbun Studio", GIT_COMMITTER_NAME="Bunbun Studio",
               GIT_AUTHOR_EMAIL="bunbun-studio@users.noreply.github.com",
               GIT_COMMITTER_EMAIL="bunbun-studio@users.noreply.github.com")
    def g(*a): subprocess.run(["git", *a], cwd=site, check=True, env=env)
    g("init", "-q", "-b", "gh-pages")
    g("add", "-A")
    g("commit", "-q", "-m", "publish site")
    g("push", "-q", "--force", f"https://x-access-token:{tok()}@{REMOTE}", "gh-pages")
    print("published - the site updates at the repo's Pages URL in a minute or two")

if __name__ == "__main__":
    main()
