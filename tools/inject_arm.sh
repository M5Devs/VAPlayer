#!/bin/sh
# VAPlayer provisioning tools — inject_arm wrapper
#
# Copyright (C) 2026 VAPlayer contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Delegates to inject_arm.py so the tool can be invoked as a shell script.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

PY=${VAPLAYER_PYTHON:-}
if [ -z "$PY" ]; then
    if command -v python3 >/dev/null 2>&1; then
        PY=python3
    elif command -v python >/dev/null 2>&1; then
        PY=python
    else
        echo "inject_arm: python3 is required but was not found" >&2
        exit 1
    fi
fi

exec "$PY" "$here/inject_arm.py" "$@"
