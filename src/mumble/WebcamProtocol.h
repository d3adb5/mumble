// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WEBCAMPROTOCOL_H_
#define MUMBLE_MUMBLE_WEBCAMPROTOCOL_H_

#include "WebcamCrypto.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The signalling of webcam sharing (Mumla's docs/design/webcam-share.md, protocol v1): compact
// JSON carried in PluginDataTransmission messages under "webcam/<type>" data ids, addressed to
// single sessions. Every secret (stream URLs, read credential, content key) travels only inside
// a box sealed to the viewer's X25519 key, so Murmur relays the messages without learning them.
// Kept free of Qt so it can be unit tested on its own.
namespace Mumble {
namespace Webcam {

	constexpr int PROTOCOL_VERSION = 1;

	/// Murmur drops plugin data above this size.
	constexpr std::size_t MAX_MESSAGE_SIZE = 1000;

	constexpr const char *DATA_ID_PREFIX   = "webcam/";
	constexpr const char *ID_SHARE_STATE   = "webcam/share-state";
	constexpr const char *ID_WATCH_REQUEST = "webcam/watch-request";
	constexpr const char *ID_WATCH_GRANT   = "webcam/watch-grant";
	constexpr const char *ID_WATCH_DENY    = "webcam/watch-deny";
	constexpr const char *ID_WATCH_REKEY   = "webcam/watch-rekey";
	constexpr const char *ID_WATCH_REVOKE  = "webcam/watch-revoke";
	constexpr const char *ID_WATCH_LEAVE   = "webcam/watch-leave";

	constexpr const char *CODEC_H264       = "h264";
	constexpr const char *ENC_NAL_V1       = "aes256gcm-nal-v1";
	constexpr const char *DENY_DECLINED    = "declined";
	constexpr const char *DENY_NOT_SHARING = "not-sharing";
	constexpr const char *DENY_POLICY      = "policy";
	constexpr const char *DENY_UNSUPPORTED = "unsupported";

	// -- base64 --------------------------------------------------------------------------------

	inline std::string toBase64(const Bytes &data) {
		static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		out.reserve((data.size() + 2) / 3 * 4);
		std::size_t i = 0;
		for (; i + 2 < data.size(); i += 3) {
			const std::uint32_t n = (std::uint32_t(data[i]) << 16) | (std::uint32_t(data[i + 1]) << 8) | data[i + 2];
			out.push_back(table[(n >> 18) & 63]);
			out.push_back(table[(n >> 12) & 63]);
			out.push_back(table[(n >> 6) & 63]);
			out.push_back(table[n & 63]);
		}
		if (i + 1 == data.size()) {
			const std::uint32_t n = std::uint32_t(data[i]) << 16;
			out.push_back(table[(n >> 18) & 63]);
			out.push_back(table[(n >> 12) & 63]);
			out.append("==");
		} else if (i + 2 == data.size()) {
			const std::uint32_t n = (std::uint32_t(data[i]) << 16) | (std::uint32_t(data[i + 1]) << 8);
			out.push_back(table[(n >> 18) & 63]);
			out.push_back(table[(n >> 12) & 63]);
			out.push_back(table[(n >> 6) & 63]);
			out.push_back('=');
		}
		return out;
	}

	/// Standard base64 with padding. Nothing on any malformed input.
	inline std::optional< Bytes > fromBase64(const std::string &text) {
		if (text.size() % 4 != 0) {
			return std::nullopt;
		}
		auto value = [](char c) -> int {
			if (c >= 'A' && c <= 'Z') {
				return c - 'A';
			}
			if (c >= 'a' && c <= 'z') {
				return c - 'a' + 26;
			}
			if (c >= '0' && c <= '9') {
				return c - '0' + 52;
			}
			if (c == '+') {
				return 62;
			}
			if (c == '/') {
				return 63;
			}
			return -1;
		};
		Bytes out;
		out.reserve(text.size() / 4 * 3);
		for (std::size_t i = 0; i < text.size(); i += 4) {
			const bool last = i + 4 == text.size();
			const int pad   = last ? (text[i + 3] == '=' ? (text[i + 2] == '=' ? 2 : 1) : 0) : 0;
			std::uint32_t n = 0;
			for (std::size_t j = 0; j < 4; ++j) {
				int v = 0;
				if (j >= 4 - static_cast< std::size_t >(pad)) {
					if (text[i + j] != '=') {
						return std::nullopt;
					}
				} else {
					v = value(text[i + j]);
					if (v < 0) {
						return std::nullopt;
					}
				}
				n = (n << 6) | static_cast< std::uint32_t >(v);
			}
			out.push_back(static_cast< std::uint8_t >(n >> 16));
			if (pad < 2) {
				out.push_back(static_cast< std::uint8_t >(n >> 8));
			}
			if (pad < 1) {
				out.push_back(static_cast< std::uint8_t >(n));
			}
		}
		return out;
	}

	/// A watch-request nonce: 16 lowercase hex characters. Empty if the CSPRNG fails.
	inline std::string randomNonce() {
		const Bytes bits = randomBytes(8);
		return bits.empty() ? std::string() : toHex(bits);
	}

	inline bool isNonce(const std::string &text) {
		if (text.size() != 16) {
			return false;
		}
		for (char c : text) {
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
				return false;
			}
		}
		return true;
	}

	// -- messages ------------------------------------------------------------------------------

	namespace detail {
		/// The object in \p data if it parses and speaks protocol v1. A higher version is not
		/// understood and is ignored, as is anything that is not a JSON object.
		inline std::optional< nlohmann::json > parseMessage(const std::string &data) {
			if (data.size() > MAX_MESSAGE_SIZE) {
				return std::nullopt;
			}
			nlohmann::json json = nlohmann::json::parse(data, nullptr, false);
			if (json.is_discarded() || !json.is_object()) {
				return std::nullopt;
			}
			const auto v = json.find("v");
			if (v == json.end() || !v->is_number_integer() || v->get< int >() != PROTOCOL_VERSION) {
				return std::nullopt;
			}
			return json;
		}

		inline std::optional< std::string > stringField(const nlohmann::json &json, const char *name) {
			const auto it = json.find(name);
			if (it == json.end() || !it->is_string()) {
				return std::nullopt;
			}
			return it->get< std::string >();
		}

		inline std::optional< Bytes > base64Field(const nlohmann::json &json, const char *name, std::size_t size = 0) {
			std::optional< std::string > text = stringField(json, name);
			if (!text) {
				return std::nullopt;
			}
			std::optional< Bytes > bytes = fromBase64(*text);
			if (!bytes || (size != 0 && bytes->size() != size)) {
				return std::nullopt;
			}
			return bytes;
		}

		inline int intField(const nlohmann::json &json, const char *name, int fallback) {
			const auto it = json.find(name);
			return (it != json.end() && it->is_number_integer()) ? it->get< int >() : fallback;
		}

		inline nlohmann::json versioned() {
			return nlohmann::json{ { "v", PROTOCOL_VERSION } };
		}
	} // namespace detail

	/// Whether a plugin data id belongs to webcam sharing.
	inline bool isWebcamDataId(const std::string &dataId) {
		return dataId.rfind(DATA_ID_PREFIX, 0) == 0;
	}

	/// webcam/share-state: public, it drives the indicator.
	struct ShareState {
		bool sharing = false;
		int width    = 0;
		int height   = 0;
		int fps      = 0;
	};

	inline std::string encodeShareState(const ShareState &state) {
		nlohmann::json json = detail::versioned();
		json["sharing"]     = state.sharing;
		if (state.sharing) {
			json["w"]   = state.width;
			json["h"]   = state.height;
			json["fps"] = state.fps;
		}
		return json.dump();
	}

	inline std::optional< ShareState > decodeShareState(const std::string &data) {
		std::optional< nlohmann::json > json = detail::parseMessage(data);
		if (!json) {
			return std::nullopt;
		}
		const auto sharing = json->find("sharing");
		if (sharing == json->end() || !sharing->is_boolean()) {
			return std::nullopt;
		}
		ShareState state;
		state.sharing = sharing->get< bool >();
		state.width   = detail::intField(*json, "w", 0);
		state.height  = detail::intField(*json, "h", 0);
		state.fps     = detail::intField(*json, "fps", 0);
		return state;
	}

	/// webcam/watch-request.
	struct WatchRequest {
		std::string nonce;
		Bytes publicKey;
	};

	inline std::string encodeWatchRequest(const WatchRequest &request) {
		nlohmann::json json = detail::versioned();
		json["nonce"]       = request.nonce;
		json["pk"]          = toBase64(request.publicKey);
		return json.dump();
	}

	/// The request, with an empty public key when it carries none (which the sender answers
	/// with DENY_UNSUPPORTED). Nothing when it is not a request at all.
	inline std::optional< WatchRequest > decodeWatchRequest(const std::string &data) {
		std::optional< nlohmann::json > json = detail::parseMessage(data);
		if (!json) {
			return std::nullopt;
		}
		std::optional< std::string > nonce = detail::stringField(*json, "nonce");
		if (!nonce || !isNonce(*nonce)) {
			return std::nullopt;
		}
		WatchRequest request;
		request.nonce = *nonce;
		if (std::optional< Bytes > pk = detail::base64Field(*json, "pk", X25519_SIZE)) {
			request.publicKey = *pk;
		}
		return request;
	}

	/// webcam/watch-deny.
	struct WatchDeny {
		std::string nonce;
		std::string reason;
	};

	inline std::string encodeWatchDeny(const WatchDeny &deny) {
		nlohmann::json json = detail::versioned();
		json["nonce"]       = deny.nonce;
		json["reason"]      = deny.reason;
		return json.dump();
	}

	inline std::optional< WatchDeny > decodeWatchDeny(const std::string &data) {
		std::optional< nlohmann::json > json = detail::parseMessage(data);
		if (!json) {
			return std::nullopt;
		}
		std::optional< std::string > nonce = detail::stringField(*json, "nonce");
		if (!nonce) {
			return std::nullopt;
		}
		return WatchDeny{ *nonce, detail::stringField(*json, "reason").value_or(std::string()) };
	}

	/// webcam/watch-revoke and webcam/watch-leave carry nothing but the version.
	inline std::string encodeEmpty() {
		return detail::versioned().dump();
	}

	inline bool decodeEmpty(const std::string &data) {
		return detail::parseMessage(data).has_value();
	}

	/// The secrets sealed into a grant or a rekey. A rekey without urls is a key-only rotation.
	struct StreamSecrets {
		std::vector< std::string > urls;
		std::string user;
		std::string pass;
		std::string codec = CODEC_H264;
		std::string enc   = ENC_NAL_V1;
		std::uint8_t kid  = 0;
		Bytes key;
	};

	namespace detail {
		inline std::string encodeSecrets(const StreamSecrets &secrets, bool grant) {
			nlohmann::json json = nlohmann::json::object();
			if (grant || !secrets.urls.empty()) {
				json["urls"] = secrets.urls;
				if (!secrets.user.empty()) {
					json["user"] = secrets.user;
					json["pass"] = secrets.pass;
				}
			}
			if (grant) {
				json["codec"] = secrets.codec;
				json["enc"]   = secrets.enc;
			}
			json["kid"] = secrets.kid;
			json["key"] = toBase64(secrets.key);
			return json.dump();
		}

		inline std::optional< StreamSecrets > decodeSecrets(const std::string &text, bool grant) {
			nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
			if (json.is_discarded() || !json.is_object()) {
				return std::nullopt;
			}
			StreamSecrets secrets;
			const auto kid = json.find("kid");
			if (kid == json.end() || !kid->is_number_integer() || kid->get< int >() < 0 || kid->get< int >() > 255) {
				return std::nullopt;
			}
			secrets.kid                = static_cast< std::uint8_t >(kid->get< int >());
			std::optional< Bytes > key = base64Field(json, "key", KEY_SIZE);
			if (!key) {
				return std::nullopt;
			}
			secrets.key = *key;

			const auto urls = json.find("urls");
			if (urls != json.end()) {
				if (!urls->is_array()) {
					return std::nullopt;
				}
				for (const nlohmann::json &url : *urls) {
					if (url.is_string()) {
						secrets.urls.push_back(url.get< std::string >());
					}
				}
			}
			if (grant && secrets.urls.empty()) {
				return std::nullopt;
			}
			secrets.user = stringField(json, "user").value_or(std::string());
			secrets.pass = stringField(json, "pass").value_or(std::string());
			if (grant) {
				secrets.codec = stringField(json, "codec").value_or(std::string());
				secrets.enc   = stringField(json, "enc").value_or(std::string());
			}
			return secrets;
		}

		inline std::optional< std::string > sealedMessage(const char *dataId, const std::string *nonce,
														  const Bytes &viewerPublic, const StreamSecrets &secrets,
														  const std::optional< KeyPair > &ephemeral, const Bytes &iv) {
			const std::string aad = std::string(dataId) + (nonce ? *nonce : std::string());
			std::optional< WrappedBox > wrapped =
				sealToViewer(viewerPublic, encodeSecrets(secrets, nonce != nullptr), aad, ephemeral, iv);
			if (!wrapped) {
				return std::nullopt;
			}
			nlohmann::json json = versioned();
			if (nonce) {
				json["nonce"] = *nonce;
			}
			json["epk"]     = toBase64(wrapped->epk);
			json["box"]     = toBase64(wrapped->box);
			std::string out = json.dump();
			if (out.size() > MAX_MESSAGE_SIZE) {
				return std::nullopt;
			}
			return out;
		}

		inline std::optional< StreamSecrets > openedMessage(const char *dataId, const std::string *nonce,
															const KeyPair &viewer, const std::string &data) {
			std::optional< nlohmann::json > json = parseMessage(data);
			if (!json) {
				return std::nullopt;
			}
			if (nonce && stringField(*json, "nonce") != *nonce) {
				return std::nullopt;
			}
			std::optional< Bytes > epk = base64Field(*json, "epk", X25519_SIZE);
			std::optional< Bytes > box = base64Field(*json, "box");
			if (!epk || !box) {
				return std::nullopt;
			}
			const std::string aad              = std::string(dataId) + (nonce ? *nonce : std::string());
			std::optional< std::string > plain = openFromSender(viewer, WrappedBox{ *epk, *box }, aad);
			if (!plain) {
				return std::nullopt;
			}
			return decodeSecrets(*plain, nonce != nullptr);
		}
	} // namespace detail

	/// webcam/watch-grant for the request with \p nonce, sealed to the viewer's key. Nothing if
	/// sealing fails or the message would exceed MAX_MESSAGE_SIZE. \p ephemeral and \p iv are for
	/// test vectors only.
	inline std::optional< std::string > encodeWatchGrant(const std::string &nonce, const Bytes &viewerPublic,
														 const StreamSecrets &secrets,
														 const std::optional< KeyPair > &ephemeral = std::nullopt,
														 const Bytes &iv                           = {}) {
		return detail::sealedMessage(ID_WATCH_GRANT, &nonce, viewerPublic, secrets, ephemeral, iv);
	}

	/// Opens a grant answering our request with \p nonce. Nothing if the nonce differs, the box
	/// fails to open or the secrets are malformed.
	inline std::optional< StreamSecrets > decodeWatchGrant(const std::string &data, const std::string &nonce,
														   const KeyPair &viewer) {
		return detail::openedMessage(ID_WATCH_GRANT, &nonce, viewer, data);
	}

	/// webcam/watch-rekey, sealed to the viewer's key: urls set means "reconnect there".
	inline std::optional< std::string > encodeWatchRekey(const Bytes &viewerPublic, const StreamSecrets &secrets) {
		return detail::sealedMessage(ID_WATCH_REKEY, nullptr, viewerPublic, secrets, std::nullopt, {});
	}

	inline std::optional< StreamSecrets > decodeWatchRekey(const std::string &data, const KeyPair &viewer) {
		return detail::openedMessage(ID_WATCH_REKEY, nullptr, viewer, data);
	}

	/// The first URL a viewer can use: v1 viewers read RTSPS only.
	inline std::optional< std::string > pickUrl(const std::vector< std::string > &urls) {
		for (const std::string &url : urls) {
			if (url.rfind("rtsps://", 0) == 0) {
				return url;
			}
		}
		return std::nullopt;
	}

	/// A relay login: a user and its password.
	struct Login {
		std::string user;
		std::string pass;

		bool operator==(const Login &other) const { return user == other.user && pass == other.pass; }
	};

	/// The login a viewer reads the stream with. The relay wants one for every read, so the
	/// viewer brings its own; a login the sender put in the grant, if any, still wins. Nothing
	/// if neither has a user.
	inline std::optional< Login > readLogin(const StreamSecrets &secrets, const Login &own) {
		if (!secrets.user.empty()) {
			return Login{ secrets.user, secrets.pass };
		}
		if (!own.user.empty()) {
			return own;
		}
		return std::nullopt;
	}

} // namespace Webcam
} // namespace Mumble

#endif
