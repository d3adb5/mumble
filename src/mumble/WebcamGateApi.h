// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WEBCAMGATEAPI_H_
#define MUMBLE_MUMBLE_WEBCAMGATEAPI_H_

#include "WebcamProtocol.h"

#include <optional>
#include <string>

// The relay's gate (protocol v1.1, ~/projects/mtx-gate/DESIGN.md): an HTTPS API in front of
// MediaMTX where an owner claims its share, mints a ticket per viewer, and a viewer redeems its
// ticket for its own IP. Kept free of Qt so the rules can be unit tested.
namespace Mumble {
namespace Webcam {

	/// How often a sender renews its share claim; the gate forgets it after 90 s.
	constexpr int GATE_SHARE_RENEW_MS = 30000;
	/// How long a viewer keeps retrying a ticket the gate refuses for now (gateRetries): long
	/// enough for the sender's next renewal to bring a new one.
	constexpr int GATE_RETRY_WINDOW_MS = 45000;

	/// Where the gate API is: \p override when set, else https://<host of \p relay>/gate/v1. The
	/// owner login goes there, so it only ever comes from the user's own settings, never from a
	/// grant, and only over HTTPS. Nothing if neither gives an HTTPS address.
	inline std::optional< std::string > gateBase(const std::string &relay, const std::string &override) {
		const std::size_t start = override.find_first_not_of(" \t");
		if (start != std::string::npos) {
			std::string base = override.substr(start, override.find_last_not_of(" \t") - start + 1);
			if (base.rfind("https://", 0) != 0 || base.size() <= 8) {
				return std::nullopt;
			}
			while (base.back() == '/') {
				base.pop_back();
			}
			return base;
		}
		const auto hostPort = relayHostPort(relay);
		if (!hostPort) {
			return std::nullopt;
		}
		return "https://" + hostPort->first + "/gate/v1";
	}

	/// What a gate answer means to a client.
	enum class GateAnswer {
		/// 200 with a JSON body, or 204.
		Ok,
		/// 201 on a share claim: the gate had forgotten the share, so tickets must be minted anew.
		Created,
		/// 401: the owner login is wrong.
		BadLogin,
		/// 403: the path or ticket belongs to another login.
		Forbidden,
		/// 404: no such share or ticket (the gate restarted, the share ended or it was revoked).
		Unknown,
		/// 410: the first-redeem window is over; the viewer asks to watch again.
		Expired,
		/// 429: a limit was reached.
		Limited,
		/// No gate answered: the relay has none (a v1 relay, as production is today), or it could
		/// not be reached. Clients then behave as in v1, without tickets.
		NoGate,
	};

	/// Classifies an HTTP answer. Only refusals the gate makes itself (\p fromGate, its
	/// X-Mtx-Gate header) count as such, and its 200 and 201 always carry a JSON object
	/// (\p jsonBody): a web server in front of no gate saying 404, or a page answering 200, means
	/// no gate.
	inline GateAnswer gateAnswer(int status, bool fromGate, bool jsonBody) {
		if (status == 204) {
			return GateAnswer::Ok;
		}
		if (status == 200 || status == 201) {
			if (!jsonBody) {
				return GateAnswer::NoGate;
			}
			return status == 200 ? GateAnswer::Ok : GateAnswer::Created;
		}
		if (!fromGate) {
			return GateAnswer::NoGate;
		}
		switch (status) {
			case 401:
				return GateAnswer::BadLogin;
			case 403:
				return GateAnswer::Forbidden;
			case 404:
				return GateAnswer::Unknown;
			case 410:
				return GateAnswer::Expired;
			case 429:
				return GateAnswer::Limited;
			default:
				return GateAnswer::NoGate;
		}
	}

	/// Whether a viewer retries a redeem the gate refused, within GATE_RETRY_WINDOW_MS: the
	/// ticket may be for a path the sender has yet to claim (403/404, after a move or a gate
	/// restart), or the gate is busy (429).
	inline bool gateRetries(GateAnswer answer) {
		return answer == GateAnswer::Forbidden || answer == GateAnswer::Unknown || answer == GateAnswer::Limited;
	}

} // namespace Webcam
} // namespace Mumble

#endif
