# Upstream provenance

This repository began as a sanitized source snapshot of:

- Project: [Madxbio97/SF-pc-port](https://github.com/Madxbio97/SF-pc-port)
- Imported commit: `f5f10ad0c7aa76a599ec723fbed8a0e5804e5651`
- Import date: 2026-07-28
- Upstream license: MIT

The upstream copyright notice, MIT license, third-party notices and vendored
component licenses are retained.

## Excluded from the public import

The clean import omits optional presentation and localization material that is
not required to compile or run the English game from a user-owned disc:

- character-based launcher artwork;
- dossier screens and character biographies;
- generated Russian font, map and title images;
- packed Russian mission and weapon text; and
- embedded Russian UI, briefing and weapon translations;
- generated localization review images and tables.

No conclusion about upstream legality is implied. These exclusions are a
conservative distribution policy for this independent repository.

## Updating from upstream

The public repository deliberately does not share Git history with upstream.
To incorporate a later upstream version:

1. record the old and new upstream commit IDs;
2. review their source delta locally;
3. exclude game-derived or character presentation assets;
4. apply the reviewed code changes onto this repository without importing
   upstream branches or tags;
5. run the full build and test suite; and
6. commit the synchronization with both commit IDs in the message.

Do not mirror-push upstream refs into this repository. The objective is to keep
the public object graph limited to reviewed source snapshots and our own
changes.
