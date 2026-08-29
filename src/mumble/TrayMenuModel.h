// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_TRAYMENUMODEL_H_
#define MUMBLE_MUMBLE_TRAYMENUMODEL_H_

#include <QtCore/QList>
#include <QtCore/QPair>
#include <QtCore/QString>
#include <QtCore/QVariant>

namespace Mumble {
namespace TrayMenu {

	/// One entry of a tray submenu mirroring one of the main window's dropdowns.
	struct Choice {
		QString label;
		QVariant value;
		/// True for the entry standing for the currently configured value.
		bool checked = false;
	};

	/// Builds the entries of a submenu offering the given choices, checking the one
	/// carrying the current value. Nothing is checked when the current value is not
	/// among the choices, which happens for a device the backend no longer offers.
	inline QList< Choice > buildChoices(const QList< QPair< QString, QVariant > > &choices, const QVariant &current) {
		QList< Choice > entries;
		bool foundCurrent = false;

		for (const auto &choice : choices) {
			const bool checked = !foundCurrent && choice.second == current;
			foundCurrent       = foundCurrent || checked;

			entries.append({ choice.first, choice.second, checked });
		}

		return entries;
	}

	/// Escapes text that is used as a menu entry's label: Qt reads an ampersand as
	/// the marker of a keyboard mnemonic, so a literal one has to be doubled.
	inline QString escapeMenuText(const QString &text) {
		QString escaped = text;

		return escaped.replace(QLatin1String("&"), QLatin1String("&&"));
	}

} // namespace TrayMenu
} // namespace Mumble

#endif
