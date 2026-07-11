// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_CLIENTUSER_H_
#define MUMBLE_MUMBLE_CLIENTUSER_H_

#include <QtCore/QHash>
#include <QtCore/QReadWriteLock>

#include "Settings.h"
#include "Timer.h"
#include "User.h"

#include <atomic>

/// Local, receive-side audio processing configured for a single other user.
/// Applied to that user's stream during playback only - it never affects the
/// audio this client transmits.
struct LocalAudioProcessingSettings {
	/// Whether the user's stream is denoised locally.
	bool suppressionEnabled = false;
	/// Method the stream is denoised with, independent of the client's own
	/// (transmit-side) noise suppression.
	Settings::NoiseCancel suppressionMode = Settings::NoiseCancelRNN;
	/// Maximum attenuation of the noise in dB (negative number), for the
	/// Speex-based methods.
	int speexSuppressStrength = -30;
	/// Whether the stream is amplified based on its signal-to-noise ratio.
	bool snrAmplificationEnabled = false;
	/// Loudness the maximum amplification targets, on the same 1-32768 knob
	/// scale as Settings::iMinLoudness: smaller values allow more gain. This
	/// is the gain ceiling while the stream is classified as speech.
	int ampMaxLoudness = 10000;
	/// Loudness the adaptive amplification targets: the gain ceiling while
	/// the stream is classified as noise, like Settings::iAdaptiveLoudness.
	/// The default (the AGC target) means noise is not amplified at all.
	int ampAdaptiveLoudness = 30000;
	/// Loudness the base amplification floor targets: the stream is always
	/// amplified by at least this much, like Settings::iBaseLoudness. The
	/// default (the AGC target) means a 0 dB floor.
	int ampBaseLoudness = 30000;
	/// Frame SNR below which the stream counts as noise, in tenths of a dB.
	int snrSilenceDb10 = 30;
	/// Frame SNR above which the stream counts as speech, in tenths of a dB.
	/// Between the thresholds the previous classification is kept, mirroring
	/// the input's voice activity detection.
	int snrSpeechDb10 = 120;
};

class ClientUser : public QObject, public User {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ClientUser)

protected:
	float m_localVolume = 1.0f;
	QString m_localNickname;

public:
	Settings::TalkState tsState;
	Timer tLastTalkStateChange;
	/// Last time the user's audio stream was audible (above the silence threshold)
	Timer tLastAudible;
	/// Last time a frame of the user's audio stream was received and measured
	Timer tLastAudioReceived;
	bool bLocalIgnore;
	bool bLocalIgnoreTTS;
	bool bLocalMute;
	// Whether or not the user's effective output is inaudible due to volume settings, positional audio etc.
	bool volumeMute;

	float fPowerMin, fPowerMax;
	float fAverageAvailable;

	int iFrames;
	int iSequence;

	QByteArray qbaTextureFormat;
	QString qsFriendName;

	/// Local receive-side processing of this user's stream. Written by the UI
	/// thread, read by the audio output thread every frame - hence individual
	/// atomics rather than a lock.
	std::atomic< bool > m_localSuppressEnabled{ false };
	std::atomic< Settings::NoiseCancel > m_localSuppressMode{ Settings::NoiseCancelRNN };
	std::atomic< int > m_localSpeexSuppressStrength{ -30 };
	std::atomic< bool > m_localSnrAmpEnabled{ false };
	std::atomic< int > m_localAmpMaxLoudness{ 10000 };
	std::atomic< int > m_localAmpAdaptiveLoudness{ 30000 };
	std::atomic< int > m_localAmpBaseLoudness{ 30000 };
	std::atomic< int > m_localSnrSilenceDb10{ 30 };
	std::atomic< int > m_localSnrSpeechDb10{ 120 };

	/// Live measurements of this user's stream, written by the audio output
	/// thread while their audio is decoded and read by the UI.
	std::atomic< float > m_localSnrDb{ 0.0f };
	std::atomic< float > m_localFrameSnrDb{ 0.0f };
	std::atomic< bool > m_localAmpSpeech{ false };
	std::atomic< float > m_localAmpFactor{ 1.0f };

	QString getFlagsString() const;
	ClientUser(QObject *p = nullptr);

	LocalAudioProcessingSettings getLocalAudioProcessing() const;
	void setLocalAudioProcessing(const LocalAudioProcessingSettings &settings);

	float getLocalVolumeAdjustments() const;

	QString getLocalNickname() const;

	/**
	 * Determines whether a user is active or not
	 * A user is active when it is currently speaking or when the user has
	 * spoken within Settings::uiActiveTime amount of seconds.
	 */
	bool isActive();

	/// Whether the user's current transmission has recently been audible, i.e.
	/// whether they are actually heard rather than transmitting silence
	bool isAudible();

	/// Feeds the measured power (RMS) of a decoded audio frame into the
	/// audibility tracking. Called from the audio thread.
	void registerAudioPower(float rmsPower);

	static QHash< unsigned int, ClientUser * > c_qmUsers;
	static QReadWriteLock c_qrwlUsers;

	static QList< ClientUser * > c_qlTalking;
	static QReadWriteLock c_qrwlTalking;
	static QList< ClientUser * > getTalking();
	static QList< ClientUser * > getActive();

	static void sortUsersOverlay(QList< ClientUser * > &list);

	static ClientUser *get(unsigned int);
	static bool isValid(unsigned int);
	static ClientUser *add(unsigned int, QObject *p = nullptr);
	static ClientUser *match(const ClientUser *p, bool matchname = false);
	static void remove(unsigned int);
	static void remove(ClientUser *);

protected:
	static bool lessThanOverlay(const ClientUser *, const ClientUser *);

	/// Last audibility state reported via talkingStateChanged()
	bool m_lastAudibleState = false;
public slots:
	void setTalking(Settings::TalkState ts);
	void setMute(bool mute);
	void setDeaf(bool deaf);
	void setSuppress(bool suppress);
	void setLocalIgnore(bool ignore);
	void setLocalIgnoreTTS(bool ignoreTTS);
	void setLocalMute(bool mute);
	void setSelfMute(bool mute);
	void setSelfDeaf(bool deaf);
	void setPrioritySpeaker(bool priority);
	void setRecording(bool recording);
	void setLocalVolumeAdjustment(float adjustment);
	void setLocalNickname(const QString &nickname);
signals:
	void talkingStateChanged();
	void muteDeafStateChanged();
	void prioritySpeakerStateChanged();
	void recordingStateChanged();
	void localVolumeAdjustmentsChanged(float newAdjustment, float oldAdjustment);
	void localNicknameChanged();
};

#endif
