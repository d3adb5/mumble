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

QTEST_MAIN(TestTrayMenuModel)
#include "TestTrayMenuModel.moc"
