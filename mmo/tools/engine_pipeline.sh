#!/bin/sh
# Sourced, not run: the one place that knows what the engine's compile rule
# hands our patches.

# engine_pipeline_prepare ENGINE RELPATH WORKDIR
engine_pipeline_prepare() {
    _ep_engine=$1
    _ep_file=$2
    _ep_work=$3

    if [ -f "$_ep_engine/tools/armrec/strip_asm.py" ]; then
        if python3 "$_ep_engine/tools/armrec/strip_asm.py" \
            "$_ep_work/$_ep_file" "$_ep_work/$_ep_file.ep-stripped" 2>/dev/null; then
            mv "$_ep_work/$_ep_file.ep-stripped" "$_ep_work/$_ep_file"
        fi
        rm -f "$_ep_work/$_ep_file.ep-stripped"
    fi

    _ep_base=$_ep_engine/pc/patches/$_ep_file.patch
    if [ -f "$_ep_base" ]; then
        (cd "$_ep_work" && patch -p1 --silent --forward <"$_ep_base" >/dev/null 2>&1) || return 1
    fi
    return 0
}
