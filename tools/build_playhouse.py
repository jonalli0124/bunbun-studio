"""Inline the sprite data into the shell to produce the shareable single-file page."""
import os

from repo_paths import TOOLS as T
shell = open(os.path.join(T, "scene_shell.html"), encoding="utf-8").read()
data  = open(os.path.join(T, "scene_data.js"),   encoding="utf-8").read()

marker = "/*SCENE_DATA*/"
assert marker in shell, "marker missing from shell"
out = shell.replace(marker, data)

path = os.path.join(T, "playhouse.html")
with open(path, "w", encoding="utf-8") as fh:
    fh.write(out)
print(f"{path}  {os.path.getsize(path)/1024/1024:.2f} MB")
