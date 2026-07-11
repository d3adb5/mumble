// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ConfigDialog.h"

#include "AudioInput.h"
#include "AudioOutput.h"
#include "widgets/ConfigProfileBar.h"
#include "widgets/EventFilters.h"
#include "Global.h"

#include <QScrollArea>
#include <QScrollBar>
#include <QtCore/QMutexLocker>
#include <QtGui/QScreen>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>


// init static member fields
QMutex ConfigDialog::s_existingWidgetsMutex;
QHash< QString, ConfigWidget * > ConfigDialog::s_existingWidgets;

// Resolve the ConfigWidget backing a page shown in the stack, unwrapping both
// the optional QScrollArea and the optional profile-bar container.
static ConfigWidget *configWidgetForPage(QWidget *page) {
	if (!page) {
		return nullptr;
	}
	if (ConfigWidget *cw = qobject_cast< ConfigWidget * >(page)) {
		return cw;
	}
	if (QScrollArea *qsa = qobject_cast< QScrollArea * >(page)) {
		page = qsa->widget();
	}
	if (!page) {
		return nullptr;
	}
	if (ConfigWidget *cw = qobject_cast< ConfigWidget * >(page)) {
		return cw;
	}
	return page->findChild< ConfigWidget * >();
}

ConfigDialog::ConfigDialog(QWidget *p) : QDialog(p) {
	setupUi(this);

	{
		QMutexLocker lock(&s_existingWidgetsMutex);
		s_existingWidgets.clear();
	}


	s = Global::get().s;

	unsigned int idx = 0;
	for (ConfigWidgetNew cwn : *ConfigRegistrar::c_qmNew) {
		ConfigWidget *cw = cwn(s);
		{
			QMutexLocker lock(&s_existingWidgetsMutex);
			s_existingWidgets.insert(cw->getName(), cw);
		}

		addPage(cw, ++idx);
	}

	updateListView();

	QPushButton *okButton = dialogButtonBox->button(QDialogButtonBox::Ok);
	okButton->setToolTip(tr("Accept changes"));
	okButton->setWhatsThis(tr("This button will accept current settings and return to the application.<br />"
							  "The settings will be stored to disk when you leave the application."));

	QPushButton *cancelButton = dialogButtonBox->button(QDialogButtonBox::Cancel);
	cancelButton->setToolTip(tr("Reject changes"));
	cancelButton->setWhatsThis(tr("This button will reject all changes and return to the application.<br />"
								  "The settings will be reset to the previous positions."));

	QPushButton *applyButton = dialogButtonBox->button(QDialogButtonBox::Apply);
	applyButton->setToolTip(tr("Apply changes"));
	applyButton->setWhatsThis(tr("This button will immediately apply all changes."));

	QPushButton *resetButton = pageButtonBox->addButton(QDialogButtonBox::Reset);
	resetButton->setToolTip(tr("Undo changes for current page"));
	resetButton->setWhatsThis(
		tr("This button will revert any changes done on the current page to the most recent applied settings."));

	QPushButton *restoreButton = pageButtonBox->addButton(QDialogButtonBox::RestoreDefaults);
	restoreButton->setToolTip(tr("Restore defaults for current page"));
	restoreButton->setWhatsThis(
		tr("This button will restore the defaults for the settings on the current page. Other pages will not be "
		   "changed.<br />"
		   "To restore all settings to their defaults, you can press the \"Defaults (All)\" button."));

	QPushButton *restoreAllButton = pageButtonBox->addButton(tr("Defaults (All)"), QDialogButtonBox::ResetRole);
	restoreAllButton->setToolTip(tr("Restore all defaults"));
	restoreAllButton->setWhatsThis(tr("This button will restore the defaults for all settings."));
	restoreAllButton->installEventFilter(new OverrideTabOrderFilter(restoreAllButton, applyButton));

	updateTabOrder();
	qlwIcons->setFocus();
}

void ConfigDialog::addPage(ConfigWidget *cw, unsigned int idx) {
	int w = INT_MAX, h = INT_MAX;

	const QList< QScreen * > screens = qApp->screens();
	for (int i = 0; i < screens.size(); ++i) {
		const QRect ds = screens[i]->availableGeometry();
		if (ds.isValid()) {
			w = qMin(w, ds.width());
			h = qMin(h, ds.height());
		}
	}

	// Pages that support settings profiles get a profile bar above their content.
	// The bar and the page are wrapped in a container so the page's own layout is
	// left untouched, whatever its type.
	QWidget *content = cw;
	if (!cw->profileCategory().isEmpty()) {
		QWidget *container        = new QWidget();
		QVBoxLayout *containerLay = new QVBoxLayout(container);
		containerLay->setContentsMargins(0, 0, 0, 0);
		containerLay->addWidget(new ConfigProfileBar(cw, cw->profileCategory()));
		containerLay->addWidget(cw);
		content = container;
	}

	const QSize pageMin = content->minimumSizeHint();
	content->resize(pageMin);
	content->setMinimumSize(pageMin);

	// Wrap the page in a scroll area when it does not fit the available screen once
	// the dialog's own chrome (the icon list on the left and the button rows at the
	// bottom) is accounted for.
	const int widthChrome  = 128;
	const int heightChrome = 192;
	if ((pageMin.width() + widthChrome > w) || (pageMin.height() + heightChrome > h)) {
		QScrollArea *qsa = new QScrollArea();
		qsa->setFrameShape(QFrame::NoFrame);
		qsa->setWidgetResizable(true);
		qsa->setWidget(content);
		qsa->setFocusPolicy(Qt::NoFocus);

		// Open the scroll area at the page's own size, clamped to what fits on screen,
		// so the page is shown in full whenever it fits and only the dimension that
		// genuinely overflows the screen scrolls - rather than collapsing into a small,
		// doubly-scrolled box. Reserve room for the vertical scrollbar so that needing
		// one does not also force a horizontal one.
		const int scrollBarWidth = qsa->verticalScrollBar()->sizeHint().width();
		qsa->setMinimumSize(qMin(pageMin.width() + scrollBarWidth, w - widthChrome),
							qMin(pageMin.height(), h - heightChrome));

		qhPages.insert(cw, qsa);
		qswPages->addWidget(qsa);
	} else {
		qhPages.insert(cw, content);
		qswPages->addWidget(content);
	}
	qmWidgets.insert(idx, cw);
	cw->load(Global::get().s);
}

ConfigDialog::~ConfigDialog() {
	{
		QMutexLocker lock(&s_existingWidgetsMutex);
		s_existingWidgets.clear();
	}

	for (QWidget *qw : qhPages) {
		delete qw;
	}
}

ConfigWidget *ConfigDialog::getConfigWidget(const QString &name) {
	QMutexLocker lock(&s_existingWidgetsMutex);

	return s_existingWidgets.value(name, nullptr);
}

void ConfigDialog::on_pageButtonBox_clicked(QAbstractButton *b) {
	ConfigWidget *conf = configWidgetForPage(qswPages->currentWidget());
	if (!conf)
		return;
	switch (pageButtonBox->standardButton(b)) {
		case QDialogButtonBox::RestoreDefaults: {
			Settings def;
			conf->load(def);
			break;
		}
		case QDialogButtonBox::Reset: {
			conf->load(Global::get().s);
			break;
		}
		// standardButton returns NoButton for any custom buttons. The only custom button
		// in the pageButtonBox is the one for resetting all settings.
		case QDialogButtonBox::NoButton: {
			// Ask for confirmation before resetting **all** settings
			QMessageBox msgBox;
			msgBox.setIcon(QMessageBox::Question);
			msgBox.setText(QObject::tr("Reset all settings?"));
			msgBox.setInformativeText(QObject::tr("Do you really want to reset all settings (not only the ones "
												  "currently visible) to their default value?"));
			msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
			msgBox.setDefaultButton(QMessageBox::No);

			if (msgBox.exec() == QMessageBox::Yes) {
				Settings defaultSetting;
				for (ConfigWidget *cw : qmWidgets) {
					cw->load(defaultSetting);
				}
			}
			break;
		}
		default:
			break;
	}
}

void ConfigDialog::on_dialogButtonBox_clicked(QAbstractButton *b) {
	switch (dialogButtonBox->standardButton(b)) {
		case QDialogButtonBox::Apply: {
			apply();
			break;
		}
		default:
			break;
	}
}

void ConfigDialog::on_qlwIcons_currentItemChanged(QListWidgetItem *current, QListWidgetItem *previous) {
	if (!current)
		current = previous;

	if (current) {
		QWidget *w = qhPages.value(qmIconWidgets.value(current));
		if (w)
			qswPages->setCurrentWidget(w);
		if (previous) {
			updateTabOrder();
		}
	}
}

void ConfigDialog::updateTabOrder() {
	QPushButton *okButton         = dialogButtonBox->button(QDialogButtonBox::Ok);
	QPushButton *cancelButton     = dialogButtonBox->button(QDialogButtonBox::Cancel);
	QPushButton *applyButton      = dialogButtonBox->button(QDialogButtonBox::Apply);
	QPushButton *resetButton      = pageButtonBox->button(QDialogButtonBox::Reset);
	QPushButton *restoreButton    = pageButtonBox->button(QDialogButtonBox::RestoreDefaults);
	QPushButton *restoreAllButton = static_cast< QPushButton * >(pageButtonBox->buttons().last());

	QWidget *contentFocusWidget = qswPages;

	// Focus the page content (the profile-bar container when present, otherwise
	// the ConfigWidget itself) so tabbing flows through the whole page.
	QWidget *content = qswPages->currentWidget();
	if (QScrollArea *qsa = qobject_cast< QScrollArea * >(content)) {
		content = qsa->widget();
	}
	if (content) {
		contentFocusWidget = content;
	}

	setTabOrder(cancelButton, okButton);
	setTabOrder(okButton, qlwIcons);
	setTabOrder(qlwIcons, contentFocusWidget);
	if (resetButton && restoreButton && restoreAllButton) {
		setTabOrder(contentFocusWidget, resetButton);
		setTabOrder(resetButton, restoreButton);
		setTabOrder(restoreButton, restoreAllButton);
		setTabOrder(restoreAllButton, applyButton);
	} else {
		setTabOrder(contentFocusWidget, applyButton);
	}
	setTabOrder(applyButton, cancelButton);
}

void ConfigDialog::updateListView() {
	QWidget *ccw         = qmIconWidgets.value(qlwIcons->currentItem());
	QListWidgetItem *sel = nullptr;

	qmIconWidgets.clear();
	qlwIcons->clear();

	QFontMetrics qfm(qlwIcons->font());
	int configNavbarWidth = 0;

	for (ConfigWidget *cw : qmWidgets) {
		configNavbarWidth = qMax(configNavbarWidth, qfm.horizontalAdvance(cw->title()));

		QListWidgetItem *i = new QListWidgetItem(qlwIcons);
		i->setIcon(cw->icon());
		i->setText(cw->title());
		i->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

		qmIconWidgets.insert(i, cw);
		if (cw == ccw)
			sel = i;
	}

	// Add space for icon and some padding.
	configNavbarWidth += qlwIcons->iconSize().width() + 25;

	qlwIcons->setMinimumWidth(configNavbarWidth);
	qlwIcons->setMaximumWidth(configNavbarWidth);

	if (sel)
		qlwIcons->setCurrentItem(sel);
	else
		qlwIcons->setCurrentRow(0);
}

void ConfigDialog::apply() {
	Audio::stop();

	for (ConfigWidget *cw : qmWidgets) {
		cw->save();
	}

	Global::get().s = s;

	for (ConfigWidget *cw : qmWidgets) {
		cw->accept();
	}

	if (!Global::get().s.bAttenuateOthersOnTalk)
		Global::get().bAttenuateOthers = false;

	// They might have changed their keys.
	Global::get().iPushToTalk = 0;

	Audio::start();

	emit settingsAccepted();
}

void ConfigDialog::accept() {
	apply();

	// Save settings to disk
	Global::get().s.save();

	QDialog::accept();
}
