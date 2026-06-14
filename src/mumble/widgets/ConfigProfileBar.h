// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WIDGETS_CONFIGPROFILEBAR_H_
#define MUMBLE_MUMBLE_WIDGETS_CONFIGPROFILEBAR_H_

#include <QtCore/QString>
#include <QtWidgets/QWidget>

class ConfigWidget;
class QComboBox;
class QPushButton;

/// A reusable settings-profile bar for a configuration page. It offers a
/// dropdown of the saved profiles for a category plus "Save", "Save As..." and
/// "Delete" buttons, all operating on the page's working settings. Selecting a
/// profile loads it into the page's controls; the change is committed to the
/// running configuration through the usual OK/Apply flow, like every other
/// setting. The bar drives the generic Settings::*SettingsProfile helpers, so a
/// page opts in simply by overriding ConfigWidget::profileCategory().
class ConfigProfileBar : public QWidget {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ConfigProfileBar)

	ConfigWidget *m_page;
	QString m_category;
	QComboBox *m_combo;
	QPushButton *m_save;
	QPushButton *m_saveAs;
	QPushButton *m_delete;

	/// Repopulate the dropdown from the page's working settings, optionally
	/// selecting \p selected (an empty string selects the placeholder).
	void reload(const QString &selected);

public:
	ConfigProfileBar(ConfigWidget *page, const QString &category, QWidget *parent = nullptr);

private slots:
	void onProfileSelected(int index);
	void onSave();
	void onSaveAs();
	void onDelete();
};

#endif
