// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

// GLib's headers use "signals" as a name, which Qt defines as a keyword: they come first.
#include <gio/gio.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <gst/video/video.h>

#include "WebcamStream.h"

#include <QtCore/QMetaObject>

#include <chrono>
#include <condition_variable>
#include <map>

using namespace Mumble::Webcam;

namespace {
/// Waits for the pipeline's EOS to reach the bus before tearing it down.
struct EosWait {
	std::mutex mutex;
	std::condition_variable cv;
};
EosWait &eosWait() {
	static EosWait wait;
	return wait;
}

QString fromGChar(const gchar *text) {
	return text ? QString::fromUtf8(text) : QString();
}

/// A factory that exists, or nullptr.
GstElementFactory *findFactory(const char *name) {
	return gst_element_factory_find(name);
}

bool hasFactory(const char *name) {
	GstElementFactory *factory = findFactory(name);
	if (!factory) {
		return false;
	}
	gst_object_unref(factory);
	return true;
}

GstElement *makeElement(const char *factory, const char *name) {
	return gst_element_factory_make(factory, name);
}

/// The CA database to use for the relay, or nullptr for the system's.
GTlsDatabase *tlsDatabase(const QString &caFile) {
	if (caFile.isEmpty()) {
		return nullptr;
	}
	GError *error          = nullptr;
	GTlsDatabase *database = g_tls_file_database_new(caFile.toUtf8().constData(), &error);
	if (error) {
		g_error_free(error);
		return nullptr;
	}
	return database;
}

/// A pipeline of elements made one by one, linked in order. Nothing if any is missing.
GstElement *buildChain(GstElement *pipeline, const std::vector< GstElement * > &chain) {
	for (GstElement *e : chain) {
		if (!e) {
			for (GstElement *other : chain) {
				if (other && !GST_OBJECT_PARENT(other)) {
					gst_object_unref(other);
				}
			}
			gst_object_unref(pipeline);
			return nullptr;
		}
		gst_bin_add(GST_BIN(pipeline), e);
	}
	for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
		if (!gst_element_link(chain[i], chain[i + 1])) {
			gst_object_unref(pipeline);
			return nullptr;
		}
	}
	return pipeline;
}

GstElement *capsFilter(const char *name, const std::string &caps) {
	GstElement *filter = makeElement("capsfilter", name);
	if (filter) {
		GstCaps *parsed = gst_caps_from_string(caps.c_str());
		g_object_set(filter, "caps", parsed, nullptr);
		gst_caps_unref(parsed);
	}
	return filter;
}

/// The settings of an encoder for low-latency constrained-baseline H.264 at \p quality.
GstElement *makeEncoder(const QString &wanted, const Quality &quality) {
	QStringList candidates = WebcamStream::encoderFactories();
	if (!wanted.isEmpty()) {
		candidates = { wanted };
	}
	for (const QString &name : candidates) {
		const QByteArray factory = name.toUtf8();
		GstElement *encoder      = makeElement(factory.constData(), "enc");
		if (!encoder) {
			continue;
		}
		const guint kbps   = static_cast< guint >(std::max(32, quality.bitrateKbps));
		const guint keyInt = static_cast< guint >(std::max(1, quality.fps * 2));
		if (name == QLatin1String("x264enc")) {
			gst_util_set_object_arg(G_OBJECT(encoder), "tune", "zerolatency");
			gst_util_set_object_arg(G_OBJECT(encoder), "speed-preset", "veryfast");
			g_object_set(encoder, "bitrate", kbps, "key-int-max", keyInt, "bframes", 0u, "insert-vui", TRUE, nullptr);
		} else if (name == QLatin1String("openh264enc")) {
			gst_util_set_object_arg(G_OBJECT(encoder), "rate-control", "bitrate");
			gst_util_set_object_arg(G_OBJECT(encoder), "complexity", "low");
			g_object_set(encoder, "bitrate", kbps * 1000, "gop-size", keyInt, nullptr);
		} else {
			// The VA-API encoders (vah264enc, vah264lpenc) share these.
			g_object_set(encoder, "bitrate", kbps, "key-int-max", keyInt, "b-frames", 0u, nullptr);
			gst_util_set_object_arg(G_OBJECT(encoder), "rate-control", "cbr");
		}
		return encoder;
	}
	return nullptr;
}

/// A source element for a device id: v4l2src on a device path, the device monitor's own
/// element otherwise (the platform camera source on Windows and macOS).
GstElement *makeSource(const QString &device) {
	if (device.startsWith(QLatin1String("/dev/")) && hasFactory("v4l2src")) {
		GstElement *source = makeElement("v4l2src", "src");
		if (source) {
			g_object_set(source, "device", device.toUtf8().constData(), nullptr);
		}
		return source;
	}

	GstDeviceMonitor *monitor = gst_device_monitor_new();
	gst_device_monitor_add_filter(monitor, "Video/Source", nullptr);
	GstElement *source = nullptr;
	if (gst_device_monitor_start(monitor)) {
		GList *devices = gst_device_monitor_get_devices(monitor);
		for (GList *it = devices; it && !source; it = it->next) {
			GstDevice *candidate = GST_DEVICE(it->data);
			gchar *name          = gst_device_get_display_name(candidate);
			if (device.isEmpty() || fromGChar(name) == device) {
				source = gst_device_create_element(candidate, "src");
			}
			g_free(name);
		}
		g_list_free_full(devices, gst_object_unref);
		gst_device_monitor_stop(monitor);
	}
	gst_object_unref(monitor);
	return source;
}

/// An h264parse that repeats SPS/PPS in-band before every IDR, so a viewer joining late can
/// start (the relay does the same, but should not have to).
GstElement *parameterSetsBeforeIdr(GstElement *parse) {
	if (parse) {
		g_object_set(parse, "config-interval", -1, nullptr);
	}
	return parse;
}

std::vector< std::string > capsStrings(GstCaps *caps) {
	std::vector< std::string > out;
	if (!caps) {
		return out;
	}
	for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
		gchar *text = gst_structure_to_string(gst_caps_get_structure(caps, i));
		out.emplace_back(text ? text : "");
		g_free(text);
	}
	return out;
}

/// MediaMTX offers SRTP (RTP/SAVP with MIKEY) inside RTSPS and refuses the SETUP rtspsrc
/// answers that with. Inside TLS, with every slice already encrypted end to end, SRTP adds
/// nothing: the plain profile is read over the same TLS connection instead.
void readPlainProfile(GstElement *, GstSDPMessage *sdp, gpointer) {
	for (guint i = 0; i < gst_sdp_message_medias_len(sdp); ++i) {
		auto *media = const_cast< GstSDPMedia * >(gst_sdp_message_get_media(sdp, i));
		if (g_strcmp0(gst_sdp_media_get_proto(media), "RTP/SAVP") == 0) {
			gst_sdp_media_set_proto(media, "RTP/AVP");
		}
		for (guint a = gst_sdp_media_attributes_len(media); a-- > 0;) {
			if (g_strcmp0(gst_sdp_media_get_attribute(media, a)->key, "key-mgmt") == 0) {
				gst_sdp_media_remove_attribute(media, a);
			}
		}
	}
}

/// Replaces the buffer a probe sees with \p data, keeping its timestamps and flags.
void replaceBuffer(GstPadProbeInfo *info, GstBuffer *old, const Bytes &data) {
	GstBuffer *fresh = gst_buffer_new_allocate(nullptr, data.size(), nullptr);
	gst_buffer_fill(fresh, 0, data.data(), data.size());
	gst_buffer_copy_into(
		fresh, old,
		static_cast< GstBufferCopyFlags >(GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_META), 0,
		static_cast< gsize >(-1));
	gst_buffer_unref(old);
	GST_PAD_PROBE_INFO_DATA(info) = fresh;
}
} // namespace

// -- free functions ----------------------------------------------------------------------------

bool WebcamStream::ensureInitialized(QString *error) {
	static std::once_flag once;
	static bool ok = false;
	static QString message;
	std::call_once(once, [] {
		GError *err = nullptr;
		ok          = gst_init_check(nullptr, nullptr, &err);
		if (err) {
			message = fromGChar(err->message);
			g_error_free(err);
		}
	});
	if (!ok && error) {
		*error = message.isEmpty() ? QObject::tr("GStreamer could not be initialized.") : message;
	}
	return ok;
}

QStringList WebcamStream::encoderFactories() {
	QStringList out;
	if (!ensureInitialized()) {
		return out;
	}
	for (const char *name : { "x264enc", "openh264enc", "vah264enc", "vah264lpenc" }) {
		if (hasFactory(name)) {
			out << QString::fromLatin1(name);
		}
	}
	return out;
}

std::vector< WebcamDevice > WebcamStream::listCameras() {
	std::vector< WebcamDevice > out;
	if (!ensureInitialized()) {
		return out;
	}
	GstDeviceMonitor *monitor = gst_device_monitor_new();
	gst_device_monitor_add_filter(monitor, "Video/Source", nullptr);
	if (!gst_device_monitor_start(monitor)) {
		gst_object_unref(monitor);
		return out;
	}

	std::map< QString, std::size_t > byId;
	GList *devices = gst_device_monitor_get_devices(monitor);
	for (GList *it = devices; it; it = it->next) {
		GstDevice *device = GST_DEVICE(it->data);

		QString properties;
		if (GstStructure *props = gst_device_get_properties(device)) {
			gchar *text = gst_structure_to_string(props);
			properties  = fromGChar(text);
			g_free(text);
			gst_structure_free(props);
		}
		gchar *name = gst_device_get_display_name(device);

		WebcamDevice camera;
		camera.name = fromGChar(name);
		g_free(name);
		const std::optional< std::string > path = devicePath(properties.toStdString());
		camera.id                               = path ? QString::fromStdString(*path) : camera.name;

		GstCaps *caps = gst_device_get_caps(device);
		camera.modes  = parseCaps(capsStrings(caps));
		if (caps) {
			gst_caps_unref(caps);
		}

		// PipeWire's provider and the v4l2 one list the same camera: keep one entry per path,
		// merging what each says it can do.
		const auto existing = byId.find(camera.id);
		if (existing != byId.end()) {
			std::vector< Mode > &modes = out[existing->second].modes;
			for (const Mode &mode : camera.modes) {
				if (std::find(modes.begin(), modes.end(), mode) == modes.end()) {
					modes.push_back(mode);
				}
			}
			continue;
		}
		if (camera.modes.empty()) {
			continue;
		}
		byId[camera.id] = out.size();
		out.push_back(std::move(camera));
	}
	g_list_free_full(devices, gst_object_unref);
	gst_device_monitor_stop(monitor);
	gst_object_unref(monitor);
	return out;
}

// -- WebcamPipeline ----------------------------------------------------------------------------

WebcamPipeline::WebcamPipeline(QObject *parent) : QObject(parent) {
}

WebcamPipeline::~WebcamPipeline() {
	stop();
}

bool WebcamPipeline::startPipeline(GstElement *pipeline, const QStringList &secrets) {
	stop();
	m_pipeline = pipeline;
	m_secrets  = secrets;
	m_live     = false;
	m_eos      = false;
	++m_generation;

	GstBus *bus = gst_element_get_bus(m_pipeline);
	gst_bus_set_sync_handler(bus, reinterpret_cast< GstBusSyncHandler >(&WebcamPipeline::busSyncHandler), this,
							 nullptr);
	gst_object_unref(bus);

	if (gst_element_set_state(m_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
		// The error itself is on the bus, and reaches failed() from there.
		return false;
	}
	return true;
}

void WebcamPipeline::stop() {
	if (!m_pipeline) {
		return;
	}
	GstElement *pipeline = m_pipeline;
	m_pipeline           = nullptr;
	++m_generation;

	// EOS lets the relay close the session cleanly, but only a live session can carry it: one
	// that never went live has its streaming thread parked in the connection, and the EOS would
	// wait on that thread's stream lock forever (teevee-camera, ISSUES.md 22). NULL flushes the
	// rest.
	// The EOS is sent before the lock is taken: it may reach the bus on this very thread.
	if (m_live) {
		gst_element_send_event(pipeline, gst_event_new_eos());
		std::unique_lock< std::mutex > lock(eosWait().mutex);
		eosWait().cv.wait_for(lock, std::chrono::seconds(2), [this] { return m_eos.load(); });
	}

	GstBus *bus = gst_element_get_bus(pipeline);
	gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
	gst_object_unref(bus);

	gst_element_set_state(pipeline, GST_STATE_NULL);
	gst_object_unref(pipeline);
	m_live = false;
}

GstElement *WebcamPipeline::element(const char *name) const {
	return m_pipeline ? gst_bin_get_by_name(GST_BIN(m_pipeline), name) : nullptr;
}

void WebcamPipeline::markLive() {
	if (m_live.exchange(true)) {
		return;
	}
	const unsigned int generation = m_generation;
	QMetaObject::invokeMethod(
		this,
		[this, generation] {
			if (generation == m_generation) {
				emit live();
			}
		},
		Qt::QueuedConnection);
}

QString WebcamPipeline::redact(const QString &text) const {
	QString out = text;
	for (const QString &secret : m_secrets) {
		if (!secret.isEmpty()) {
			out.replace(secret, QLatin1String("…"));
		}
	}
	return out;
}

QString WebcamPipeline::describeError(GstMessage *message) const {
	GError *error = nullptr;
	gst_message_parse_error(message, &error, nullptr);
	QString text = error ? fromGChar(error->message) : tr("Unknown error");
	if (error) {
		g_error_free(error);
	}
	// The debug string is never used: it names the URL.
	return redact(text);
}

int WebcamPipeline::busSyncHandler(GstBus *, GstMessage *message, void *data) {
	auto *self = static_cast< WebcamPipeline * >(data);
	if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
		std::lock_guard< std::mutex > lock(eosWait().mutex);
		self->m_eos = true;
		eosWait().cv.notify_all();
	}
	if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR || GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
		self->handleMessage(message);
	}
	gst_message_unref(message);
	return GST_BUS_DROP;
}

void WebcamPipeline::handleMessage(GstMessage *message) {
	const unsigned int generation = m_generation;
	if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
		const QString text = describeError(message);
		GError *error      = nullptr;
		gst_message_parse_error(message, &error, nullptr);
		const bool refused =
			error && error->domain == GST_RESOURCE_ERROR && error->code == GST_RESOURCE_ERROR_NOT_AUTHORIZED;
		if (error) {
			g_error_free(error);
		}
		QMetaObject::invokeMethod(
			this,
			[this, generation, text, refused] {
				if (generation == m_generation) {
					stop();
					m_refused = refused;
					emit failed(text);
				}
			},
			Qt::QueuedConnection);
	} else {
		QMetaObject::invokeMethod(
			this,
			[this, generation] {
				// An EOS we asked for in stop() has bumped the generation already.
				if (generation == m_generation) {
					stop();
					emit ended();
				}
			},
			Qt::QueuedConnection);
	}
}

// -- WebcamPublisher ---------------------------------------------------------------------------

WebcamPublisher::WebcamPublisher(QObject *parent) : WebcamPipeline(parent) {
}

bool WebcamPublisher::start(const Config &config, std::uint8_t kid, const Bytes &key, QString *error) {
	if (!WebcamStream::ensureInitialized(error)) {
		return false;
	}
	if (key.size() != KEY_SIZE) {
		*error = tr("No key for the stream.");
		return false;
	}

	// The camera's modes decide how it is opened: raw where it keeps the rate, MJPG otherwise.
	std::optional< Mode > mode;
	for (const WebcamDevice &camera : WebcamStream::listCameras()) {
		if (config.device.isEmpty() || camera.id == config.device || camera.name == config.device) {
			mode = chooseMode(camera.modes, config.quality.height, config.quality.fps);
			break;
		}
	}
	if (!mode) {
		*error = tr("The camera could not be found.");
		return false;
	}

	const std::pair< int, int > size = outputSize(*mode, config.quality.height);
	const int fps                    = std::max(1, std::min(config.quality.fps, static_cast< int >(mode->fps() + 0.5)));
	m_sending                        = { size.first, size.second, fps, config.quality.bitrateKbps };

	GstElement *encoder = makeEncoder(config.encoder, m_sending);
	if (!encoder) {
		*error = config.encoder.isEmpty() ? tr("No H.264 encoder is installed (x264, OpenH264 or VA-API).")
										  : tr("The H.264 encoder %1 is not installed.").arg(config.encoder);
		return false;
	}

	GstElement *pipeline              = gst_pipeline_new("webcam-publish");
	GstElement *source                = makeSource(config.device);
	std::vector< GstElement * > chain = { source, capsFilter("srccaps", mode->caps()) };
	if (mode->jpeg) {
		chain.push_back(makeElement("jpegdec", nullptr));
	}
	const std::string rawCaps = "video/x-raw,format=I420,width=" + std::to_string(size.first)
								+ ",height=" + std::to_string(size.second) + ",framerate=" + std::to_string(fps)
								+ "/1,pixel-aspect-ratio=1/1";
	GstElement *sink          = makeElement("rtspclientsink", "sink");
	chain.insert(chain.end(), { makeElement("videoconvert", nullptr), makeElement("videoscale", nullptr),
								makeElement("videorate", nullptr), capsFilter("rawcaps", rawCaps), encoder,
								capsFilter("profile", "video/x-h264,profile=constrained-baseline"),
								parameterSetsBeforeIdr(makeElement("h264parse", nullptr)),
								capsFilter("h264caps", "video/x-h264,stream-format=avc,alignment=au"),
								makeElement("queue", nullptr), sink });
	if (!source || !sink) {
		for (GstElement *e : chain) {
			if (e) {
				gst_object_unref(e);
			}
		}
		gst_object_unref(pipeline);
		*error = !source ? tr("The camera could not be opened.")
						 : tr("GStreamer's RTSP client (rtspclientsink, from gst-rtsp-server) is not installed.");
		return false;
	}

	pipeline = buildChain(pipeline, chain);
	if (!pipeline) {
		*error = tr("The camera pipeline could not be built: a GStreamer plugin is missing.");
		return false;
	}

	g_object_set(sink, "location", config.url.toUtf8().constData(), "latency", 0u, nullptr);
	gst_util_set_object_arg(G_OBJECT(sink), "protocols", "tcp");
	if (!config.user.isEmpty()) {
		g_object_set(sink, "user-id", config.user.toUtf8().constData(), "user-pw", config.password.toUtf8().constData(),
					 nullptr);
	}
	if (GTlsDatabase *database = tlsDatabase(config.caFile)) {
		g_object_set(sink, "tls-database", database, nullptr);
		g_object_unref(database);
	}

	{
		std::lock_guard< std::mutex > lock(m_keyMutex);
		m_kid = kid;
		m_key = key;
	}
	GstElement *h264caps = gst_bin_get_by_name(GST_BIN(pipeline), "h264caps");
	GstPad *pad          = gst_element_get_static_pad(h264caps, "src");
	gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
					  reinterpret_cast< GstPadProbeCallback >(&WebcamPublisher::encryptProbe), this, nullptr);
	gst_object_unref(pad);
	gst_object_unref(h264caps);

	if (!startPipeline(pipeline, { config.url, config.user, config.password })) {
		*error = tr("The camera could not be started.");
		return false;
	}
	return true;
}

void WebcamPublisher::setKey(std::uint8_t kid, const Bytes &key) {
	std::lock_guard< std::mutex > lock(m_keyMutex);
	m_kid = kid;
	m_key = key;
}

QString WebcamPublisher::describeError(GstMessage *message) const {
	GError *error = nullptr;
	gst_message_parse_error(message, &error, nullptr);
	const bool busy = error && error->domain == GST_RESOURCE_ERROR && error->code == GST_RESOURCE_ERROR_BUSY;
	if (error) {
		g_error_free(error);
	}
	if (busy) {
		return tr("The camera is in use by another application.");
	}
	return WebcamPipeline::describeError(message);
}

unsigned int WebcamPublisher::encryptProbe(GstPad *, GstPadProbeInfo *info, void *data) {
	auto *self        = static_cast< WebcamPublisher * >(data);
	GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);

	GstMapInfo map;
	if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
		return GST_PAD_PROBE_DROP;
	}
	std::optional< Bytes > sealed;
	{
		std::lock_guard< std::mutex > lock(self->m_keyMutex);
		sealed = encryptAccessUnit(map.data, map.size, self->m_key, self->m_kid);
	}
	gst_buffer_unmap(buffer, &map);

	// Never let a unit out unencrypted.
	if (!sealed) {
		return GST_PAD_PROBE_DROP;
	}
	replaceBuffer(info, buffer, *sealed);
	self->markLive();
	return GST_PAD_PROBE_OK;
}

// -- WebcamViewerStream ------------------------------------------------------------------------

namespace {
GstFlowReturn onNewSample(GstAppSink *sink, gpointer data);
}

WebcamViewerStream::WebcamViewerStream(QObject *parent) : WebcamPipeline(parent) {
}

bool WebcamViewerStream::start(const Config &config, QString *error) {
	if (!WebcamStream::ensureInitialized(error)) {
		return false;
	}

	GstElement *pipeline = gst_pipeline_new("webcam-view");
	GstElement *source   = makeElement("rtspsrc", "src");
	GstElement *depay    = makeElement("rtph264depay", "depay");
	GstElement *sink     = makeElement("appsink", "out");
	if (!source || !depay || !sink) {
		for (GstElement *e : { source, depay, sink }) {
			if (e) {
				gst_object_unref(e);
			}
		}
		gst_object_unref(pipeline);
		*error = tr("GStreamer's RTSP or H.264 plugins are not installed.");
		return false;
	}

	g_object_set(source, "location", config.url.toUtf8().constData(), "latency",
				 static_cast< guint >(std::max(0, config.latencyMs)), nullptr);
	gst_util_set_object_arg(G_OBJECT(source), "protocols", "tcp");
	g_signal_connect(source, "on-sdp", G_CALLBACK(readPlainProfile), nullptr);
	if (!config.user.isEmpty()) {
		g_object_set(source, "user-id", config.user.toUtf8().constData(), "user-pw",
					 config.password.toUtf8().constData(), nullptr);
	}
	if (GTlsDatabase *database = tlsDatabase(config.caFile)) {
		g_object_set(source, "tls-database", database, nullptr);
		g_object_unref(database);
	}

	gst_bin_add(GST_BIN(pipeline), source);
	pipeline = buildChain(pipeline, { depay, capsFilter("avccaps", "video/x-h264,stream-format=avc,alignment=au"),
									  makeElement("h264parse", nullptr), makeElement("decodebin", "dec") });
	if (!pipeline) {
		*error = tr("The video pipeline could not be built: a GStreamer plugin is missing.");
		return false;
	}

	// rtspsrc and decodebin only grow their pads once the stream is known.
	g_signal_connect(source, "pad-added", G_CALLBACK(+[](GstElement *, GstPad *pad, gpointer target) {
						 GstPad *sinkPad = gst_element_get_static_pad(GST_ELEMENT(target), "sink");
						 if (!gst_pad_is_linked(sinkPad)) {
							 gst_pad_link(pad, sinkPad);
						 }
						 gst_object_unref(sinkPad);
					 }),
					 depay);

	GstElement *convert = makeElement("videoconvert", nullptr);
	GstElement *rgb     = capsFilter(nullptr, "video/x-raw,format=RGBx");
	if (!convert || !rgb) {
		gst_object_unref(pipeline);
		*error = tr("The video pipeline could not be built: a GStreamer plugin is missing.");
		return false;
	}
	gst_bin_add_many(GST_BIN(pipeline), convert, rgb, sink, nullptr);
	gst_element_link_many(convert, rgb, sink, nullptr);
	GstElement *decoder = gst_bin_get_by_name(GST_BIN(pipeline), "dec");
	g_signal_connect(decoder, "pad-added", G_CALLBACK(+[](GstElement *, GstPad *pad, gpointer target) {
						 GstPad *sinkPad = gst_element_get_static_pad(GST_ELEMENT(target), "sink");
						 if (!gst_pad_is_linked(sinkPad)) {
							 gst_pad_link(pad, sinkPad);
						 }
						 gst_object_unref(sinkPad);
					 }),
					 convert);
	gst_object_unref(decoder);

	g_object_set(sink, "sync", TRUE, "max-buffers", 2u, "drop", TRUE, nullptr);
	GstAppSinkCallbacks callbacks = {};
	callbacks.new_sample          = &onNewSample;
	gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, this, nullptr);

	{
		std::lock_guard< std::mutex > lock(m_keyMutex);
		m_waitingForIdr = true;
	}
	GstElement *avccaps = gst_bin_get_by_name(GST_BIN(pipeline), "avccaps");
	GstPad *pad         = gst_element_get_static_pad(avccaps, "src");
	gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
					  reinterpret_cast< GstPadProbeCallback >(&WebcamViewerStream::decryptProbe), this, nullptr);
	gst_object_unref(pad);
	gst_object_unref(avccaps);

	if (!startPipeline(pipeline, { config.url, config.user, config.password })) {
		*error = tr("The video could not be started.");
		return false;
	}
	return true;
}

void WebcamViewerStream::addKey(std::uint8_t kid, const Bytes &key) {
	std::lock_guard< std::mutex > lock(m_keyMutex);
	m_keys.set(kid, key);
}

void WebcamViewerStream::deliverFrame(const QImage &image) {
	markLive();
	emit frame(image);
}

unsigned int WebcamViewerStream::decryptProbe(GstPad *, GstPadProbeInfo *info, void *data) {
	auto *self        = static_cast< WebcamViewerStream * >(data);
	GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);

	GstMapInfo map;
	if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
		return GST_PAD_PROBE_DROP;
	}
	std::optional< Bytes > plain;
	{
		std::lock_guard< std::mutex > lock(self->m_keyMutex);
		const bool idr = containsIdr(map.data, map.size);
		if (!self->m_waitingForIdr || idr) {
			plain = decryptAccessUnit(map.data, map.size, self->m_keys);
		}
		// A unit that fails to open, or one that depends on a unit dropped before it, never
		// reaches the decoder: it waits for the next IDR instead.
		if (plain && idr) {
			self->m_waitingForIdr = false;
		} else if (!plain) {
			self->m_waitingForIdr = true;
		}
	}
	gst_buffer_unmap(buffer, &map);

	if (!plain) {
		return GST_PAD_PROBE_DROP;
	}
	replaceBuffer(info, buffer, *plain);
	return GST_PAD_PROBE_OK;
}

namespace {
GstFlowReturn onNewSample(GstAppSink *sink, gpointer data) {
	GstSample *sample = gst_app_sink_pull_sample(sink);
	if (!sample) {
		return GST_FLOW_OK;
	}
	GstVideoInfo info;
	GstBuffer *buffer = gst_sample_get_buffer(sample);
	GstMapInfo map;
	if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) && buffer
		&& gst_buffer_map(buffer, &map, GST_MAP_READ)) {
		const QImage view(map.data, GST_VIDEO_INFO_WIDTH(&info), GST_VIDEO_INFO_HEIGHT(&info),
						  GST_VIDEO_INFO_PLANE_STRIDE(&info, 0), QImage::Format_RGBX8888);
		const QImage image = view.copy();
		gst_buffer_unmap(buffer, &map);

		auto *self = static_cast< WebcamViewerStream * >(data);
		QMetaObject::invokeMethod(self, [self, image] { self->deliverFrame(image); }, Qt::QueuedConnection);
	}
	gst_sample_unref(sample);
	return GST_FLOW_OK;
}
} // namespace
