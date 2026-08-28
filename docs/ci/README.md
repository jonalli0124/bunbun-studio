# The auto-rebuild action (parked)

`pages.yml` rebuilds the tools and republishes the site on every push - but installing
a workflow file needs a token with the `workflow` scope, which the publishing token
does not have yet. Until then, `python tools/publish_site.py` does the same job from
any machine. To arm the action: move `pages.yml` to `.github/workflows/` and push with
a workflow-scoped token.
