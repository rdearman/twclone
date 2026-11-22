#!/bin/sh
set -eu

# Run on a release workstation to build source bundles, then on the web host
# with a verified artifact directory. A directory rename and symlink rename
# make the visible release change atomic.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=${1:?usage: release.sh VERSION ARTIFACT_DIR}
ARTIFACT_DIR=${2:?usage: release.sh VERSION ARTIFACT_DIR}
DEST=${TWCLONE_SITE_RELEASES:-/srv/twclone-site/releases}
CURRENT=${TWCLONE_SITE_CURRENT:-/srv/twclone-site/current}
STAGING="$DEST/.staging-$VERSION-$$"
FINAL="$DEST/$VERSION"

test -d "$ARTIFACT_DIR"
test ! -e "$FINAL"
mkdir -p "$DEST"
mkdir "$STAGING"
cp "$ROOT/index.html" "$ROOT/style.css" "$ROOT/release.js" "$STAGING/"
cp -R "$ARTIFACT_DIR" "$STAGING/downloads"
(cd "$STAGING/downloads" && sha256sum -c SHA256SUMS)
mv "$STAGING" "$FINAL"
ln -s "$FINAL" "$CURRENT.new"
mv -Tf "$CURRENT.new" "$CURRENT"
printf 'Published %s at %s\n' "$VERSION" "$CURRENT"
