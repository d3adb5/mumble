// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_TRAYMENUMODEL_H_
#define MUMBLE_MUMBLE_TRAYMENUMODEL_H_

#include <QtCore/QList>
#include <QtCore/QPair>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>

#include <algorithm>

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

	/// A section of the tray icon's context menu that the user can move around. The
	/// entry showing or hiding the main window stays at the top and the one quitting
	/// at the bottom, where a tray menu is expected to have them.
	enum class Section {
		Channels,
		AudioDevices,
		Controls,
	};

	/// The sections in the order they are shown in unless configured otherwise.
	inline QList< Section > defaultSectionOrder() {
		return { Section::Channels, Section::AudioDevices, Section::Controls };
	}

	/// The name a section is stored under in the settings.
	inline QString sectionKey(Section section) {
		switch (section) {
			case Section::Channels:
				return QStringLiteral("channels");
			case Section::AudioDevices:
				return QStringLiteral("audio_devices");
			case Section::Controls:
				break;
		}

		return QStringLiteral("controls");
	}

	/// Reads the configured order of the menu's sections. Names that are not (or no
	/// longer) known are dropped, as are repeated ones, and every section the setting
	/// does not mention is appended in its default position - so a setting written by
	/// an older or newer version still yields a complete menu.
	inline QList< Section > parseSectionOrder(const QStringList &configured) {
		QList< Section > order;

		for (const QString &name : configured) {
			for (Section section : defaultSectionOrder()) {
				if (sectionKey(section) == name && !order.contains(section)) {
					order.append(section);
				}
			}
		}

		for (Section section : defaultSectionOrder()) {
			if (!order.contains(section)) {
				order.append(section);
			}
		}

		return order;
	}

	/// Writes an order back into the form it is stored in.
	inline QStringList serializeSectionOrder(const QList< Section > &order) {
		QStringList names;

		for (Section section : order) {
			names.append(sectionKey(section));
		}

		return names;
	}

	/// Talking state of a user, mirroring Settings::TalkState. Kept separate so that
	/// this helper stays independent of the client's settings.
	enum class TalkState { Passive, Talking, MutedTalking, Whispering, Shouting };

	/// The audio-related state the tray's channel view shows for a user.
	struct UserState {
		/// True for an entry standing for a channel listener rather than a user
		/// that is actually in the channel.
		bool listener       = false;
		bool selfDeafened   = false;
		bool serverDeafened = false;
		bool selfMuted      = false;
		bool serverMuted    = false;
		bool suppressed     = false;
		bool localMuted     = false;
		TalkState talkState = TalkState::Passive;
		/// False while the user transmits nothing but silence.
		bool audible = true;
	};

	/// The symbol shown next to a user in the tray's channel view. A menu entry only
	/// carries a single icon, so - unlike the main window's tree, which has a column
	/// of status icons next to the talking one - the states have to be prioritised.
	enum class UserIcon {
		Listener,
		DeafenedSelf,
		DeafenedServer,
		MutedSelf,
		MutedServer,
		MutedSuppressed,
		MutedLocal,
		TalkingOn,
		TalkingSilent,
		TalkingMuted,
		TalkingWhisper,
		TalkingShout,
		TalkingOff,
	};

	/// Picks the symbol standing for a user's state, preferring the states that keep
	/// the user from being heard over their talking state. The order matches the one
	/// the tray icon itself uses for the local user: deafened before muted, and one's
	/// own choice before what the server imposes.
	inline UserIcon iconFor(const UserState &state) {
		if (state.listener) {
			return UserIcon::Listener;
		}
		if (state.selfDeafened) {
			return UserIcon::DeafenedSelf;
		}
		if (state.serverDeafened) {
			return UserIcon::DeafenedServer;
		}
		if (state.selfMuted) {
			return UserIcon::MutedSelf;
		}
		if (state.serverMuted) {
			return UserIcon::MutedServer;
		}
		if (state.suppressed) {
			return UserIcon::MutedSuppressed;
		}
		if (state.localMuted) {
			return UserIcon::MutedLocal;
		}

		switch (state.talkState) {
			case TalkState::Talking:
				// Hint at users that are transmitting nothing but silence, like the user list does
				return state.audible ? UserIcon::TalkingOn : UserIcon::TalkingSilent;
			case TalkState::MutedTalking:
				return UserIcon::TalkingMuted;
			case TalkState::Whispering:
				return UserIcon::TalkingWhisper;
			case TalkState::Shouting:
				return UserIcon::TalkingShout;
			case TalkState::Passive:
				break;
		}

		return UserIcon::TalkingOff;
	}

	/// A user shown in the tray's channel view.
	struct UserEntry {
		/// The user's name as the server knows it, which the entries are ordered by
		QString name;
		/// The text the entry shows, which may differ from the name (local nicknames,
		/// volume adjustments, ...)
		QString label;
		unsigned int session = 0;
		/// True for the local user, whose entry is emphasised like it is in the user list
		bool self = false;
		UserState state;
	};

	/// Orders two entries the way the main window's user list does: listeners are
	/// grouped directly above the regular users and both groups are sorted by name.
	inline bool lessThan(const UserEntry &first, const UserEntry &second) {
		if (first.state.listener != second.state.listener) {
			return first.state.listener;
		}

		// Mirrors User::lessThan: compare case-insensitively for an intuitive order,
		// falling back to a case-sensitive comparison so that names differing only in
		// casing still get a stable order.
		int result = QString::compare(first.name, second.name, Qt::CaseInsensitive);
		if (result == 0) {
			result = QString::compare(first.name, second.name, Qt::CaseSensitive);
		}

		return result < 0;
	}

	/// Sorts the entries of the channel view in place.
	inline void sortEntries(QList< UserEntry > &entries) {
		std::stable_sort(entries.begin(), entries.end(), lessThan);
	}

} // namespace TrayMenu
} // namespace Mumble

#endif
