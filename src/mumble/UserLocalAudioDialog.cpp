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
static constexpr int AMP_SLIDER_MAX = static_cast< int >(Mumble::Amplification::AGC_TARGET) - 500;

static int loudnessFromAmpSlider(int value) {
	return static_cast< int >(Mumble::Amplification::AGC_TARGET) - value;
}

static int ampSliderFromLoudness(int loudness) {
	return qBound(0, static_cast< int >(Mumble::Amplification::AGC_TARGET) - loudness, AMP_SLIDER_MAX);
}

// Slider value matching a given linear gain factor, for the live indicator.
static int ampSliderFromFactor(float factor) {
	factor = qMax(factor, 1.0f);
	return qBound(0, static_cast< int >(Mumble::Amplification::AGC_TARGET * (1.0f - 1.0f / factor) + 0.5f),
				  AMP_SLIDER_MAX);
}

// The multiplication sign used to present amplification factors, e.g. "2.50x".
static const QChar AMP_TIMES = QChar(static_cast< char16_t >(0x00D7));

static QString ampFactorText(float factor) {
	return QString::number(static_cast< double >(factor), 'f', 2) + AMP_TIMES;
}

// Formats an amplification slider value as its gain factor, e.g. "2.50x".
static QString ampFactorTextFromSlider(int value) {
	return ampFactorText(Mumble::Amplification::AGC_TARGET / static_cast< float >(loudnessFromAmpSlider(value)));
}

// The SNR sliders and settings carry tenths of a dB for smooth handles.
static QString snrTextFromSlider(int value) {
	return QStringLiteral("%1 dB").arg(static_cast< double >(value) / 10.0, 0, 'f', 1);
}

// Upper end of the SNR meter and threshold slider in dB; everything above
// simply pegs the bar.
static constexpr float SNR_BAR_MAX_DB = 30.0f;
// The meter stores millibels-ish (dB x 1000) to get a smooth integer scale.
static constexpr int SNR_BAR_SCALE = 1000;

// Indicator colors matching the input settings' live markers: green for
// speech, orange for the in-between/noise state, red for silence.
static const QColor INDICATOR_SPEECH(0x22, 0xaa, 0x22);
static const QColor INDICATOR_NOISE(0xcc, 0x88, 0x00);
static const QColor INDICATOR_SILENCE(0xcc, 0x33, 0x33);

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

	// One bar, three handles for the base, adaptive and maximum amplification,
	// mirroring the input amplification settings.
	qsAmp->setRange(0, AMP_SLIDER_MAX);
	qsAmp->setSingleStep(500);
	qsAmp->setCaptions({ tr("Base"), tr("Adaptive"), tr("Max") });
	qsAmp->setValueFormatter(ampFactorTextFromSlider);

	// Two coupled handles (silence/speech) for the SNR gate, like the voice
	// activity thresholds of the input.
	qsSnr->setHandleCount(2);
	qsSnr->setRange(0, static_cast< int >(SNR_BAR_MAX_DB) * 10);
	qsSnr->setSingleStep(5);
	qsSnr->setCaptions({ tr("Silence"), tr("Speech") });
	qsSnr->setValueFormatter(snrTextFromSlider);

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
	const QSignalBlocker blockAmpSlider(qsAmp);
	const QSignalBlocker blockSnrSlider(qsSnr);

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

	// The three amplification handles, ordered base <= adaptive <= maximum
	// (the widget keeps them from crossing), and the two SNR gate handles.
	qsAmp->setValues({ ampSliderFromLoudness(settings.ampBaseLoudness),
					   ampSliderFromLoudness(settings.ampAdaptiveLoudness),
					   ampSliderFromLoudness(settings.ampMaxLoudness) });
	qsSnr->setValues({ settings.snrSilenceDb10, settings.snrSpeechDb10 });

	updateSuppressionControls();
	qlSpeexStrength->setText(tr("-%1 dB").arg(qsSpeexStrength->value()));
}

LocalAudioProcessingSettings UserLocalAudioDialog::gatherSettings() const {
	LocalAudioProcessingSettings settings;
	settings.suppressionEnabled      = qgbSuppress->isChecked();
	settings.suppressionMode         = static_cast< Settings::NoiseCancel >(qcbMethod->currentData().toInt());
	settings.speexSuppressStrength   = -qsSpeexStrength->value();
	settings.snrAmplificationEnabled = qgbAmp->isChecked();
	settings.ampBaseLoudness         = loudnessFromAmpSlider(qsAmp->value(0));
	settings.ampAdaptiveLoudness     = loudnessFromAmpSlider(qsAmp->value(1));
	settings.ampMaxLoudness          = loudnessFromAmpSlider(qsAmp->value(2));
	settings.snrSilenceDb10          = qsSnr->value(0);
	settings.snrSpeechDb10           = qsSnr->value(1);
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

void UserLocalAudioDialog::on_qsAmp_valuesChanged() {
	applyToUser();
}

void UserLocalAudioDialog::on_qsSnr_valuesChanged() {
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

	// The live frame SNR against the gate thresholds, colored by where it
	// sits, like the input's voice activity slider.
	const bool speech      = user->m_localAmpSpeech.load();
	const int frameSnrDb10 = static_cast< int >(user->m_localFrameSnrDb.load() * 10.0f + 0.5f);
	QColor snrColor        = INDICATOR_NOISE;
	if (frameSnrDb10 >= qsSnr->value(1)) {
		snrColor = INDICATOR_SPEECH;
	} else if (frameSnrDb10 < qsSnr->value(0)) {
		snrColor = INDICATOR_SILENCE;
	}
	qsSnr->setIndicator(frameSnrDb10, receiving, snrColor);

	// The amplification currently applied, marked on the level slider, and
	// whether the stream is detected as speech or noise (noise caps the gain
	// at the adaptive level).
	const QColor ampColor = speech ? INDICATOR_SPEECH : INDICATOR_NOISE;
	const float ampFactor = user->m_localAmpFactor.load();
	qsAmp->setIndicator(ampSliderFromFactor(ampFactor), receiving && qgbAmp->isChecked(), ampColor);

	if (!qgbAmp->isChecked()) {
		qlCurrentAmp->setText(tr("off"));
		qlAmpDetect->setText(QString());
	} else if (receiving) {
		qlCurrentAmp->setText(ampFactorText(ampFactor));
		qlAmpDetect->setText(QStringLiteral("<b style=\"color:%1\">%2</b>")
								 .arg(ampColor.name(), speech ? tr("speech") : tr("noise")));
	} else {
		qlCurrentAmp->setText(tr("no audio"));
		qlAmpDetect->setText(QString());
	}
}
