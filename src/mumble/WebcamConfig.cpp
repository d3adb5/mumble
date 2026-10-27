// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "WebcamConfig.h"

#include "WebcamMode.h"
#include "WebcamStream.h"

#include <QtWidgets/QFileDialog>

const QString WebcamConfig::name = QLatin1String("WebcamConfig");

static ConfigWidget *WebcamConfigNew(Settings &st) {
	return new WebcamConfig(st);
}

static ConfigRegistrar registrarWebcamConfig(1350, WebcamConfigNew);

namespace {
/// The index of the custom quality in the quality box, after the presets.
int customQualityIndex() {
	return static_cast< int >(Mumble::Webcam::qualityPresets().size());
}
} // namespace

WebcamConfig::WebcamConfig(Settings &st) : ConfigWidget(st) {
	setupUi(this);

	const QStringList names                               = { tr("Data saver"), tr("Low"), tr("Medium"), tr("High") };
	const std::vector< Mumble::Webcam::Quality > &presets = Mumble::Webcam::qualityPresets();
	for (std::size_t i = 0; i < presets.size(); ++i) {
		const Mumble::Webcam::Quality &q = presets[i];
		qcbQuality->addItem(
			tr("%1 (%2p, %3 fps, %4 kbit/s)")
				.arg(i < static_cast< std::size_t >(names.size()) ? names[static_cast< int >(i)] : QString::number(i))
				.arg(q.height)
				.arg(q.fps)
				.arg(q.bitrateKbps));
	}
	qcbQuality->addItem(tr("Custom"));

	qcbEncoder->addItem(tr("Automatic"), QString());
	for (const QString &encoder : WebcamStream::encoderFactories()) {
		qcbEncoder->addItem(encoder, encoder);
	}
}

QString WebcamConfig::title() const {
	return tr("Camera");
}

const QString &WebcamConfig::getName() const {
	return WebcamConfig::name;
}

QIcon WebcamConfig::icon() const {
	return QIcon(QLatin1String("skin:actions/camera-web.svg"));
}

void WebcamConfig::fillDevices(const QString &selected) {
	qcbDevice->clear();
	qcbDevice->addItem(tr("The first camera"), QString());
	for (const WebcamDevice &camera : WebcamStream::listCameras()) {
		qcbDevice->addItem(camera.name, camera.id);
	}
	int index = qcbDevice->findData(selected);
	if (index < 0 && !selected.isEmpty()) {
		// A camera not plugged in right now stays selected.
		qcbDevice->addItem(tr("%1 (not connected)").arg(selected), selected);
		index = qcbDevice->count() - 1;
	}
	qcbDevice->setCurrentIndex(std::max(0, index));
}

void WebcamConfig::load(const Settings &r) {
	loadComboBox(qcbPolicy, r.iWebcamPolicy);
	fillDevices(r.qsWebcamDevice);
	loadComboBox(qcbQuality, std::max(0, std::min(r.iWebcamQuality, customQualityIndex())));
	qsbCustomHeight->setValue(r.iWebcamCustomHeight);
	qsbCustomFps->setValue(r.iWebcamCustomFps);
	qsbCustomKbps->setValue(r.iWebcamCustomKbps);
	on_qcbQuality_currentIndexChanged(qcbQuality->currentIndex());

	int encoder = qcbEncoder->findData(r.qsWebcamEncoder);
	if (encoder < 0) {
		qcbEncoder->addItem(tr("%1 (not installed)").arg(r.qsWebcamEncoder), r.qsWebcamEncoder);
		encoder = qcbEncoder->count() - 1;
	}
	qcbEncoder->setCurrentIndex(encoder);
	loadCheckBox(qcbOnDemand, r.bWebcamOnDemand);

	qleRelay->setText(r.qsWebcamRelay);
	qlePublishUser->setText(r.qsWebcamPublishUser);
	qlePublishPassword->setText(r.qsWebcamPublishPassword);
	qleReadUser->setText(r.qsWebcamReadUser);
	qleReadPassword->setText(r.qsWebcamReadPassword);
	qleCaFile->setText(r.qsWebcamCaFile);
	qleGateUrl->setText(r.qsWebcamGateUrl);
	qsbLatency->setValue(r.iWebcamViewLatency);
}

void WebcamConfig::save() const {
	s.iWebcamPolicy           = qcbPolicy->currentIndex();
	s.qsWebcamDevice          = qcbDevice->currentData().toString();
	s.iWebcamQuality          = qcbQuality->currentIndex();
	s.iWebcamCustomHeight     = qsbCustomHeight->value();
	s.iWebcamCustomFps        = qsbCustomFps->value();
	s.iWebcamCustomKbps       = qsbCustomKbps->value();
	s.qsWebcamEncoder         = qcbEncoder->currentData().toString();
	s.bWebcamOnDemand         = qcbOnDemand->isChecked();
	s.qsWebcamRelay           = qleRelay->text().trimmed();
	s.qsWebcamPublishUser     = qlePublishUser->text();
	s.qsWebcamPublishPassword = qlePublishPassword->text();
	s.qsWebcamReadUser        = qleReadUser->text();
	s.qsWebcamReadPassword    = qleReadPassword->text();
	s.qsWebcamCaFile          = qleCaFile->text().trimmed();
	s.qsWebcamGateUrl         = qleGateUrl->text().trimmed();
	s.iWebcamViewLatency      = qsbLatency->value();
}

void WebcamConfig::on_qcbQuality_currentIndexChanged(int index) {
	const bool custom = index == customQualityIndex();
	for (QWidget *widget : std::initializer_list< QWidget * >{ qlCustomHeight, qsbCustomHeight, qlCustomFps,
															   qsbCustomFps, qlCustomKbps, qsbCustomKbps }) {
		widget->setEnabled(custom);
	}
}

void WebcamConfig::on_qpbRefreshDevices_clicked() {
	fillDevices(qcbDevice->currentData().toString());
}

void WebcamConfig::on_qpbCaFile_clicked() {
	const QString file = QFileDialog::getOpenFileName(this, tr("Trusted CA for relays"), qleCaFile->text(),
													  tr("Certificates (*.pem *.crt);;All files (*)"));
	if (!file.isEmpty()) {
		qleCaFile->setText(file);
	}
}
