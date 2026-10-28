// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WEBCAMSTREAM_H_
#define MUMBLE_MUMBLE_WEBCAMSTREAM_H_

#include "WebcamCrypto.h"
#include "WebcamMode.h"

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtGui/QImage>

#include <atomic>
#include <memory>
#include <mutex>

typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;
typedef struct _GstMessage GstMessage;
typedef struct _GstPad GstPad;
typedef struct _GstPadProbeInfo GstPadProbeInfo;

/// A webcam GStreamer can open.
struct WebcamDevice {
	/// What the settings store: the /dev/videoN path where there is one, the display name
	/// otherwise.
	QString id;
	QString name;
	std::vector< Mumble::Webcam::Mode > modes;
};

/// The GStreamer side of webcam sharing (protocol v1): publishing a camera to a MediaMTX relay
/// over RTSPS, and reading one back, with every slice NAL encrypted end to end by the per-NAL
/// AES-GCM of WebcamCrypto.h in pad probes. No URL, credential or key is ever logged: error
/// texts are redacted before they leave this class.
namespace WebcamStream {
/// Initialises GStreamer once. False (with \p error set) if it cannot be.
bool ensureInitialized(QString *error = nullptr);

/// Every webcam the device monitor lists, one entry per device path.
std::vector< WebcamDevice > listCameras();

/// The H.264 encoders the settings offer, in the order "automatic" tries them.
QStringList encoderFactories();
} // namespace WebcamStream

/// Common lifetime of a pipeline: bus messages arrive on GStreamer threads and are handed to
/// the Qt thread, and stopping never hangs on a session that never went live.
class WebcamPipeline : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(WebcamPipeline)

public:
	explicit WebcamPipeline(QObject *parent = nullptr);
	~WebcamPipeline() override;

	/// Stops and tears the pipeline down. Safe to call at any time, also twice.
	void stop();
	bool isRunning() const { return m_pipeline != nullptr; }
	/// The last failure was the relay refusing our login (an RTSP 401).
	bool wasRefused() const { return m_refused; }

	/// Called on the Qt thread with each picture for the screen.
	void deliverFrame(const QImage &image);

signals:
	/// Frames are flowing.
	void live();
	/// A picture to show: a watched camera, or the sender's own preview (never sent).
	void frame(const QImage &image);
	/// The pipeline stopped on an error; \p message is safe to show.
	void failed(const QString &message);
	/// The other end ended the stream.
	void ended();

protected:
	/// Takes ownership of \p pipeline and starts it. Strings in \p secrets are redacted from any
	/// message shown.
	bool startPipeline(GstElement *pipeline, const QStringList &secrets);
	GstElement *element(const char *name) const;
	void markLive();
	QString redact(const QString &text) const;

	virtual QString describeError(GstMessage *message) const;

private:
	static int busSyncHandler(GstBus *bus, GstMessage *message, void *self);
	void handleMessage(GstMessage *message);

	GstElement *m_pipeline = nullptr;
	QStringList m_secrets;
	std::atomic_bool m_live{ false };
	std::atomic_bool m_eos{ false };
	/// Bumped on every start and stop, so messages of an old pipeline are dropped.
	std::atomic_uint m_generation{ 0 };
	bool m_refused = false;
};

/// Publishes a camera to a relay.
class WebcamPublisher : public WebcamPipeline {
private:
	Q_OBJECT
	Q_DISABLE_COPY(WebcamPublisher)

public:
	struct Config {
		/// Device id from listCameras(); empty for the first camera.
		QString device;
		Mumble::Webcam::Quality quality;
		/// A factory from encoderFactories(), or empty for the first one installed.
		QString encoder;
		QString url;
		QString user;
		QString password;
		/// A PEM file of CAs to trust for the relay, or empty for the system's.
		QString caFile;
		/// Also give the camera's picture to frame(), for the sender's own preview. It branches
		/// off before the encoder and never reaches the relay.
		bool preview = false;
	};

	explicit WebcamPublisher(QObject *parent = nullptr);

	/// Opens the camera and starts publishing under \p key. False with \p error set when it
	/// cannot even start; later errors come as failed().
	bool start(const Config &config, std::uint8_t kid, const Mumble::Webcam::Bytes &key, QString *error);
	/// A key-only rotation: NALs from now on go out under the new key.
	void setKey(std::uint8_t kid, const Mumble::Webcam::Bytes &key);

	/// What the camera was opened at, for share-state.
	Mumble::Webcam::Quality sending() const { return m_sending; }

protected:
	QString describeError(GstMessage *message) const override;

private:
	static unsigned int encryptProbe(GstPad *pad, GstPadProbeInfo *info, void *self);

	std::mutex m_keyMutex;
	std::uint8_t m_kid = 0;
	Mumble::Webcam::Bytes m_key;
	Mumble::Webcam::Quality m_sending{ 0, 0, 0, 0 };
	/// The relay's host:port, for errors (the path is a secret, the host is not).
	QString m_relay;
};

/// Reads a camera back from a relay and hands out decoded frames.
/// The camera alone, opened and scaled as it would be sent at a quality, for a preview in the
/// settings. Nothing leaves the machine.
class WebcamCameraPreview : public WebcamPipeline {
private:
	Q_OBJECT
	Q_DISABLE_COPY(WebcamCameraPreview)

public:
	explicit WebcamCameraPreview(QObject *parent = nullptr);

	/// Opens \p device (empty for the first camera) as for \p quality. False with \p error set
	/// when it cannot even start; later errors come as failed().
	bool start(const QString &device, const Mumble::Webcam::Quality &quality, QString *error);

protected:
	QString describeError(GstMessage *message) const override;
};

class WebcamViewerStream : public WebcamPipeline {
private:
	Q_OBJECT
	Q_DISABLE_COPY(WebcamViewerStream)

public:
	struct Config {
		QString url;
		QString user;
		QString password;
		QString caFile;
		int latencyMs = 150;
	};

	explicit WebcamViewerStream(QObject *parent = nullptr);

	bool start(const Config &config, QString *error);
	/// Adds a key, keeping the previous one for the frames still in flight.
	void addKey(std::uint8_t kid, const Mumble::Webcam::Bytes &key);

private:
	static unsigned int decryptProbe(GstPad *pad, GstPadProbeInfo *info, void *self);

	std::mutex m_keyMutex;
	Mumble::Webcam::KeyRing m_keys;
	/// Until an IDR decrypts, nothing but IDRs goes to the decoder.
	bool m_waitingForIdr = true;
};

#endif
