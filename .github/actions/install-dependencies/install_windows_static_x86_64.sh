#!/usr/bin/env bash

set -e
set -x

source "$( dirname "$0" )/common.sh"

# All of these are already installed on the runner
# choco install cmake ninja vswhere
choco install aria2 7zip

verify_required_env_variables_set

make_build_env_available "7z"

aria2c "https://dl.mumble.info/build/extra/asio_sdk.zip" --out "asio_sdk.zip"
extract_with_progress "asio_sdk.zip" "${GITHUB_WORKSPACE}/3rdparty/asio"

aria2c "https://dl.mumble.info/build/extra/g15_sdk.zip" --out "g15_sdk.zip"
extract_with_progress "g15_sdk.zip" "g15_sdk"
mv "g15_sdk/LCDSDK" "${GITHUB_WORKSPACE}/3rdparty/g15"
rm -rf "g15_sdk"

aria2c "https://github.com/oleg-shilo/wixsharp/releases/download/v1.19.0.0/WixSharp.1.19.0.0.7z" --out "WixSharp.7z"
extract_with_progress "WixSharp.7z" "C:/WixSharp"

git clone "https://github.com/nathan818fr/vcvars-bash.git" "C:/vcvars-bash"


# Configure database tables for Mumble tests
echo -e "[mysqld]\nlog-bin-trust-function-creators = 1" >> "C:/Windows/my.ini"

mysqld --initialize-insecure --console

powershell -Command "Start-Process mysqld"

# Give the MySQL daemon some time to start up: a fixed sleep is not enough, as
# the daemon regularly takes longer than that to accept connections.
ready=0
for _ in {1..30}; do
	if mysql --user=root -e "SELECT 1" &> /dev/null \
		|| mysql --user=root --password="root" -e "SELECT 1" &> /dev/null; then
		ready=1
		break
	fi
	sleep 2
done

if [[ "$ready" -ne 1 ]]; then
	echo "MySQL daemon did not accept connections in time" 1>&2
	exit 1
fi

configure_database_tables "mysql"
