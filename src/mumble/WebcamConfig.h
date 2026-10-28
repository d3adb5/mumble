// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WEBCAMCONFIG_H_
#define MUMBLE_MUMBLE_WEBCAMCONFIG_H_

#include "ConfigDialog.h"
#include "ui_WebcamConfig.h"

#include <QtCore/QPointer>

#include <memory>

class WebcamCameraPreview;
class WebcamWindow;

/// The settings page for webcam sharing: who may watch, the camera and what it is sent at, and
/// the relay it goes through.
class WebcamConfig : public ConfigWidget, Ui::WebcamConfig {
private:
	Q_OBJECT
	Q_DISABLE_COPY(WebcamConfig)
public:
	/// The unique name of this ConfigWidget
	static const QString name;
	WebcamConfig(Settings &st);
	~WebcamConfig() override;
	QString title() const Q_DECL_OVERRIDE;
	const QString &getName() const Q_DECL_OVERRIDE;
	QIcon icon() const Q_DECL_OVERRIDE;
public slots:
	void save() const Q_DECL_OVERRIDE;
	void load(const Settings &r) Q_DECL_OVERRIDE;

	void on_qcbQuality_currentIndexChanged(int index);
	void on_qpbRefreshDevices_clicked();
	void on_qpbCaFile_clicked();
	void on_qpbPreview_clicked();

protected:
	void hideEvent(QHideEvent *event) Q_DECL_OVERRIDE;

private:
	void fillDevices(const QString &selected);
	/// Closes the preview and lets the camera go.
	void stopPreview();

	std::unique_ptr< WebcamCameraPreview > m_preview;
	QPointer< WebcamWindow > m_previewWindow;
};

#endif
