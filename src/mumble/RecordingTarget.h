// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_RECORDINGTARGET_H_
#define MUMBLE_MUMBLE_RECORDINGTARGET_H_

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QString>

namespace Mumble {
namespace RecordingTarget {

	/// Name a recording is written under, derived from the configured file name.
	/// A name without an extension gets the one the recording format defaults to
	/// and an empty name becomes the "%user" pattern, which the recorder expands
	/// to the name of the user being recorded.
	inline QString resolveFileName(const QString &configuredName, const QString &defaultExtension) {
		const QFileInfo info(configuredName);

		QString baseName = info.baseName();
		QString suffix   = info.completeSuffix();

		if (suffix.isEmpty()) {
			suffix = defaultExtension;
		}

		if (baseName.isEmpty()) {
			baseName = QLatin1String("%user");
		}

		return baseName + QLatin1Char('.') + suffix;
	}

	/// Path a recording is written to, built from the configured directory and the
	/// resolved file name. The patterns both of them may contain are left alone -
	/// the recorder expands them once it knows what it records.
	inline QString resolveFilePath(const QString &directory, const QString &configuredName,
								   const QString &defaultExtension) {
		return QDir(directory).absoluteFilePath(resolveFileName(configuredName, defaultExtension));
	}

} // namespace RecordingTarget
} // namespace Mumble

#endif
