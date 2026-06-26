#!/usr/bin/env bash

set -e
set -x

os=$1
build_type=$2
arch=$3

# Turn variables into lowercase
os="${os,,}"
# only consider name up to the hyphen
os=$(echo "$os" | sed 's/-.*//')
build_type="${build_type,,}"
arch="${arch,,}"


OS_SPECIFIC_CMAKE_OPTIONS=""

case "$os" in
	"ubuntu")
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-sqlite-tests=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-mysql-tests=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-postgresql-tests=ON"
		;;
	"windows")
		if ! [[ "$arch" = "x86_64" ]]; then
			echo "Unsupported architecture '$arch'"
			exit 1
		fi

		eval "$( "C:/vcvars-bash/vcvarsall.sh" x64 )"

		# Rust's cargo links its host build scripts by invoking "link.exe" by name.
		# In the Git-bash environment the GNU coreutils link (/usr/bin/link.exe)
		# shadows MSVC's linker, which breaks the bundled DeepFilterNet (Rust) build
		# with "/usr/bin/link: extra operand". Remove it so the MSVC link.exe set up
		# by vcvars is used instead. Mumble's own C++ build is unaffected (CMake
		# resolves cl/link explicitly).
		rm -f /usr/bin/link /usr/bin/link.exe

		PATH="$PATH:/C/WixSharp"
		echo "PATH=$PATH" >> "$GITHUB_ENV"

		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -DCMAKE_C_COMPILER=cl"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -DCMAKE_CXX_COMPILER=cl"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-sqlite-tests=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-mysql-tests=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-postgresql-tests=OFF"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Dpackaging=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Dasio=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Dg15=ON"

		if [[ "$MUMBLE_SKIP_MSI_REBUILD" = "ON" ]]; then
			OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Dskip-msi-rebuild=ON"
		fi

		if [[ -n "$MUMBLE_USE_ELEVATION" ]]; then
			OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Delevation=ON"
		fi
		;;
	"macos")
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-sqlite-tests=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-mysql-tests=OFF"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-postgresql-tests=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="-DCMAKE_OSX_ARCHITECTURES=$arch"
		;;
	*)
		echo "OS $os is not supported"
		exit 1
		;;
esac


buildDir="${GITHUB_WORKSPACE}/build"

mkdir -p "$buildDir"

cd "$buildDir"

# Run cmake with all necessary options
cmake -G Ninja \
	  -S "$GITHUB_WORKSPACE" \
	  -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
	  -DBUILD_NUMBER=$MUMBLE_BUILD_NUMBER \
	  $OS_SPECIFIC_CMAKE_OPTIONS \
	  $CMAKE_OPTIONS \
      -DCMAKE_UNITY_BUILD=ON \
	  -Ddisplay-install-paths=ON \
	  $ADDITIONAL_CMAKE_OPTIONS \
	  $VCPKG_CMAKE_OPTIONS

# Actually build
cmake --build . --config $BUILD_TYPE

