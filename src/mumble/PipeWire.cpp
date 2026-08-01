// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PipeWire.h"

#include "Audio.h"
#include "Global.h"

#include <cstring>

#include <pipewire/core.h>
#include <pipewire/keys.h>
#include <pipewire/stream.h>

#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/utils/dict.h>
#include <spa/utils/hook.h>

// Not part of the bundled header snapshot.
#ifndef PW_TYPE_INTERFACE_Node
#	define PW_TYPE_INTERFACE_Node "PipeWire:Interface:Node"
#endif
#ifndef PW_KEY_TARGET_OBJECT
#	define PW_KEY_TARGET_OBJECT "target.object"
#endif

#define RESOLVE(var)                                                  \
	{                                                                 \
		var = reinterpret_cast< decltype(var) >(m_lib.resolve(#var)); \
		if (!var)                                                     \
			return;                                                   \
	}

static std::unique_ptr< PipeWireSystem > pws;

class PipeWireInputRegistrar : public AudioInputRegistrar {
private:
	AudioInput *create() override;
	const QVariant getDeviceChoice() override;
	const QList< audioDevice > getDeviceChoices() override;
	void setDeviceChoice(const QVariant &, Settings &) override;
	bool canEcho(EchoCancelOptionID, const QString &) const override;
	bool isMicrophoneAccessDeniedByOS() override;

public:
	PipeWireInputRegistrar();
};

class PipeWireOutputRegistrar : public AudioOutputRegistrar {
private:
	AudioOutput *create() override;
	const QVariant getDeviceChoice() override;
	const QList< audioDevice > getDeviceChoices() override;
	void setDeviceChoice(const QVariant &, Settings &) override;
	const QList< audioDevice > getChannelLayouts() override;
	const QVariant getChannelLayoutChoice() override;
	void setChannelLayoutChoice(const QVariant &, Settings &) override;
	bool usesOutputDelay() const override;

public:
	PipeWireOutputRegistrar();
};

class PipeWireInit : public DeferInit {
private:
	std::unique_ptr< PipeWireInputRegistrar > inputRegistrar;
	std::unique_ptr< PipeWireOutputRegistrar > outputRegistrar;
	void initialize() override;
	void destroy() override;
};

PipeWireInputRegistrar::PipeWireInputRegistrar() : AudioInputRegistrar(QStringLiteral("PipeWire"), 100) {
}

AudioInput *PipeWireInputRegistrar::create() {
	return new PipeWireInput;
}

const QVariant PipeWireInputRegistrar::getDeviceChoice() {
	return Global::get().s.qsPipeWireInputDevice;
}

const QList< audioDevice > PipeWireInputRegistrar::getDeviceChoices() {
	return pws->inputDevices();
}

void PipeWireInputRegistrar::setDeviceChoice(const QVariant &choice, Settings &settings) {
	settings.qsPipeWireInputDevice = choice.toString();
}

bool PipeWireInputRegistrar::canEcho(EchoCancelOptionID, const QString &) const {
	return false;
}

bool PipeWireInputRegistrar::isMicrophoneAccessDeniedByOS() {
	return false;
}

PipeWireOutputRegistrar::PipeWireOutputRegistrar() : AudioOutputRegistrar(QStringLiteral("PipeWire"), 100) {
}

AudioOutput *PipeWireOutputRegistrar::create() {
	return new PipeWireOutput;
}

const QVariant PipeWireOutputRegistrar::getDeviceChoice() {
	return Global::get().s.qsPipeWireOutputDevice;
}

const QList< audioDevice > PipeWireOutputRegistrar::getDeviceChoices() {
	return pws->outputDevices();
}

void PipeWireOutputRegistrar::setDeviceChoice(const QVariant &choice, Settings &settings) {
	settings.qsPipeWireOutputDevice = choice.toString();
}

const QList< audioDevice > PipeWireOutputRegistrar::getChannelLayouts() {
	return { audioDevice(QStringLiteral("Mono"), 1), audioDevice(QStringLiteral("Stereo"), 2),
			 audioDevice(QStringLiteral("2.1"), 3),  audioDevice(QStringLiteral("3.1"), 4),
			 audioDevice(QStringLiteral("5.1"), 6),  audioDevice(QStringLiteral("7.1"), 8) };
}

const QVariant PipeWireOutputRegistrar::getChannelLayoutChoice() {
	return Global::get().s.pipeWireOutput;
}

void PipeWireOutputRegistrar::setChannelLayoutChoice(const QVariant &choice, Settings &settings) {
	settings.pipeWireOutput = static_cast< std::uint8_t >(choice.toUInt());
}

bool PipeWireOutputRegistrar::usesOutputDelay() const {
	return false;
}

void PipeWireInit::initialize() {
	pws = std::make_unique< PipeWireSystem >();

	if (pws->m_ok) {
		inputRegistrar  = std::make_unique< PipeWireInputRegistrar >();
		outputRegistrar = std::make_unique< PipeWireOutputRegistrar >();
	} else {
		pws.reset();
	}
}

void PipeWireInit::destroy() {
	inputRegistrar.reset();
	outputRegistrar.reset();
	pws.reset();
}

static PipeWireInit pwi;

PipeWireSystem::PipeWireSystem() : m_ok(false), m_users(0) {
	// clang-format off
	const QStringList names {
		// Common names used by Linux distributions.
		"libpipewire.so",
		"libpipewire-0.3.so",
		// Name used by the Flatpak FreeDesktop runtime.
		"libpipewire-0.3.so.0"
	};
	// clang-format on

	for (const auto &name : names) {
		m_lib.setFileName(name);
		if (m_lib.load()) {
			break;
		}
	}

	if (!m_lib.isLoaded()) {
		return;
	}

	RESOLVE(pw_get_library_version);
	RESOLVE(pw_init);
	RESOLVE(pw_deinit);
	RESOLVE(pw_loop_new);
	RESOLVE(pw_loop_destroy);
	RESOLVE(pw_thread_loop_new_full);
	RESOLVE(pw_thread_loop_destroy);
	RESOLVE(pw_thread_loop_start);
	RESOLVE(pw_thread_loop_stop);
	RESOLVE(pw_thread_loop_lock);
	RESOLVE(pw_thread_loop_unlock);
	RESOLVE(pw_properties_new);
	RESOLVE(pw_properties_set);
	RESOLVE(pw_context_new);
	RESOLVE(pw_context_destroy);
	RESOLVE(pw_context_connect);
	RESOLVE(pw_core_disconnect);
	RESOLVE(pw_proxy_destroy);
	RESOLVE(pw_stream_new_simple);
	RESOLVE(pw_stream_set_active);
	RESOLVE(pw_stream_destroy);
	RESOLVE(pw_stream_connect);
	RESOLVE(pw_stream_dequeue_buffer);
	RESOLVE(pw_stream_queue_buffer);

	qInfo("PipeWire %s from %s", pw_get_library_version(), qPrintable(m_lib.fileName()));

	pw_init(nullptr, nullptr);

	m_ok = true;

	startMonitor();
}

PipeWireSystem::~PipeWireSystem() {
	if (m_ok) {
		stopMonitor();
		pw_deinit();
	}
}

void PipeWireSystem::startMonitor() {
	static const pw_registry_events registryEvents = {
		PW_VERSION_REGISTRY_EVENTS,
		onRegistryGlobal,
		onRegistryGlobalRemove,
	};

	m_monitorLoop = pw_loop_new(nullptr);
	if (!m_monitorLoop) {
		return;
	}

	m_monitorThread = pw_thread_loop_new_full(m_monitorLoop, "DeviceMonitor", nullptr);
	if (!m_monitorThread) {
		return;
	}

	m_monitorContext = pw_context_new(m_monitorLoop, nullptr, 0);
	if (!m_monitorContext) {
		return;
	}

	if (pw_thread_loop_start(m_monitorThread) != 0) {
		return;
	}

	pw_thread_loop_lock(m_monitorThread);

	// This fails when no PipeWire daemon is running; the device lists then
	// simply stay empty while the streams keep behaving as before.
	m_monitorCore = pw_context_connect(m_monitorContext, nullptr, 0);
	if (m_monitorCore) {
		m_registry = pw_core_get_registry(m_monitorCore, PW_VERSION_REGISTRY, 0);
		if (m_registry) {
			// make_unique value-initializes, i.e. the hook starts out zeroed.
			m_registryListener = std::make_unique< spa_hook >();
			pw_registry_add_listener(m_registry, m_registryListener.get(), &registryEvents, this);
		}
	} else {
		qWarning("PipeWireSystem: Unable to connect the device monitor to the PipeWire daemon");
	}

	pw_thread_loop_unlock(m_monitorThread);
}

void PipeWireSystem::stopMonitor() {
	if (m_monitorThread) {
		pw_thread_loop_stop(m_monitorThread);
	}

	if (m_registry) {
		pw_proxy_destroy(reinterpret_cast< pw_proxy * >(m_registry));
		m_registry = nullptr;
	}

	if (m_monitorCore) {
		pw_core_disconnect(m_monitorCore);
		m_monitorCore = nullptr;
	}

	if (m_monitorContext) {
		pw_context_destroy(m_monitorContext);
		m_monitorContext = nullptr;
	}

	if (m_monitorThread) {
		pw_thread_loop_destroy(m_monitorThread);
		m_monitorThread = nullptr;
	}

	if (m_monitorLoop) {
		pw_loop_destroy(m_monitorLoop);
		m_monitorLoop = nullptr;
	}
}

void PipeWireSystem::onRegistryGlobal(void *data, uint32_t id, uint32_t, const char *type, uint32_t,
									  const spa_dict *props) {
	if (!type || !props || strcmp(type, PW_TYPE_INTERFACE_Node) != 0) {
		return;
	}

	const char *mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
	if (!mediaClass) {
		return;
	}

	// "Audio/Source/Virtual" covers virtual microphones; sink monitors are not
	// separate nodes in PipeWire, so they cannot show up here.
	const bool isSink   = strcmp(mediaClass, "Audio/Sink") == 0;
	const bool isSource = strcmp(mediaClass, "Audio/Source") == 0 || strcmp(mediaClass, "Audio/Source/Virtual") == 0;
	if (!isSink && !isSource) {
		return;
	}

	const char *name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
	if (!name) {
		return;
	}

	const char *description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
	if (!description) {
		description = spa_dict_lookup(props, PW_KEY_NODE_NICK);
	}
	if (!description) {
		description = name;
	}

	PipeWireSystem *pws = static_cast< PipeWireSystem * >(data);
	{
		QMutexLocker lock(&pws->m_nodesMutex);
		pws->m_nodes.insert(id, { QString::fromUtf8(name), QString::fromUtf8(description), isSink });
	}

	AudioDeviceMonitor::notifyChanged();
}

void PipeWireSystem::onRegistryGlobalRemove(void *data, uint32_t id) {
	PipeWireSystem *pws = static_cast< PipeWireSystem * >(data);

	bool removed = false;
	{
		QMutexLocker lock(&pws->m_nodesMutex);
		removed = pws->m_nodes.remove(id) > 0;
	}

	if (removed) {
		AudioDeviceMonitor::notifyChanged();
	}
}

const QList< audioDevice > PipeWireSystem::inputDevices() {
	QList< audioDevice > choices{ audioDevice(tr("Default Device"), QString()) };

	QMutexLocker lock(&m_nodesMutex);
	QList< audioDevice > nodes;
	for (const PipeWireNode &node : m_nodes) {
		if (!node.isSink) {
			nodes << audioDevice(node.description, node.name);
		}
	}
	lock.unlock();

	std::sort(nodes.begin(), nodes.end(),
			  [](const audioDevice &a, const audioDevice &b) { return a.first < b.first; });

	return choices + nodes;
}

const QList< audioDevice > PipeWireSystem::outputDevices() {
	QList< audioDevice > choices{ audioDevice(tr("Default Device"), QString()) };

	QMutexLocker lock(&m_nodesMutex);
	QList< audioDevice > nodes;
	for (const PipeWireNode &node : m_nodes) {
		if (node.isSink) {
			nodes << audioDevice(node.description, node.name);
		}
	}
	lock.unlock();

	std::sort(nodes.begin(), nodes.end(),
			  [](const audioDevice &a, const audioDevice &b) { return a.first < b.first; });

	return choices + nodes;
}

PipeWireEngine::PipeWireEngine(const char *category, const QByteArray &target, void *param,
							   const std::function< void(void *param) > callback)
	: m_ok(false), m_loop(nullptr), m_stream(nullptr), m_thread(nullptr) {
	if (!pws->isOk()) {
		return;
	}

	m_loop = pws->pw_loop_new(nullptr);
	if (!m_loop) {
		return;
	}

	pw_properties *props =
		pws->pw_properties_new(PW_KEY_APP_NAME, "Mumble", PW_KEY_NODE_NAME, category, PW_KEY_MEDIA_CATEGORY, category,
							   PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_ROLE, "Communication", nullptr);

	if (!target.isEmpty()) {
		// Servers too old to know target.object simply ignore it and keep the
		// default routing.
		pws->pw_properties_set(props, PW_KEY_TARGET_OBJECT, target.constData());
	}

	m_events          = std::make_unique< pw_stream_events >();
	m_events->version = PW_VERSION_STREAM_EVENTS;
	m_events->process = *callback.target< void (*)(void *) >();

	m_stream = pws->pw_stream_new_simple(m_loop, category, props, m_events.get(), param);
	if (!m_stream) {
		return;
	}

	m_thread = pws->pw_thread_loop_new_full(m_loop, category, nullptr);
	if (m_thread) {
		m_ok = true;
	}
}

PipeWireEngine::~PipeWireEngine() {
	if (!pws->isOk()) {
		return;
	}

	if (m_stream) {
		pws->pw_stream_destroy(m_stream);
	}

	if (m_thread) {
		pws->pw_thread_loop_destroy(m_thread);
	}


	if (m_loop) {
		pws->pw_loop_destroy(m_loop);
	}
}

bool PipeWireEngine::connect(const uint8_t direction, const uint32_t *channels, const uint8_t nChannels) {
	uint8_t buffer[1024];

	spa_pod_builder builder{};
	builder.data = buffer;
	builder.size = sizeof(buffer);

	spa_audio_info_raw info{};
	info.channels = nChannels;
	info.format   = SPA_AUDIO_FORMAT_F32;
	info.rate     = SAMPLE_RATE;

	if (nChannels < 2) {
		info.position[0] = SPA_AUDIO_CHANNEL_MONO;
	} else {
		for (uint8_t i = 0; i < nChannels; ++i) {
			switch (channels[i]) {
				case SPEAKER_FRONT_LEFT:
					info.position[i] = SPA_AUDIO_CHANNEL_FL;
					break;
				case SPEAKER_FRONT_RIGHT:
					info.position[i] = SPA_AUDIO_CHANNEL_FR;
					break;
				case SPEAKER_FRONT_CENTER:
					info.position[i] = SPA_AUDIO_CHANNEL_FC;
					break;
				case SPEAKER_LOW_FREQUENCY:
					info.position[i] = SPA_AUDIO_CHANNEL_LFE;
					break;
				case SPEAKER_BACK_LEFT:
					info.position[i] = SPA_AUDIO_CHANNEL_RL;
					break;
				case SPEAKER_BACK_RIGHT:
					info.position[i] = SPA_AUDIO_CHANNEL_RR;
					break;
				case SPEAKER_SIDE_LEFT:
					info.position[i] = SPA_AUDIO_CHANNEL_SL;
					break;
				case SPEAKER_SIDE_RIGHT:
					info.position[i] = SPA_AUDIO_CHANNEL_SR;
					break;
				default:
					qWarning("PipeWireSystem: Unhandled standard channel mapping 0x%x!", channels[i]);
			}
		}
	}

	const spa_pod *params[1] = { spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info) };

	return pws->pw_stream_connect(m_stream, direction, PW_ID_ANY,
								  PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS,
								  params, 1)
		   == 0;
}

void PipeWireEngine::start() {
	if (m_thread) {
		pws->pw_thread_loop_start(m_thread);
	}
}

void PipeWireEngine::stop() {
	if (m_thread) {
		pws->pw_thread_loop_stop(m_thread);
	}
}

pw_buffer *PipeWireEngine::dequeueBuffer() {
	if (m_stream) {
		return pws->pw_stream_dequeue_buffer(m_stream);
	}

	return nullptr;
}

void PipeWireEngine::queueBuffer(pw_buffer *buffer) {
	if (m_stream) {
		pws->pw_stream_queue_buffer(m_stream, buffer);
	}
}

void PipeWireEngine::setActive(const bool active) {
	// Reached from mute toggles even when the engine never came up.
	if (!m_ok) {
		return;
	}

	pws->pw_thread_loop_lock(m_thread);
	pws->pw_stream_set_active(m_stream, active);
	pws->pw_thread_loop_unlock(m_thread);
}
PipeWireInput::PipeWireInput() {
	m_engine = std::make_unique< PipeWireEngine >("Capture", Global::get().s.qsPipeWireInputDevice.toUtf8(), this,
												  processCallback);
	if (!m_engine->isOk()) {
		return;
	}

	// Stereo transmission needs both the front-left and front-right channel
	iMicChannels = Global::get().s.bStereoInput ? 2 : 1;

	constexpr uint32_t CHANNELS[]{
		SPEAKER_FRONT_LEFT,
		SPEAKER_FRONT_RIGHT,
	};

	if (!m_engine->connect(PW_DIRECTION_INPUT, CHANNELS, static_cast< std::uint8_t >(iMicChannels))) {
		return;
	}

	eMicFormat = SampleFloat;
	iMicFreq   = SAMPLE_RATE;
	initializeMixer();

	m_engine->start();
}

PipeWireInput::~PipeWireInput() {
	m_engine->stop();
}

void PipeWireInput::processCallback(void *param) {
	auto pwi = static_cast< PipeWireInput * >(param);

	pw_buffer *buffer = pwi->m_engine->dequeueBuffer();
	if (!buffer) {
		return;
	}

	spa_data &data = buffer->buffer->datas[0];
	if (!data.data) {
		return;
	}

	// addMic() expects the number of frames (samples per channel)
	pwi->addMic(data.data, static_cast< unsigned int >(data.chunk->size / sizeof(float)) / pwi->iMicChannels);

	pwi->m_engine->queueBuffer(buffer);
}

void PipeWireInput::run() {
}
void PipeWireInput::onUserMutedChanged() {
	if (bUserIsMuted && Global::get().s.bTxMuteCue) {
		// Do not disable the stream if mute cue is enabled (otherwise mute cue will not work).
		return;
	}

	m_engine->setActive(!bUserIsMuted);
}

PipeWireOutput::PipeWireOutput() {
	m_engine = std::make_unique< PipeWireEngine >("Playback", Global::get().s.qsPipeWireOutputDevice.toUtf8(), this,
												  processCallback);
	if (!m_engine->isOk()) {
		return;
	}

	iChannels = Global::get().s.pipeWireOutput;

	// This mapping works for:
	//
	// - Mono
	// - Stereo
	// - 2.1 surround
	// - 3.1 surround
	// - 5.1 surround
	// - 7.1 surround
	//
	// Ideally this should be configurable by the user.
	constexpr uint32_t CHANNELS[]{ SPEAKER_FRONT_LEFT, SPEAKER_FRONT_RIGHT, SPEAKER_LOW_FREQUENCY, SPEAKER_FRONT_CENTER,
								   SPEAKER_BACK_LEFT,  SPEAKER_BACK_RIGHT,  SPEAKER_SIDE_LEFT,     SPEAKER_SIDE_RIGHT };

	if (!m_engine->connect(PW_DIRECTION_OUTPUT, CHANNELS, static_cast< std::uint8_t >(iChannels))) {
		return;
	}

	eSampleFormat = SampleFloat;
	iMixerFreq    = SAMPLE_RATE;
	initializeMixer(CHANNELS);

	m_engine->start();
}

PipeWireOutput::~PipeWireOutput() {
	m_engine->stop();
}

void PipeWireOutput::processCallback(void *param) {
	auto pwo = static_cast< PipeWireOutput * >(param);

	pw_buffer *buffer = pwo->m_engine->dequeueBuffer();
	if (!buffer) {
		return;
	}

	spa_data &data = buffer->buffer->datas[0];
	if (!data.data) {
		return;
	}

	spa_chunk *chunk = data.chunk;
	if (!chunk) {
		return;
	}

	chunk->offset = 0;
	chunk->stride = static_cast< int >(sizeof(float) * pwo->iChannels);

	const uint32_t frames = std::min(data.maxsize / static_cast< std::uint32_t >(chunk->stride), pwo->iFrameSize);

	chunk->size = frames * static_cast< unsigned int >(chunk->stride);
	if (!pwo->mix(data.data, frames)) {
		// When the mixer has no data available to write, we still need to push silence.
		// This is to avoid an infinite loop when destroying the stream.
		// In that infinite loop, Pipewire would wait until the stream starts draining.
		// But this never happens, if we don't push new data.
		// Thus pw_stream_destroy() would block forever.
		// chunk->size is already in bytes.
		memset(data.data, 0, chunk->size);
	}

	pwo->m_engine->queueBuffer(buffer);
}

void PipeWireOutput::run() {
}

#undef RESOLVE
