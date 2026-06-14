// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ConfigProfileBar.h"

#include "ConfigWidget.h"
#include "Settings.h"

#include <QSignalBlocker>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizePolicy>

ConfigProfileBar::ConfigProfileBar(ConfigWidget *page, const QString &category, QWidget *parent)
	: QWidget(parent), m_page(page), m_category(category) {
	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	QLabel *label = new QLabel(tr("Profile"), this);

	m_combo = new QComboBox(this);
	m_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	m_combo->setToolTip(tr("Load a saved settings profile for this page (takes effect when you click OK or Apply)"));
	label->setBuddy(m_combo);

	m_save = new QPushButton(tr("Save"), this);
	m_save->setToolTip(tr("Update the selected profile with the current settings on this page"));

	m_saveAs = new QPushButton(tr("Save As..."), this);
	m_saveAs->setToolTip(tr("Save the current settings on this page as a new profile"));

	m_delete = new QPushButton(tr("Delete"), this);
	m_delete->setToolTip(tr("Delete the selected profile"));

	layout->addWidget(label);
	layout->addWidget(m_combo, 1);
	layout->addWidget(m_save);
	layout->addWidget(m_saveAs);
	layout->addWidget(m_delete);

	connect(m_combo, QOverload< int >::of(&QComboBox::currentIndexChanged), this,
			&ConfigProfileBar::onProfileSelected);
	connect(m_save, &QPushButton::clicked, this, &ConfigProfileBar::onSave);
	connect(m_saveAs, &QPushButton::clicked, this, &ConfigProfileBar::onSaveAs);
	connect(m_delete, &QPushButton::clicked, this, &ConfigProfileBar::onDelete);

	reload(QString());
}

void ConfigProfileBar::reload(const QString &selected) {
	const QSignalBlocker blocker(m_combo);
	m_combo->clear();
	m_combo->addItem(tr("(load a profile…)"));
	for (const QString &name : m_page->s.settingsProfileNames(m_category)) {
		m_combo->addItem(name);
	}

	const int idx = selected.isEmpty() ? 0 : m_combo->findText(selected);
	m_combo->setCurrentIndex(idx < 0 ? 0 : idx);

	const bool hasSelection = m_combo->currentIndex() > 0;
	m_save->setEnabled(hasSelection);
	m_delete->setEnabled(hasSelection);
}

void ConfigProfileBar::onProfileSelected(int index) {
	if (index <= 0) {
		m_save->setEnabled(false);
		m_delete->setEnabled(false);
		return;
	}

	// Load the profile into the working settings and refresh the page's controls.
	// It is committed to the running configuration when the user clicks OK/Apply.
	m_page->s.applySettingsProfile(m_category, m_combo->currentText());
	m_page->load(m_page->s);

	m_save->setEnabled(true);
	m_delete->setEnabled(true);
}

void ConfigProfileBar::onSave() {
	if (m_combo->currentIndex() <= 0) {
		return;
	}

	// Update the selected profile in place with the current control values; no
	// need to apply them first.
	const QString name = m_combo->currentText();
	m_page->save();
	m_page->s.saveSettingsProfile(m_category, name);
	reload(name);
}

void ConfigProfileBar::onSaveAs() {
	bool ok            = false;
	const QString name = QInputDialog::getText(this, tr("Save Profile"), tr("Profile name:"), QLineEdit::Normal,
											   QString(), &ok)
							 .trimmed();
	if (!ok || name.isEmpty()) {
		return;
	}

	// Capture the current control values, then snapshot them into the profile.
	m_page->save();
	m_page->s.saveSettingsProfile(m_category, name);
	reload(name);
}

void ConfigProfileBar::onDelete() {
	if (m_combo->currentIndex() <= 0) {
		return;
	}

	m_page->s.removeSettingsProfile(m_category, m_combo->currentText());
	reload(QString());
}
