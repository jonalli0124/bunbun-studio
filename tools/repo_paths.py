"""Where the art lives, resolved from this file so a clone works anywhere.

Layout (matches the existing repo conventions):
    assets/characters/<species>/<clip>/<frame>.png   cleaned, ready-to-pack sprite frames
    assets/objects/<name>.png                        props and overlay marks
    assets/rooms/<name>.png                          320x240 room backgrounds
"""
import os

TOOLS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(TOOLS)
ASSETS = os.path.join(REPO, "assets")

CHARACTERS = os.path.join(ASSETS, "characters")
OBJECTS = os.path.join(ASSETS, "objects")
ROOMS = os.path.join(ASSETS, "rooms")

DEFAULT_SPECIES = "capybara"


def species_dir(name=DEFAULT_SPECIES):
    return os.path.join(CHARACTERS, name)
