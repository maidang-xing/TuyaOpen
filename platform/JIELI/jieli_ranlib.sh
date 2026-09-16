#!/bin/sh

set -eu
exec "${JIELI_TOOL_DIR}/lto-ar" s "$@"
