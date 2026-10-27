#!/bin/sh
# Put build/crest.html on the `pages` branch as index.html, for GitHub Pages (serve from that branch).
#   scripts/pages.sh          # update the local pages branch (scripts/build.sh runs this)
#   scripts/pages.sh --push   # also push it to origin
#
# The branch is written with git plumbing, so the working tree and the current branch are left
# alone. It holds one commit with no history (index.html and .nojekyll), replaced only when the
# file changes, so a multi-MB file doesn't pile up per build; that's why the push is forced.
# crest.html is the shareable single file: build_single.py has already refused Peak patches and
# build paths in it.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

PUSH=0
for arg in "$@"; do
  case "$arg" in
    --push) PUSH=1 ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

if ! git rev-parse --git-dir >/dev/null 2>&1; then
  echo "== pages: not a git checkout, skipped"
  exit 0
fi
[ -f build/crest.html ] || { echo "build/crest.html missing: run scripts/build.sh" >&2; exit 1; }

HTML=$(git hash-object -w build/crest.html)
NOJEKYLL=$(git hash-object -w /dev/null)
TREE=$(printf '100644 blob %s\t.nojekyll\n100644 blob %s\tindex.html\n' "$NOJEKYLL" "$HTML" | git mktree)
if [ "$(git rev-parse -q --verify 'refs/heads/pages^{tree}' || true)" = "$TREE" ]; then
  echo "== pages: index.html unchanged"
else
  SRC=$(git rev-parse --short HEAD)
  git diff --quiet HEAD -- || SRC="$SRC with local changes"
  git update-ref refs/heads/pages "$(git commit-tree "$TREE" -m "Crest standalone synth, built from $SRC")"
  echo "== pages: index.html from $SRC"
fi

if [ "$PUSH" = 1 ]; then
  if git rev-parse -q --verify refs/remotes/origin/pages >/dev/null; then
    git push --force-with-lease=pages:refs/remotes/origin/pages origin pages
  else
    git push origin pages
  fi
fi
