#!/usr/bin/env bash

set -e

if [ ! -d modules ]; then
    echo "run this script from the root of your ardupilot repo"
    exit 1
fi

SDK_VERSION="${ZEPHYR_SDK_VERSION:-}"
if [ -z "$SDK_VERSION" ] && [ -f modules/zephyr/SDK_VERSION ]; then
    SDK_VERSION=$(tr -d '[:space:]' < modules/zephyr/SDK_VERSION)
fi
if [ -z "$SDK_VERSION" ]; then
    echo "cannot determine the SDK version: set ZEPHYR_SDK_VERSION, or init the modules/zephyr submodule so SDK_VERSION can be read"
    exit 1
fi

TARGET="${ZEPHYR_SDK_TARGET:-riscv64-zephyr-elf}"

case $(uname -m) in
    x86_64)  HOST_ARCH=linux-x86_64 ;;
    aarch64) HOST_ARCH=linux-aarch64 ;;
    *)
        echo "no zephyr sdk build for host arch $(uname -m)"
        exit 1
        ;;
esac

SDK_PARENT="${ZEPHYR_SDK_PARENT:-/opt}"
SDK_DIR="$SDK_PARENT/zephyr-sdk-$SDK_VERSION"

if [ -x "$SDK_DIR/$TARGET/bin/$TARGET-gcc" ]; then
    echo "zephyr sdk $SDK_VERSION with $TARGET already installed at $SDK_DIR"
    echo "export ZEPHYR_SDK_INSTALL_DIR=$SDK_DIR"
    exit 0
fi

TARBALL="zephyr-sdk-${SDK_VERSION}_${HOST_ARCH}_minimal.tar.xz"
URL="https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v$SDK_VERSION"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "downloading $TARBALL"
wget --progress=dot:giga -P "$TMP" "$URL/$TARBALL"
wget -q -P "$TMP" "$URL/sha256.sum"
(cd "$TMP" && awk -v f="$TARBALL" '$2 == f' sha256.sum | sha256sum -c -)

echo "extracting into $SDK_PARENT"
sudo mkdir -p "$SDK_PARENT"
sudo tar xf "$TMP/$TARBALL" -C "$SDK_PARENT"
sudo chown -R "$USER" "$SDK_DIR"

echo "installing $TARGET toolchain"
"$SDK_DIR/setup.sh" -t "$TARGET" -c

echo "export ZEPHYR_SDK_INSTALL_DIR=$SDK_DIR"
