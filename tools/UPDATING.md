# Updating the tools

Two pages are published as links. Both are **built**, never edited in place.

| page | what it is | link |
|---|---|---|
| attach editor | put objects on a character, preview at real in-game scale | `claude.ai/code/artifact/06dd28fe-2956-470b-a31e-2f07ef044592` |
| Capybara Playhouse | scene builder for the kids | `claude.ai/code/artifact/f6479fd4-ff59-4071-b003-fb4eea466ecd` |

Both are private until shared from the page's own share menu.

---

## The loop

```
1. add or change art under assets/     (see assets/README.md for the three homes)
2. py tools/build.py
3. open tools/build/<page>.html        to check it
4. republish that same file            to keep the link
```

**Republish the same file path.** A different path creates a *new* artifact with a *new* link,
and the one you already sent to people stops updating. From a fresh conversation, pass the
existing URL explicitly so it updates in place rather than forking.

## What lives where

```
tools/
  src/attach_editor.html   EDIT THIS        the editor's real source
  src/scene_shell.html     EDIT THIS        the playhouse's real source
  build.py                 run this         regenerates data, inlines it, writes the pages
  mkdata.py                                 assets -> build/attach_data.json
  mkscene.py                                assets -> build/scene_data.js
  clean_sprite.py                           raw generator output -> clean frames
  repo_paths.py                             path resolution, so a clone works anywhere
  build/                   NOT COMMITTED    everything here is regenerated
```

`build/` holds the only copies anyone should open or publish. Each page carries all its art
inline, so it works from a double-click, a local server, or a share link, with no network
requests at all.

## Editing the source directly

`src/attach_editor.html` still runs unbuilt if you serve it next to an `attach_data.json` —
handy for a quick change without a rebuild. It prefers inline data when present and falls back
to fetching, so the same file covers both. The published page never fetches.

## Two things that will bite

**A stale build looks like art that did not change.** The pages embed their art; editing a PNG
does nothing until `build.py` runs. If a sprite looks wrong, rebuild before debugging.

**Downloads only work through the capability.** A plain download link is inert inside the
published viewer. Both pages route saves through `claude.use("downloads")` and fall back to a
link when running locally. If you add a new save button, route it the same way or it will
silently do nothing for anyone opening the link.
