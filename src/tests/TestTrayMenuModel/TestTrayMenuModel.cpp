// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "TrayMenuModel.h"

using namespace Mumble::TrayMenu;

typedef QList< QPair< QString, QVariant > > ChoiceList;

/// Unit tests for the model behind the tray icon's context menu: the entries of
/// the submenus mirroring the main window's dropdowns.
class TestTrayMenuModel : public QObject {
	Q_OBJECT
private slots:
	void checksCurrentChoice();
	void keepsChoiceOrder();
	void checksNothingForUnknownCurrent();
	void checksOnlyFirstOfDuplicateValues();
	void noChoicesAtAll();
	void escapesAmpersands();

	void listenersComeFirst();
	void sortsUsersByName();
	void sortsEqualNamesCaseSensitively();
	void keepsTalkingStateApart();
	void silentTransmissionHasOwnIcon();
	void beingUnheardBeatsTalkingState();
	void listenerIconWinsOverEverything();
};

static ChoiceList someModes() {
	ChoiceList choices;
	choices << qMakePair(QStringLiteral("No Suppression"), QVariant(0));
	choices << qMakePair(QStringLiteral("Speex"), QVariant(1));
	choices << qMakePair(QStringLiteral("RNNoise"), QVariant(2));
	return choices;
}

void TestTrayMenuModel::checksCurrentChoice() {
	const QList< Choice > entries = buildChoices(someModes(), QVariant(2));

	QCOMPARE(entries.size(), 3);
	QVERIFY(!entries.at(0).checked);
	QVERIFY(!entries.at(1).checked);
	QVERIFY(entries.at(2).checked);
}

void TestTrayMenuModel::keepsChoiceOrder() {
	const QList< Choice > entries = buildChoices(someModes(), QVariant(1));

	QCOMPARE(entries.at(0).label, QStringLiteral("No Suppression"));
	QCOMPARE(entries.at(1).label, QStringLiteral("Speex"));
	QCOMPARE(entries.at(2).label, QStringLiteral("RNNoise"));
	QCOMPARE(entries.at(1).value, QVariant(1));
}

void TestTrayMenuModel::checksNothingForUnknownCurrent() {
	// The device the settings name is not offered by the backend right now.
	ChoiceList choices;
	choices << qMakePair(QStringLiteral("Default"), QVariant(QString()));
	choices << qMakePair(QStringLiteral("Headset"), QVariant(QStringLiteral("headset")));

	const QList< Choice > entries = buildChoices(choices, QVariant(QStringLiteral("unplugged")));

	QCOMPARE(entries.size(), 2);
	for (const Choice &entry : entries) {
		QVERIFY(!entry.checked);
	}
}

void TestTrayMenuModel::checksOnlyFirstOfDuplicateValues() {
	ChoiceList choices;
	choices << qMakePair(QStringLiteral("Speakers"), QVariant(QStringLiteral("default")));
	choices << qMakePair(QStringLiteral("Default Device"), QVariant(QStringLiteral("default")));

	const QList< Choice > entries = buildChoices(choices, QVariant(QStringLiteral("default")));

	QVERIFY(entries.at(0).checked);
	QVERIFY(!entries.at(1).checked);
}

void TestTrayMenuModel::noChoicesAtAll() {
	const QList< Choice > entries = buildChoices(ChoiceList(), QVariant(QStringLiteral("headset")));

	QVERIFY(entries.isEmpty());
}

void TestTrayMenuModel::escapesAmpersands() {
	QCOMPARE(escapeMenuText(QStringLiteral("Rock & Roll")), QStringLiteral("Rock && Roll"));
	QCOMPARE(escapeMenuText(QStringLiteral("&&")), QStringLiteral("&&&&"));
	QCOMPARE(escapeMenuText(QStringLiteral("Speex & RNNoise")), QStringLiteral("Speex && RNNoise"));
	QCOMPARE(escapeMenuText(QStringLiteral("no ampersand")), QStringLiteral("no ampersand"));
}

static UserEntry user(const QString &name, const UserState &state = UserState()) {
	UserEntry entry;
	entry.name  = name;
	entry.label = name;
	entry.state = state;

	return entry;
}

static UserEntry listener(const QString &name) {
	UserState state;
	state.listener = true;

	return user(name, state);
}

static QStringList names(const QList< UserEntry > &entries) {
	QStringList result;
	for (const UserEntry &entry : entries) {
		result << entry.name;
	}

	return result;
}

void TestTrayMenuModel::listenersComeFirst() {
	// Mirrors the user list, which groups listeners directly above the regular users
	QList< UserEntry > entries = { user(QStringLiteral("Alice")), listener(QStringLiteral("Zoe")),
								   user(QStringLiteral("Bob")), listener(QStringLiteral("Adam")) };

	sortEntries(entries);

	QCOMPARE(names(entries), QStringList({ QStringLiteral("Adam"), QStringLiteral("Zoe"), QStringLiteral("Alice"),
										   QStringLiteral("Bob") }));
}

void TestTrayMenuModel::sortsUsersByName() {
	QList< UserEntry > entries = { user(QStringLiteral("charlie")), user(QStringLiteral("Bob")),
								   user(QStringLiteral("alice")) };

	sortEntries(entries);

	QCOMPARE(names(entries),
			 QStringList({ QStringLiteral("alice"), QStringLiteral("Bob"), QStringLiteral("charlie") }));
}

void TestTrayMenuModel::sortsEqualNamesCaseSensitively() {
	// Names that only differ in casing still need a stable order
	QList< UserEntry > entries = { user(QStringLiteral("bob")), user(QStringLiteral("Bob")) };

	sortEntries(entries);

	QCOMPARE(names(entries), QStringList({ QStringLiteral("Bob"), QStringLiteral("bob") }));
}

void TestTrayMenuModel::keepsTalkingStateApart() {
	UserState state;

	state.talkState = TalkState::Passive;
	QCOMPARE(iconFor(state), UserIcon::TalkingOff);

	state.talkState = TalkState::Talking;
	QCOMPARE(iconFor(state), UserIcon::TalkingOn);

	state.talkState = TalkState::MutedTalking;
	QCOMPARE(iconFor(state), UserIcon::TalkingMuted);

	state.talkState = TalkState::Whispering;
	QCOMPARE(iconFor(state), UserIcon::TalkingWhisper);

	state.talkState = TalkState::Shouting;
	QCOMPARE(iconFor(state), UserIcon::TalkingShout);
}

void TestTrayMenuModel::silentTransmissionHasOwnIcon() {
	UserState state;
	state.talkState = TalkState::Talking;
	state.audible   = false;

	QCOMPARE(iconFor(state), UserIcon::TalkingSilent);

	// Only an actual transmission can be silent
	state.talkState = TalkState::Passive;
	QCOMPARE(iconFor(state), UserIcon::TalkingOff);
}

void TestTrayMenuModel::beingUnheardBeatsTalkingState() {
	UserState state;
	state.talkState = TalkState::Talking;

	state.localMuted = true;
	QCOMPARE(iconFor(state), UserIcon::MutedLocal);

	state.suppressed = true;
	QCOMPARE(iconFor(state), UserIcon::MutedSuppressed);

	state.serverMuted = true;
	QCOMPARE(iconFor(state), UserIcon::MutedServer);

	state.selfMuted = true;
	QCOMPARE(iconFor(state), UserIcon::MutedSelf);

	state.serverDeafened = true;
	QCOMPARE(iconFor(state), UserIcon::DeafenedServer);

	state.selfDeafened = true;
	QCOMPARE(iconFor(state), UserIcon::DeafenedSelf);
}

void TestTrayMenuModel::listenerIconWinsOverEverything() {
	UserState state;
	state.listener     = true;
	state.selfDeafened = true;
	state.talkState    = TalkState::Talking;

	QCOMPARE(iconFor(state), UserIcon::Listener);
}

QTEST_MAIN(TestTrayMenuModel)
#include "TestTrayMenuModel.moc"
