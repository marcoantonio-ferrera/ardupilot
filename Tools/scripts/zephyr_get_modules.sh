#!/usr/bin/env bash

set -e

if [ ! -d modules ]; then
    echo "run this script from the root of your ardupilot repo"
    exit 1
fi

WEST_YML=modules/zephyr/west.yml

MODULES="
lib/open-amp:open-amp:5efe7974f9546582e99f5a842a816ea4b65f5227
hal/libmetal:libmetal:66e084293b2a7ced5a73fbd247deddba8915883a
"

west_revision() {
    [ -f "$WEST_YML" ] || return 0
    awk -v want="$1" '
        $1 == "-" && $2 == "name:" { name = $3 }
        $1 == "revision:" && name == want { print $2; exit }
    ' "$WEST_YML"
}

for entry in $MODULES; do
    path=${entry%%:*}
    rest=${entry#*:}
    name=${rest%%:*}
    rev=${rest##*:}

    pinned=$(west_revision "$name")
    if [ -n "$pinned" ] && [ "$pinned" != "$rev" ]; then
        echo "$name: using $WEST_YML revision $pinned"
        rev=$pinned
    fi

    dest="modules/modules/$path"
    if [ ! -d "$dest/.git" ]; then
        echo "cloning $name into $dest"
        mkdir -p "$(dirname "$dest")"
        git clone "https://github.com/zephyrproject-rtos/$name.git" "$dest"
    fi

    echo "$name -> $rev"
    git -C "$dest" fetch --quiet origin
    git -C "$dest" checkout --quiet "$rev"
done
