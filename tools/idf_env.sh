#!/usr/bin/env bash
# Source this to build and flash the ESP32-C3 firmware without the ESP-IDF
# installer's export step.
#
# Why it exists: `idf_tools.py export` aborts when any toolchain recorded in the IDF
# install is missing, and this machine only has the RISC-V one that esp32c3 needs.
# It then emits nothing at all rather than what it does have, so idf.py cannot find
# the compiler. The paths below are what it would have emitted.
#
# Override any of these before sourcing if your layout differs:
#   IDF_PATH, IDF_PYTHON_ENV_PATH, IDF_TOOLS_PATH
#
# Note: this shell reports PATH in Windows form (semicolons, drive letters) and its
# `ls` is not GNU-compatible (with -d it prints basenames), so the paths below are
# resolved with plain glob expansion instead of ls.

: "${IDF_PATH:=C:/esp/esp-idf-v5.5.3}"
# $HOME is unreliable here (a nested bash may report /root), so default explicitly.
: "${IDF_TOOLS_PATH:=C:/Users/$USERNAME/.espressif}"

# Tools paths may arrive as C:\Users\name; the backslashes stop the globs from
# matching, so normalize them before use.
IDF_TOOLS_PATH="${IDF_TOOLS_PATH//\\//}"
: "${IDF_PYTHON_ENV_PATH:=$IDF_TOOLS_PATH/python_env/idf5.5_py3.14_env}"

# Last directory matched by a glob, or empty when nothing matches.
_last() {
    local last="" p
    for p in "$@"; do
        [[ -d "$p" ]] && last="$p"
    done
    printf '%s' "$last"
}

export IDF_PATH IDF_PYTHON_ENV_PATH
export ESP_ROM_ELF_DIR="${ESP_ROM_ELF_DIR:-$(_last "$IDF_TOOLS_PATH"/tools/esp-rom-elfs/*/)}"
export ESP_IDF_VERSION="${ESP_IDF_VERSION:-5.5.3}"
export IDF_CCACHE_ENABLE=1

for _dir in \
    "$(_last "$IDF_TOOLS_PATH"/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin)" \
    "$(_last "$IDF_TOOLS_PATH"/tools/ninja/*)" \
    "$(_last "$IDF_TOOLS_PATH"/tools/cmake/*/bin)" \
    "$(_last "$IDF_TOOLS_PATH"/tools/ccache/*/*/)" \
    "$IDF_PYTHON_ENV_PATH/Scripts" \
    "$IDF_PATH/tools"
do
    _dir="${_dir%/}"
    [[ -n "$_dir" && -d "$_dir" ]] && PATH="$_dir;$PATH"
done
export PATH

idfpy() { python "$IDF_PATH/tools/idf.py" "$@"; }
