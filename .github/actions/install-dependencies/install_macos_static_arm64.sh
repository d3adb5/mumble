#!/usr/bin/env bash

set -e
set -x

source "$( dirname "$0" )/common.sh"

verify_required_env_variables_set

brew install coreutils aria2 gnu-tar xz

make_build_env_available "tar.xz"


# Note: no database server is set up here. Only the SQLite tests are enabled for this
# job (see build.sh) as those need no external service, which keeps the macOS build -
# whose main purpose is producing the .dmg - short. MySQL and PostgreSQL are covered by
# the other jobs of the build matrix.
