#!/usr/bin/env bash
set -euo pipefail

repo_root="${1:?repo root is required}"

require_file() {
    local path="$1"
    if [[ ! -f "${repo_root}/${path}" ]]; then
        echo "missing required file: ${path}" >&2
        exit 1
    fi
}

grep -q 'option(LITEIM_BUILD_QT_CLIENT' "${repo_root}/CMakeLists.txt" || {
    echo "missing LITEIM_BUILD_QT_CLIENT option" >&2
    exit 1
}

grep -q 'add_subdirectory(client_qt)' "${repo_root}/CMakeLists.txt" || {
    echo "root CMakeLists.txt does not conditionally add client_qt" >&2
    exit 1
}

require_file "client_qt/CMakeLists.txt"
require_file "client_qt/include/liteim_client/MainWindow.hpp"
require_file "client_qt/src/MainWindow.cpp"
require_file "client_qt/src/main.cpp"
require_file "client_qt/resources/qss/app.qss"
require_file "client_qt/resources/icons/README.md"

if find "${repo_root}/client_qt/resources" -type f ! -name 'README.md' -print0 \
    | xargs -0 --no-run-if-empty grep -Iiq 'wechat\|weixin'; then
    echo "Qt resource files must not mention or reuse WeChat branding" >&2
    exit 1
fi

if find "${repo_root}/client_qt/resources" -type f -printf '%f\n' \
    | grep -iq 'wechat\|weixin'; then
    echo "Qt resource filenames must not mention WeChat branding" >&2
    exit 1
fi
