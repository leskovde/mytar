#!/bin/bash

export STEF="$HOME/nprg066/mytar/tests/stef.sh"
export GNUTAR="/bin/tar"
export MYTAR_C="$HOME/nprg066/mytar/mytar.c"
export MYGNUTAR="$HOME/nprg066/mytar/mytar"

[[ -n $MYGNUTAR ]] && export STEF_REGFILE_LOCAL_VARS=" "

[[ -n $STEF ]] || { echo "STEF variable must be set first.  See README." \
    >&2 && exit 1; }
[[ -x $STEF ]] || { echo "STEF variable value '$STEF' not an executable" \
    "script." >&2 && exit 1; }

$STEF $*
