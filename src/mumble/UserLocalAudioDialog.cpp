// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "UserLocalAudioDialog.h"

#include "AudioInputAmplification.h"
#include "ClientUser.h"
#include "Database.h"
#include "MainWindow.h"
#include "Global.h"

#include <QSignalBlocker>
#include <QtCore/QTimer>
#include <QtGui/QCloseEvent>
#include <QtWidgets/QPushButton>

#include <chrono>

// The amplification slider mirrors the input amplification's scale: the value
// is the AGC target minus the targeted loudness, so a higher value means more
// gain (see AudioConfigDialog).
static int loudnessFromAmpSlider(int value) {
	return static_cast< int >(Mumble::Amplification::AGC_TARGET) - value;
}

static int ampSliderFromLoudness(int loudness) {
	const int max = static_cast< int >(Mumble::Amplification::AGC_TARGET) - 500;
	return qBound(0, static_cast< int >(Mumble::Amplification::AGC_TARGET) - loudness, max);
}

// The multiplication sign used to present amplification factors, e.g. "2.50x".
static const QChar AMP_TIMES = QChar(static_cast< char16_t >(0x00D7));

static QString ampFactorText(float factor) {
	return QString::number(static_cast< double >(factor), 'f', 2) + AMP_TIMES;
}

// Upper end of the SNR meter in dB; everything above simply pegs the bar.
static constexpr float SNR_BAR_MAX_DB = 30.0f;
// The meter stores millibels-ish (dB x 1000) to get a smooth integer scale.
static constexpr int SNR_BAR_SCALE = 1000;

UserLocalAudioDialog::UserLocalAudioDialog(
	unsigned int sessionId, std::unordered_map< unsigned int, qt_unique_ptr< UserLocalAudioDialog > > &qmTracker,
	QWidget *parent)
	: QDialog(parent), m_clientSession(sessionId), m_qmTracker(qmTracker) {
	setupUi(this);

	// Only offer the suppression methods this build provides, mirroring the
	// audio input settings.
	qcbMethod->addItem(QStringLiteral("Speex"), static_cast< int >(Settings::NoiseCancelSpeex));
#ifdef USE_RNNOISE
	qcbMethod->addItem(QStringLiteral("RNNoise"), static_cast< int >(Settings::NoiseCancelRNN));
	qcbMethod->addItem(tr("Both"), static_cast< int >(Settings::NoiseCancelBoth));
#endif
#ifdef USE_WEBRTC_AUDIO_PROCESSING
	qcbMethod->addItem(QStringLiteral("WebRTC"), static_cast< int >(Settings::NoiseCancelWebRTC));
#endif
#ifdef USE_DEEPFILTERNET
	qcbMethod->addItem(QStringLiteral("DeepFilterNet"), static_cast< int >(Settings::NoiseCancelDeepFilter));
#endif

	// A low SNR is bad, a high one is good.
	abSnr->iMin    = 0;
	abSnr->iMax    = static_cast< int >(SNR_BAR_MAX_DB) * SNR_BAR_SCALE;
	abSnr->iBelow  = 6 * SNR_BAR_SCALE;
	abSnr->iAbove  = 15 * SNR_BAR_SCALE;
	abSnr->iValue  = 0;
	abSnr->qcBelow = Qt::red;
	abSnr->qcInside = Qt::yellow;
	abSnr->qcAbove  = Qt::green;

	ClientUser *user = ClientUser::get(sessionId);
	if (!user) {
		UserLocalAudioDialog::close();
	} else {
		setWindowTitle(tr("Local Audio Processing for %1").arg(user->qsName));
		m_originalSettings = user->getLocalAudioProcessing();
		loadSettings(m_originalSettings);
	}

	qtMeterTimer = new QTimer(this);
	connect(qtMeterTimer, &QTimer::timeout, this, &UserLocalAudioDialog::updateMeters);
	qtMeterTimer->start(100);
	updateMeters();

	if (Global::get().mw && Global::get().mw->windowFlags() & Qt::WindowStaysOnTopHint) {
		// Match a main window that is set to always stay on top, so the dialog
		// does not get hidden behind it.
		setWindowFlags(Qt::WindowStaysOnTopHint);
	}
}

void UserLocalAudioDialog::present(unsigned int sessionId,
								   std::unordered_map< unsigned int, qt_unique_ptr< UserLocalAudioDialog > > &qmTracker,
								   QWidget *parent) {
	if (qmTracker.find(sessionId) != qmTracker.end()) {
		qmTracker.at(sessionId)->show();
		qmTracker.at(sessionId)->raise();
	} else {
		qt_unique_ptr< UserLocalAudioDialog > dialog =
			make_qt_unique< UserLocalAudioDialog >(sessionId, qmTracker, parent);
		dialog->show();
		qmTracker.insert(std::make_pair(sessionId, std::move(dialog)));
	}
}

void UserLocalAudioDialog::closeEvent(QCloseEvent *event) {
	m_qmTracker.erase(m_clientSession);
	event->accept();
}

void UserLocalAudioDialog::reject() {
	// Cancelling restores the settings the dialog was opened with.
	ClientUser *user = ClientUser::get(m_clientSession);
	if (user) {
		user->setLocalAudioProcessing(m_originalSettings);
	}
	m_qmTracker.erase(m_clientSession);
	UserLocalAudioDialog::close();
}

void UserLocalAudioDialog::loadSettings(const LocalAudioProcessingSettings &settings) {
	const QSignalBlocker blockSuppress(qgbSuppress);
	const QSignalBlocker blockMethod(qcbMethod);
	const QSignalBlocker blockStrength(qsSpeexStrength);
	const QSignalBlocker blockAmp(qgbAmp);
	const QSignalBlocker blockMaxAmp(qsMaxAmp);

	qgbSuppress->setChecked(settings.suppressionEnabled);
	int methodIndex = qcbMethod->findData(static_cast< int >(settings.suppressionMode));
	if (methodIndex < 0) {
		// The stored method is not available in this build; fall back to the
		// first offered one (Speex), like the processing itself does.
		methodIndex = 0;
	}
	qcbMethod->setCurrentIndex(methodIndex);
	qsSpeexStrength->setValue(-settings.speexSuppressStrength);
	qgbAmp->setChecked(settings.snrAmplificationEnabled);
	qsMaxAmp->setValue(ampSliderFromLoudness(settings.ampMaxLoudness));

	updateSuppressionControls();
	qlSpeexStrength->setText(tr("-%1 dB").arg(qsSpeexStrength->value()));
	qlMaxAmp->setText(ampFactorText(Mumble::Amplification::AGC_TARGET
									/ static_cast< float >(loudnessFromAmpSlider(qsMaxAmp->value()))));
}

LocalAudioProcessingSettings UserLocalAudioDialog::gatherSettings() const {
	LocalAudioProcessingSettings settings;
	settings.suppressionEnabled      = qgbSuppress->isChecked();
	settings.suppressionMode         = static_cast< Settings::NoiseCancel >(qcbMethod->currentData().toInt());
	settings.speexSuppressStrength   = -qsSpeexStrength->value();
	settings.snrAmplificationEnabled = qgbAmp->isChecked();
	settings.ampMaxLoudness          = loudnessFromAmpSlider(qsMaxAmp->value());
	return settings;
}

void UserLocalAudioDialog::applyToUser() {
	ClientUser *user = ClientUser::get(m_clientSession);
	if (user) {
		user->setLocalAudioProcessing(gatherSettings());
	}
}

void UserLocalAudioDialog::updateSuppressionControls() {
	const auto method = static_cast< Settings::NoiseCancel >(qcbMethod->currentData().toInt());
	const bool speex  = (method == Settings::NoiseCancelSpeex || method == Settings::NoiseCancelBoth);
	qliSpeexStrength->setVisible(speex);
	qsSpeexStrength->setVisible(speex);
	qlSpeexStrength->setVisible(speex);
}

void UserLocalAudioDialog::on_qgbSuppress_toggled(bool) {
	applyToUser();
}

void UserLocalAudioDialog::on_qcbMethod_currentIndexChanged(int) {
	updateSuppressionControls();
	applyToUser();
}

void UserLocalAudioDialog::on_qsSpeexStrength_valueChanged(int value) {
	qlSpeexStrength->setText(tr("-%1 dB").arg(value));
	applyToUser();
}

void UserLocalAudioDialog::on_qgbAmp_toggled(bool) {
	applyToUser();
}

void UserLocalAudioDialog::on_qsMaxAmp_valueChanged(int value) {
	qlMaxAmp->setText(
		ampFactorText(Mumble::Amplification::AGC_TARGET / static_cast< float >(loudnessFromAmpSlider(value))));
	applyToUser();
}

void UserLocalAudioDialog::on_qbbButtons_clicked(QAbstractButton *button) {
	if (button == qbbButtons->button(QDialogButtonBox::Reset)) {
		loadSettings(m_originalSettings);
		applyToUser();
	} else if (button == qbbButtons->button(QDialogButtonBox::Ok)) {
		ClientUser *user = ClientUser::get(m_clientSession);
		if (user) {
			user->setLocalAudioProcessing(gatherSettings());
			if (!user->qsHash.isEmpty()) {
				Global::get().db->setUserLocalAudioProcessing(user->qsHash, user->getLocalAudioProcessing());
			} else {
				Global::get().mw->logChangeNotPermanent(QObject::tr("Local Audio Processing..."), user);
			}
		}
		UserLocalAudioDialog::close();
	} else if (button == qbbButtons->button(QDialogButtonBox::Cancel)) {
		reject();
	}
}

void UserLocalAudioDialog::updateMeters() {
	ClientUser *user = ClientUser::get(m_clientSession);
	if (!user) {
		return;
	}

	// Only show a measurement while this user's audio is actually flowing;
	// otherwise the last value would linger and mislead.
	const bool receiving = user->tLastAudioReceived.isStarted()
						   && user->tLastAudioReceived.elapsed() < std::chrono::milliseconds(1000);

	if (receiving) {
		const float snr = user->m_localSnrDb.load();
		abSnr->iValue =
			static_cast< int >(qBound(0.0f, snr, SNR_BAR_MAX_DB) * static_cast< float >(SNR_BAR_SCALE) + 0.5f);
		qlSnr->setText(tr("%1 dB").arg(static_cast< double >(snr), 0, 'f', 1));
	} else {
		abSnr->iValue = 0;
		qlSnr->setText(tr("no audio"));
	}
	abSnr->setEnabled(receiving);
	abSnr->update();

	if (!qgbAmp->isChecked()) {
		qlCurrentAmp->setText(tr("off"));
	} else if (receiving) {
		qlCurrentAmp->setText(ampFactorText(user->m_localAmpFactor.load()));
	} else {
		qlCurrentAmp->setText(tr("no audio"));
	}
}
