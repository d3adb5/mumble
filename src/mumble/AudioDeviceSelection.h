// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_AUDIODEVICESELECTION_H_
#define MUMBLE_MUMBLE_AUDIODEVICESELECTION_H_

#include <QtCore/QList>
#include <QtCore/QPair>
#include <QtCore/QString>
#include <QtCore/QVariant>

namespace Mumble {
namespace AudioDeviceSelection {

	struct Entry {
		QString label;
		QVariant value;
		/// True for the placeholder that stands in for the configured device
		/// while it is not offered by the backend (e.g. currently unplugged).
		bool unavailable = false;
	};

	struct ComboModel {
		QList< Entry > entries;
		int currentIndex = -1;
	};

	/// Builds the model for a device combo box: every choice the backend offers plus,
	/// when the configured device is not among them, a placeholder entry for it. That
	/// way an unplugged device stays visible and selected instead of the selection
	/// silently jumping to another device.
	inline ComboModel buildComboModel(const QList< QPair< QString, QVariant > > &choices, const QVariant &current) {
		ComboModel model;

		for (const auto &choice : choices) {
			model.entries.append({ choice.first, choice.second, false });
			if (model.currentIndex < 0 && choice.second == current) {
				model.currentIndex = static_cast< int >(model.entries.size()) - 1;
			}
		}

		if (model.currentIndex < 0) {
			if (current.isValid() && !current.toString().isEmpty()) {
				model.entries.append({ current.toString(), current, true });
				model.currentIndex = static_cast< int >(model.entries.size()) - 1;
			} else if (!model.entries.isEmpty()) {
				model.currentIndex = 0;
			}
		}

		return model;
	}

} // namespace AudioDeviceSelection
} // namespace Mumble

#endif
