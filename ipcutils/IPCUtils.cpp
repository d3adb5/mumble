// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "IPCUtils.h"

#ifndef _WIN32
#	include <format>
#	include <cstdlib>
#	include <system_error>

#	include <sys/stat.h>
#	include <unistd.h>
#endif

namespace Mumble {

std::filesystem::path getRuntimeDirectory() {
#ifdef _WIN32
	return {};
#else
	std::filesystem::path runtimeDir;

#	ifdef __APPLE__
	runtimeDir = std::filesystem::temp_directory_path() / "info.mumble.Mumble";
#	else
	const char *xdgRuntimeDir = std::getenv("XDG_RUNTIME_DIR");
	if (xdgRuntimeDir != nullptr && xdgRuntimeDir[0] != '\0') {
		runtimeDir = std::filesystem::path(xdgRuntimeDir) / "info.mumble.Mumble";
	} else {
		runtimeDir = std::filesystem::temp_directory_path() / std::format("info.mumble.Mumble_{}", getuid());
	}
#	endif

	std::filesystem::create_directories(runtimeDir);

	// Outside of $XDG_RUNTIME_DIR the directory lives in a world-writable location under a
	// predictable name, so another user could have created it first and would then control the
	// sockets placed in it (e.g. replace the RPC socket to receive the mumble:// URLs a second
	// instance forwards). Only use a real directory that we own, and keep others out of it.
	struct stat info;
	if (lstat(runtimeDir.c_str(), &info) != 0) {
		throw std::filesystem::filesystem_error("Unable to inspect the runtime directory", runtimeDir,
												std::error_code(errno, std::generic_category()));
	}
	if (!S_ISDIR(info.st_mode) || info.st_uid != getuid()) {
		throw std::filesystem::filesystem_error("The runtime directory is not a directory owned by the current user",
												runtimeDir, std::make_error_code(std::errc::permission_denied));
	}
	if ((info.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
		std::filesystem::permissions(runtimeDir, std::filesystem::perms::owner_all,
									 std::filesystem::perm_options::replace);
	}

	return runtimeDir;
#endif
}

std::filesystem::path getOverlayPipePath() {
#ifdef _WIN32
	return "MumbleOverlayPipe";
#else
	return getRuntimeDirectory() / "MumbleOverlayPipe";
#endif
}

#ifdef _WIN32
std::wstring getOverlayPipeDevicePath() {
	return LR"(\\.\pipe\)" + getOverlayPipePath().wstring();
}
#endif

std::filesystem::path getSocketPath(std::string_view basename) {
#ifdef _WIN32
	return basename;
#else
	return getRuntimeDirectory() / (std::string(basename) + "Socket");
#endif
}

} // namespace Mumble
