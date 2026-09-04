#!/usr/bin/env bash

set -e
set -x

ARDUPILOT_ROOT=$(realpath "$SCRIPT_DIR/../../")

VENV_PATH=""
if [ -d "$ARDUPILOT_ROOT/venv-ardupilot" ]; then
    VENV_PATH="$ARDUPILOT_ROOT/venv-ardupilot"
    echo "Found existing venv at $VENV_PATH"
elif [ -d "$ARDUPILOT_ROOT/venv" ]; then
    VENV_PATH="$ARDUPILOT_ROOT/venv"
    echo "Found existing venv at $VENV_PATH"
elif [ -d "$ARDUPILOT_ROOT/.venv" ]; then
    VENV_PATH="$ARDUPILOT_ROOT/.venv"
    echo "Found existing venv at $VENV_PATH"
elif [ -d "$HOME/venv-ardupilot" ]; then
    VENV_PATH="$HOME/venv-ardupilot"
    echo "Found existing venv at $VENV_PATH"
else
    VENV_PATH="$ARDUPILOT_ROOT/.venv"
    echo "Creating new venv at $VENV_PATH"
    python3 -m venv --system-site-packages "$VENV_PATH"
fi

sudo apt-get install -y cmake ninja-build gperf device-tree-compiler python3-dev python3-venv ccache xz-utils file wget

python3 -m venv ~/.zephyr-venv

source $VENV_PATH/bin/activate

python3 -m pip install -r modules/zephyr/scripts/requirements.txt

./Tools/scripts/zephyr_get_modules.sh

SDK_EXPORT=$(./Tools/scripts/zephyr_get_sdk.sh | tee /dev/stderr | tail -1)

grep -Fxq "$SDK_EXPORT" ~/.bashrc 2>/dev/null || echo "$SDK_EXPORT" >> ~/.bashrc

set +x
echo
echo "zephyr env ready. in a new shell, or right now with:"
echo "  source $VENV_PATH/bin/activate"
echo "  $SDK_EXPORT"
echo "  unset CROSS_COMPILE"
echo "then: ./waf configure --board beaglev_fire_amp --enable-DDS && ./waf copter"
