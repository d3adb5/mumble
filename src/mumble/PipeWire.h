// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PIPEWIRE_H
#define MUMBLE_MUMBLE_PIPEWIRE_H

#include "AudioInput.h"
#include "AudioOutput.h"

#include <QLibrary>
#include <QMap>
#include <QMutex>
#include <QWaitCondition>

class PipeWireInit;

struct pw_buffer;
struct pw_context;
struct pw_core;
struct pw_loop;
struct pw_properties;
struct pw_proxy;
struct pw_registry;
struct pw_stream;
struct pw_stream_events;
struct pw_thread_loop;

struct spa_dict;
struct spa_hook;
struct spa_pod;

/// An audio node (source or sink) announced by the PipeWire registry.
struct PipeWireNode {
	QString name;
	QString description;
	bool isSink;
};

class PipeWireEngine {
public:
	bool isOk() { return m_ok; };

	bool connect(const uint8_t direction, const uint32_t *channels, const uint8_t nChannels);

	void start();
	void stop();

	pw_buffer *dequeueBuffer();
	void queueBuffer(pw_buffer *buffer);
	void setActive(bool active);

	/// @param target node.name of the device to connect to; empty for the default.
	PipeWireEngine(const char *category, const QByteArray &target, void *param,
				   const std::function< void(void *param) > callback);
	~PipeWireEngine();

protected:
	bool m_ok;
	pw_loop *m_loop;
	pw_stream *m_stream;
	pw_thread_loop *m_thread;
	std::unique_ptr< pw_stream_events > m_events;

private:
	Q_DISABLE_COPY(PipeWireEngine)
};

class PipeWireSystem : public QObject {
	friend PipeWireEngine;
	friend PipeWireInit;

public:
	bool isOk() { return m_ok; };

	/// Sorted device choices for the UI. Thread-safe.
	const QList< audioDevice > inputDevices();
	const QList< audioDevice > outputDevices();

	PipeWireSystem();
	~PipeWireSystem();

protected:
	bool m_ok;
	uint8_t m_users;
	QLibrary m_lib;

	// Registry monitor: a connection of its own that tracks the audio nodes
	// coming and going, feeding the device dropdowns.
	pw_loop *m_monitorLoop          = nullptr;
	pw_thread_loop *m_monitorThread = nullptr;
	pw_context *m_monitorContext    = nullptr;
	pw_core *m_monitorCore          = nullptr;
	pw_registry *m_registry         = nullptr;
	bool m_monitorRunning           = false;
	std::unique_ptr< spa_hook > m_registryListener;

	/// Guards m_nodes: written by the monitor thread, read by the UI thread.
	QMutex m_nodesMutex;
	QMap< uint32_t, PipeWireNode > m_nodes;

	void startMonitor();
	void stopMonitor();

	static void onRegistryGlobal(void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version,
								 const spa_dict *props);
	static void onRegistryGlobalRemove(void *data, uint32_t id);

	const char *(*pw_get_library_version)();

	void (*pw_init)(int *argc, char **argv[]);
	void (*pw_deinit)();

	pw_loop *(*pw_loop_new)(const spa_dict *props);
	void (*pw_loop_destroy)(pw_loop *loop);

	pw_thread_loop *(*pw_thread_loop_new_full)(pw_loop *loop, const char *name, const spa_dict *props);
	void (*pw_thread_loop_destroy)(pw_thread_loop *loop);
	int (*pw_thread_loop_start)(pw_thread_loop *loop);
	int (*pw_thread_loop_stop)(pw_thread_loop *loop);
	void (*pw_thread_loop_lock)(pw_thread_loop *loop);
	void (*pw_thread_loop_unlock)(pw_thread_loop *loop);

	pw_properties *(*pw_properties_new)(const char *key, ...);
	int (*pw_properties_set)(pw_properties *properties, const char *key, const char *value);

	pw_context *(*pw_context_new)(pw_loop *main_loop, pw_properties *props, size_t user_data_size);
	void (*pw_context_destroy)(pw_context *context);
	pw_core *(*pw_context_connect)(pw_context *context, pw_properties *properties, size_t user_data_size);
	int (*pw_core_disconnect)(pw_core *core);
	void (*pw_proxy_destroy)(pw_proxy *proxy);

	pw_stream *(*pw_stream_new_simple)(pw_loop *loop, const char *name, pw_properties *props,
									   const pw_stream_events *events, void *data);
	int (*pw_stream_set_active)(pw_stream *stream, bool active);
	void (*pw_stream_destroy)(pw_stream *stream);
	int (*pw_stream_connect)(pw_stream *stream, uint32_t direction, uint32_t target_id, uint32_t flags,
							 const spa_pod **params, uint32_t n_params);
	pw_buffer *(*pw_stream_dequeue_buffer)(pw_stream *stream);
	int (*pw_stream_queue_buffer)(pw_stream *stream, pw_buffer *buffer);

private:
	Q_OBJECT
	Q_DISABLE_COPY(PipeWireSystem)
};

class PipeWireInput : public AudioInput {
public:
	void run() override;

	PipeWireInput();
	~PipeWireInput() override;

protected:
	std::unique_ptr< PipeWireEngine > m_engine;

	static void processCallback(void *param);

	void onUserMutedChanged() override;

private:
	Q_OBJECT
	Q_DISABLE_COPY(PipeWireInput)
};

class PipeWireOutput : public AudioOutput {
public:
	void run() override;

	PipeWireOutput();
	~PipeWireOutput() override;

protected:
	std::unique_ptr< PipeWireEngine > m_engine;

	static void processCallback(void *param);

private:
	Q_OBJECT
	Q_DISABLE_COPY(PipeWireOutput)
};

#endif
