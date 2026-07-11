// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_USERLOCALAUDIODIALOG_H_
#define MUMBLE_MUMBLE_USERLOCALAUDIODIALOG_H_

#include "QtUtils.h"

#include <unordered_map>

#include "ClientUser.h"
#include "ui_UserLocalAudioDialog.h"

class QTimer;

/// Dialog configuring the local, receive-side processing of another user's
/// audio stream: noise suppression with a selectable method and SNR-gated
/// amplification, both applied to playback only. Changes take effect live and
/// are persisted when confirmed with OK.
class UserLocalAudioDialog : public QDialog, private Ui::UserLocalAudioDialog {
	Q_OBJECT
	Q_DISABLE_COPY(UserLocalAudioDialog)

	/// The session ID of the user whose local audio processing is adjusted.
	unsigned int m_clientSession;

	/// The user's settings when entering the dialog, for Reset/Cancel.
	LocalAudioProcessingSettings m_originalSettings;
	std::unordered_map< unsigned int, qt_unique_ptr< UserLocalAudioDialog > > &m_qmTracker;

	QTimer *qtMeterTimer;

	/// Push the current widget state onto the user (live application).
	void applyToUser();
	/// Load \p settings into the widgets without re-applying them on the way.
	void loadSettings(const LocalAudioProcessingSettings &settings);
	/// Widget state as a settings object.
	LocalAudioProcessingSettings gatherSettings() const;
	void updateSuppressionControls();

public slots:
	void closeEvent(QCloseEvent *event);
	void on_qgbSuppress_toggled(bool checked);
	void on_qcbMethod_currentIndexChanged(int index);
	void on_qsSpeexStrength_valueChanged(int value);
	void on_qgbAmp_toggled(bool checked);
	void on_qsAmp_valuesChanged();
	void on_qsSnr_valuesChanged();
	void on_qbbButtons_clicked(QAbstractButton *button);
	void updateMeters();
	void reject();

public:
	static void present(unsigned int sessionId,
						std::unordered_map< unsigned int, qt_unique_ptr< UserLocalAudioDialog > > &qmTracker,
						QWidget *parent = nullptr);
	UserLocalAudioDialog(unsigned int sessionId,
						 std::unordered_map< unsigned int, qt_unique_ptr< UserLocalAudioDialog > > &qmTracker,
						 QWidget *parent = nullptr);
};

#endif
