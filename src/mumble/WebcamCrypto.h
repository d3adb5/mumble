// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_WEBCAMCRYPTO_H_
#define MUMBLE_MUMBLE_WEBCAMCRYPTO_H_

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The cryptography behind webcam sharing (Mumla's docs/design/webcam-share.md, protocol v1):
// the per-NAL "aes256gcm-nal-v1" payload encryption that keeps the picture from the MediaMTX
// relay, and the X25519 key wrap that keeps the stream's secrets from Murmur. Kept free of Qt
// and of GStreamer so both can be unit tested on their own.
namespace Mumble {
namespace Webcam {

	using Bytes = std::vector< std::uint8_t >;

	constexpr std::size_t KEY_SIZE    = 32;
	constexpr std::size_t IV_SIZE     = 12;
	constexpr std::size_t TAG_SIZE    = 16;
	constexpr std::size_t X25519_SIZE = 32;

	/// Ends every encrypted NAL body, like H.264's rbsp_stop_one_bit: the body then never ends
	/// in 0x00 (which Annex-B would strip), and the end is unambiguous after un-escaping.
	constexpr std::uint8_t STOP_BYTE = 0x80;

	/// Bytes from OpenSSL's CSPRNG. Empty if the generator fails, which callers treat as fatal.
	inline Bytes randomBytes(std::size_t count) {
		Bytes out(count);
		if (count > 0 && RAND_bytes(out.data(), static_cast< int >(count)) != 1) {
			return {};
		}
		return out;
	}

	/// Lowercase hex of \p data.
	inline std::string toHex(const Bytes &data) {
		static const char digits[] = "0123456789abcdef";
		std::string out;
		out.reserve(data.size() * 2);
		for (std::uint8_t byte : data) {
			out.push_back(digits[byte >> 4]);
			out.push_back(digits[byte & 0x0F]);
		}
		return out;
	}

	/// A fresh stream path, "mcam/" + 128 random bits in hex. Empty if the CSPRNG fails.
	inline std::string randomStreamPath() {
		const Bytes bits = randomBytes(16);
		return bits.empty() ? std::string() : "mcam/" + toHex(bits);
	}

	// -- emulation prevention ------------------------------------------------------------------

	/// Escapes \p body so it holds no start code: an 0x03 goes after every 00 00 that is
	/// followed by a byte <= 0x03. Callers end the body in a non-zero byte (the 0x80 stop byte),
	/// so the escaped form never ends in 0x00 either.
	inline Bytes escapeEmulation(const Bytes &body) {
		Bytes out;
		out.reserve(body.size() + body.size() / 64 + 1);
		int zeros = 0;
		for (std::uint8_t byte : body) {
			if (zeros >= 2 && byte <= 0x03) {
				out.push_back(0x03);
				zeros = 0;
			}
			out.push_back(byte);
			zeros = (byte == 0x00) ? zeros + 1 : 0;
		}
		return out;
	}

	/// Reverses escapeEmulation(): drops each 0x03 that follows 00 00.
	inline Bytes unescapeEmulation(const std::uint8_t *data, std::size_t size) {
		Bytes out;
		out.reserve(size);
		int zeros = 0;
		for (std::size_t i = 0; i < size; ++i) {
			const std::uint8_t byte = data[i];
			if (zeros >= 2 && byte == 0x03) {
				zeros = 0;
				continue;
			}
			out.push_back(byte);
			zeros = (byte == 0x00) ? zeros + 1 : 0;
		}
		return out;
	}

	// -- AES-256-GCM ---------------------------------------------------------------------------

	namespace detail {
		struct CipherCtxDeleter {
			void operator()(EVP_CIPHER_CTX *ctx) const { EVP_CIPHER_CTX_free(ctx); }
		};
		using CipherCtx = std::unique_ptr< EVP_CIPHER_CTX, CipherCtxDeleter >;

		struct PkeyDeleter {
			void operator()(EVP_PKEY *key) const { EVP_PKEY_free(key); }
		};
		using Pkey = std::unique_ptr< EVP_PKEY, PkeyDeleter >;

		struct PkeyCtxDeleter {
			void operator()(EVP_PKEY_CTX *ctx) const { EVP_PKEY_CTX_free(ctx); }
		};
		using PkeyCtx = std::unique_ptr< EVP_PKEY_CTX, PkeyCtxDeleter >;
	} // namespace detail

	/// iv || ciphertext || tag of \p plaintext under \p key, authenticating \p aad as well.
	/// Empty on failure (a key of the wrong size included).
	inline Bytes sealGcm(const Bytes &key, const Bytes &iv, const std::uint8_t *plaintext, std::size_t size,
						 const Bytes &aad) {
		if (key.size() != KEY_SIZE || iv.size() != IV_SIZE) {
			return {};
		}
		detail::CipherCtx ctx(EVP_CIPHER_CTX_new());
		if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key.data(), iv.data()) != 1) {
			return {};
		}

		Bytes out(IV_SIZE + size + TAG_SIZE);
		std::copy(iv.begin(), iv.end(), out.begin());

		int len = 0;
		if (!aad.empty()
			&& EVP_EncryptUpdate(ctx.get(), nullptr, &len, aad.data(), static_cast< int >(aad.size())) != 1) {
			return {};
		}
		if (size > 0
			&& EVP_EncryptUpdate(ctx.get(), out.data() + IV_SIZE, &len, plaintext, static_cast< int >(size)) != 1) {
			return {};
		}
		if (EVP_EncryptFinal_ex(ctx.get(), out.data() + IV_SIZE + size, &len) != 1
			|| EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, TAG_SIZE, out.data() + IV_SIZE + size) != 1) {
			return {};
		}
		return out;
	}

	/// Opens what sealGcm() produced. Nothing if the tag does not match.
	inline std::optional< Bytes > openGcm(const Bytes &key, const std::uint8_t *sealed, std::size_t size,
										  const Bytes &aad) {
		if (key.size() != KEY_SIZE || size < IV_SIZE + TAG_SIZE) {
			return std::nullopt;
		}
		const std::size_t textSize = size - IV_SIZE - TAG_SIZE;
		detail::CipherCtx ctx(EVP_CIPHER_CTX_new());
		if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key.data(), sealed) != 1) {
			return std::nullopt;
		}

		Bytes out(textSize);
		int len = 0;
		if (!aad.empty()
			&& EVP_DecryptUpdate(ctx.get(), nullptr, &len, aad.data(), static_cast< int >(aad.size())) != 1) {
			return std::nullopt;
		}
		if (textSize > 0
			&& EVP_DecryptUpdate(ctx.get(), out.data(), &len, sealed + IV_SIZE, static_cast< int >(textSize)) != 1) {
			return std::nullopt;
		}
		// OpenSSL takes a non-const pointer for the expected tag but only reads it.
		Bytes tag(sealed + IV_SIZE + textSize, sealed + size);
		if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, TAG_SIZE, tag.data()) != 1
			|| EVP_DecryptFinal_ex(ctx.get(), out.data() + textSize, &len) != 1) {
			return std::nullopt;
		}
		return out;
	}

	// -- per-NAL payload encryption ------------------------------------------------------------

	/// What a sender does with a NAL unit of the given type (the low five bits of its header).
	enum class NalAction { Encrypt, Clear, Drop };

	inline NalAction nalAction(std::uint8_t header) {
		const int type = header & 0x1F;
		if (type >= 1 && type <= 5) {
			return NalAction::Encrypt;
		}
		if (type == 7 || type == 8 || type == 9) {
			return NalAction::Clear;
		}
		// SEI (6) carries the encoder's settings, and nothing else belongs in the stream.
		return NalAction::Drop;
	}

	/// hdr || EPB(kid || iv || ciphertext || tag || 0x80) for one NAL (header byte first), with \p iv
	/// given so tests can be deterministic. Empty on failure.
	inline Bytes encryptNalWithIv(const std::uint8_t *nal, std::size_t size, const Bytes &key, std::uint8_t kid,
								  const Bytes &iv) {
		if (size < 1) {
			return {};
		}
		const Bytes aad    = { nal[0], kid };
		const Bytes sealed = sealGcm(key, iv, nal + 1, size - 1, aad);
		if (sealed.empty()) {
			return {};
		}
		Bytes body;
		body.reserve(1 + sealed.size());
		body.push_back(kid);
		body.insert(body.end(), sealed.begin(), sealed.end());
		body.push_back(STOP_BYTE);

		Bytes out           = { nal[0] };
		const Bytes escaped = escapeEmulation(body);
		out.insert(out.end(), escaped.begin(), escaped.end());
		return out;
	}

	/// encryptNalWithIv() under a fresh random IV.
	inline Bytes encryptNal(const std::uint8_t *nal, std::size_t size, const Bytes &key, std::uint8_t kid) {
		const Bytes iv = randomBytes(IV_SIZE);
		return iv.empty() ? Bytes() : encryptNalWithIv(nal, size, key, kid, iv);
	}

	/// The keys a viewer holds, by kid: the current and the previous one, so a key-only rotation
	/// does not drop the frames still in flight under the old key.
	class KeyRing {
	public:
		void set(std::uint8_t kid, const Bytes &key) {
			if (m_current && m_current->first != kid) {
				m_previous = m_current;
			}
			m_current = std::make_pair(kid, key);
		}

		void clear() {
			m_current.reset();
			m_previous.reset();
		}

		const Bytes *find(std::uint8_t kid) const {
			if (m_current && m_current->first == kid) {
				return &m_current->second;
			}
			if (m_previous && m_previous->first == kid) {
				return &m_previous->second;
			}
			return nullptr;
		}

	private:
		std::optional< std::pair< std::uint8_t, Bytes > > m_current;
		std::optional< std::pair< std::uint8_t, Bytes > > m_previous;
	};

	/// The original NAL (header byte first) from an encrypted one, or nothing when its kid is
	/// not held or its tag fails. Ciphertext never reaches the decoder.
	inline std::optional< Bytes > decryptNal(const std::uint8_t *nal, std::size_t size, const KeyRing &keys) {
		if (size < 2) {
			return std::nullopt;
		}
		Bytes body = unescapeEmulation(nal + 1, size - 1);
		if (body.size() < 1 + IV_SIZE + TAG_SIZE + 1 || body.back() != STOP_BYTE) {
			return std::nullopt;
		}
		body.pop_back();
		const Bytes *key = keys.find(body[0]);
		if (!key) {
			return std::nullopt;
		}
		const Bytes aad              = { nal[0], body[0] };
		std::optional< Bytes > plain = openGcm(*key, body.data() + 1, body.size() - 1, aad);
		if (!plain) {
			return std::nullopt;
		}
		plain->insert(plain->begin(), nal[0]);
		return plain;
	}

	// -- access units in AVC (4-byte length-prefixed) form -------------------------------------

	namespace detail {
		inline void appendLengthPrefixed(Bytes &out, const Bytes &nal) {
			const auto size = static_cast< std::uint32_t >(nal.size());
			out.push_back(static_cast< std::uint8_t >(size >> 24));
			out.push_back(static_cast< std::uint8_t >(size >> 16));
			out.push_back(static_cast< std::uint8_t >(size >> 8));
			out.push_back(static_cast< std::uint8_t >(size));
			out.insert(out.end(), nal.begin(), nal.end());
		}

		/// Calls \p fn(pointer, size) for each NAL of a 4-byte length-prefixed access unit.
		/// False if the lengths run past the end.
		template< typename Fn > inline bool forEachNal(const std::uint8_t *data, std::size_t size, Fn &&fn) {
			std::size_t pos = 0;
			while (pos < size) {
				if (size - pos < 4) {
					return false;
				}
				const std::size_t len =
					(static_cast< std::size_t >(data[pos]) << 24) | (static_cast< std::size_t >(data[pos + 1]) << 16)
					| (static_cast< std::size_t >(data[pos + 2]) << 8) | static_cast< std::size_t >(data[pos + 3]);
				pos += 4;
				if (len > size - pos) {
					return false;
				}
				if (len > 0 && !fn(data + pos, len)) {
					return false;
				}
				pos += len;
			}
			return true;
		}
	} // namespace detail

	/// The sender's side for a whole access unit: slices encrypted, SPS/PPS/AUD kept, SEI and
	/// everything else dropped. Nothing if a NAL is malformed or encryption fails.
	inline std::optional< Bytes > encryptAccessUnit(const std::uint8_t *data, std::size_t size, const Bytes &key,
													std::uint8_t kid) {
		Bytes out;
		out.reserve(size + 64);
		const bool ok = detail::forEachNal(data, size, [&](const std::uint8_t *nal, std::size_t len) {
			switch (nalAction(nal[0])) {
				case NalAction::Encrypt: {
					const Bytes sealed = encryptNal(nal, len, key, kid);
					if (sealed.empty()) {
						return false;
					}
					detail::appendLengthPrefixed(out, sealed);
					return true;
				}
				case NalAction::Clear:
					detail::appendLengthPrefixed(out, Bytes(nal, nal + len));
					return true;
				case NalAction::Drop:
					return true;
			}
			return true;
		});
		if (!ok) {
			return std::nullopt;
		}
		return out;
	}

	/// The viewer's side for a whole access unit: every slice decrypted, everything else passed
	/// on. Nothing if any slice fails, so the caller drops the unit and waits for an IDR.
	inline std::optional< Bytes > decryptAccessUnit(const std::uint8_t *data, std::size_t size, const KeyRing &keys) {
		Bytes out;
		out.reserve(size);
		const bool ok = detail::forEachNal(data, size, [&](const std::uint8_t *nal, std::size_t len) {
			if (nalAction(nal[0]) == NalAction::Encrypt) {
				std::optional< Bytes > plain = decryptNal(nal, len, keys);
				if (!plain) {
					return false;
				}
				detail::appendLengthPrefixed(out, *plain);
			} else {
				detail::appendLengthPrefixed(out, Bytes(nal, nal + len));
			}
			return true;
		});
		if (!ok) {
			return std::nullopt;
		}
		return out;
	}

	/// Whether a length-prefixed access unit holds an IDR slice.
	inline bool containsIdr(const std::uint8_t *data, std::size_t size) {
		bool idr = false;
		detail::forEachNal(data, size, [&](const std::uint8_t *nal, std::size_t) {
			idr = idr || (nal[0] & 0x1F) == 5;
			return true;
		});
		return idr;
	}

	// -- X25519 key wrap -----------------------------------------------------------------------

	/// An X25519 key pair, raw 32-byte keys.
	struct KeyPair {
		Bytes privateKey;
		Bytes publicKey;
	};

	/// A key pair from a given private key (for test vectors), or a fresh one when empty.
	inline std::optional< KeyPair > x25519KeyPair(const Bytes &privateKey = {}) {
		detail::Pkey key;
		if (privateKey.empty()) {
			detail::PkeyCtx ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr));
			EVP_PKEY *raw = nullptr;
			if (!ctx || EVP_PKEY_keygen_init(ctx.get()) != 1 || EVP_PKEY_keygen(ctx.get(), &raw) != 1) {
				return std::nullopt;
			}
			key.reset(raw);
		} else {
			if (privateKey.size() != X25519_SIZE) {
				return std::nullopt;
			}
			key.reset(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, privateKey.data(), privateKey.size()));
		}
		if (!key) {
			return std::nullopt;
		}

		KeyPair pair;
		pair.privateKey.resize(X25519_SIZE);
		pair.publicKey.resize(X25519_SIZE);
		std::size_t privLen = X25519_SIZE;
		std::size_t pubLen  = X25519_SIZE;
		if (EVP_PKEY_get_raw_private_key(key.get(), pair.privateKey.data(), &privLen) != 1
			|| EVP_PKEY_get_raw_public_key(key.get(), pair.publicKey.data(), &pubLen) != 1 || privLen != X25519_SIZE
			|| pubLen != X25519_SIZE) {
			return std::nullopt;
		}
		return pair;
	}

	/// X25519(privateKey, peerPublic). Nothing on failure or an all-zero result (a low-order
	/// peer key).
	inline std::optional< Bytes > x25519(const Bytes &privateKey, const Bytes &peerPublic) {
		if (privateKey.size() != X25519_SIZE || peerPublic.size() != X25519_SIZE) {
			return std::nullopt;
		}
		detail::Pkey own(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, privateKey.data(), privateKey.size()));
		detail::Pkey peer(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peerPublic.data(), peerPublic.size()));
		if (!own || !peer) {
			return std::nullopt;
		}
		detail::PkeyCtx ctx(EVP_PKEY_CTX_new(own.get(), nullptr));
		Bytes secret(X25519_SIZE);
		std::size_t len = secret.size();
		if (!ctx || EVP_PKEY_derive_init(ctx.get()) != 1 || EVP_PKEY_derive_set_peer(ctx.get(), peer.get()) != 1
			|| EVP_PKEY_derive(ctx.get(), secret.data(), &len) != 1 || len != X25519_SIZE) {
			return std::nullopt;
		}
		std::uint8_t any = 0;
		for (std::uint8_t byte : secret) {
			any |= byte;
		}
		if (any == 0) {
			return std::nullopt;
		}
		return secret;
	}

	/// HKDF-SHA256 (RFC 5869) of \p length bytes.
	inline Bytes hkdfSha256(const Bytes &ikm, const Bytes &salt, const std::string &info, std::size_t length) {
		detail::PkeyCtx ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr));
		Bytes out(length);
		std::size_t len = length;
		if (!ctx || EVP_PKEY_derive_init(ctx.get()) != 1 || EVP_PKEY_CTX_set_hkdf_md(ctx.get(), EVP_sha256()) != 1
			|| EVP_PKEY_CTX_set1_hkdf_salt(ctx.get(), salt.data(), static_cast< int >(salt.size())) != 1
			|| EVP_PKEY_CTX_set1_hkdf_key(ctx.get(), ikm.data(), static_cast< int >(ikm.size())) != 1
			|| EVP_PKEY_CTX_add1_hkdf_info(ctx.get(), reinterpret_cast< const unsigned char * >(info.data()),
										   static_cast< int >(info.size()))
				   != 1
			|| EVP_PKEY_derive(ctx.get(), out.data(), &len) != 1 || len != length) {
			return {};
		}
		return out;
	}

	constexpr const char *KEY_WRAP_INFO = "webcam-v1 key wrap";

	/// The wrap key between an ephemeral sender key and a viewer's public key, from either side:
	/// HKDF-SHA256(X25519, salt = epk || pk, info = KEY_WRAP_INFO).
	inline std::optional< Bytes > wrapKey(const Bytes &ownPrivate, const Bytes &peerPublic, const Bytes &epk,
										  const Bytes &pk) {
		std::optional< Bytes > secret = x25519(ownPrivate, peerPublic);
		if (!secret) {
			return std::nullopt;
		}
		Bytes salt = epk;
		salt.insert(salt.end(), pk.begin(), pk.end());
		Bytes kw = hkdfSha256(*secret, salt, KEY_WRAP_INFO, KEY_SIZE);
		if (kw.empty()) {
			return std::nullopt;
		}
		return kw;
	}

	/// A sealed box for a viewer: the sender's ephemeral public key and iv || ct || tag.
	struct WrappedBox {
		Bytes epk;
		Bytes box;
	};

	/// Seals \p plaintext to \p viewerPublic. \p ephemeral and \p iv may be given for test
	/// vectors; otherwise they are fresh.
	inline std::optional< WrappedBox > sealToViewer(const Bytes &viewerPublic, const std::string &plaintext,
													const std::string &aad,
													const std::optional< KeyPair > &ephemeral = std::nullopt,
													const Bytes &iv                           = {}) {
		std::optional< KeyPair > eph = ephemeral ? ephemeral : x25519KeyPair();
		if (!eph) {
			return std::nullopt;
		}
		std::optional< Bytes > kw = wrapKey(eph->privateKey, viewerPublic, eph->publicKey, viewerPublic);
		if (!kw) {
			return std::nullopt;
		}
		const Bytes nonce  = iv.empty() ? randomBytes(IV_SIZE) : iv;
		const Bytes sealed = sealGcm(*kw, nonce, reinterpret_cast< const std::uint8_t * >(plaintext.data()),
									 plaintext.size(), Bytes(aad.begin(), aad.end()));
		if (sealed.empty()) {
			return std::nullopt;
		}
		return WrappedBox{ eph->publicKey, sealed };
	}

	/// Opens a box sealed to \p viewer. Nothing if anything fails.
	inline std::optional< std::string > openFromSender(const KeyPair &viewer, const WrappedBox &wrapped,
													   const std::string &aad) {
		std::optional< Bytes > kw = wrapKey(viewer.privateKey, wrapped.epk, wrapped.epk, viewer.publicKey);
		if (!kw) {
			return std::nullopt;
		}
		std::optional< Bytes > plain =
			openGcm(*kw, wrapped.box.data(), wrapped.box.size(), Bytes(aad.begin(), aad.end()));
		if (!plain) {
			return std::nullopt;
		}
		return std::string(plain->begin(), plain->end());
	}

} // namespace Webcam
} // namespace Mumble

#endif
