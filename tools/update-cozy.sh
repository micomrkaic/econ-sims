#!/bin/sh
# update-cozy.sh — refresh the vendored copy of the Cozy interpreter.
#
#   tools/update-cozy.sh /path/to/a/cozy/checkout
#
# third_party/cozy holds exactly what the macro simulations need: the
# interpreter's core, the embedding API (cozy.h, embed.c) and the dsge
# package. Nothing in it is edited here; to change it, change Cozy
# (https://github.com/micomrkaic/cozy) and run this again. The copy's
# version and commit are recorded in third_party/cozy/VERSION.
set -eu
src=${1:?usage: tools/update-cozy.sh /path/to/a/cozy/checkout}
cd "$(dirname "$0")/.."
dst=third_party/cozy
[ -f "$src/embed.c" ] && [ -f "$src/cozy.h" ] || { echo "update-cozy: $src is not a Cozy checkout with the embedding library (0.1.30 or later)" >&2; exit 1; }

rm -rf "$dst"
mkdir -p "$dst/packages"
for f in lexer arena ast parser value eval chunk compile vm sparse linalg_tier0 embed; do
    cp "$src/$f.c" "$dst/"
done
cp "$src"/*.h "$src"/*.inc "$dst/"
rm -f "$dst/repl.h"                       # the REPL is not part of the library
cp "$src/packages/dsge.cz" "$dst/packages/"
cp "$src/LICENSE" "$dst/LICENSE"

version=$(sed -n 's/.*COZY_VERSION "\([0-9.]*\)".*/\1/p' "$src/version.h")
commit=$(git -C "$src" rev-parse --short HEAD 2>/dev/null || echo unknown)
cat > "$dst/VERSION" <<EOF
Cozy $version (commit $commit), from https://github.com/micomrkaic/cozy
Vendored by tools/update-cozy.sh. Do not edit these files here.
EOF
echo "update-cozy: third_party/cozy is now Cozy $version ($commit)"
