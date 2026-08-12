#include "digital_signature.hpp"

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/crypto.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace
{
using EcdsaSignature = std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)>;
using EvpMessageDigest = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
using EvpPublicKey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

std::vector<unsigned char> decode_base64(const std::string& encoded)
{
    if(encoded.empty()) return {};

    BIO* source = BIO_new_mem_buf(encoded.data(),
                                  static_cast<int>(encoded.size()));
    BIO* decoder = BIO_new(BIO_f_base64());
    if(!source || !decoder)
    {
        if(decoder) BIO_free(decoder);
        if(source) BIO_free(source);
        return {};
    }

    BIO_set_flags(decoder, BIO_FLAGS_BASE64_NO_NL);
    source = BIO_push(decoder, source);

    std::vector<unsigned char> decoded(encoded.size());
    const int length = BIO_read(source, decoded.data(),
                                static_cast<int>(decoded.size()));
    BIO_free_all(source);
    if(length <= 0) return {};

    decoded.resize(static_cast<std::size_t>(length));
    return decoded;
}

std::vector<unsigned char> web_crypto_signature_to_der(
    const std::vector<unsigned char>& raw_signature)
{
    if(raw_signature.size() != 64) return {};

    EcdsaSignature signature(ECDSA_SIG_new(), ECDSA_SIG_free);
    if(!signature) return {};

    BIGNUM* r = BN_bin2bn(raw_signature.data(), 32, nullptr);
    BIGNUM* s = BN_bin2bn(raw_signature.data() + 32, 32, nullptr);
    if(!r || !s || ECDSA_SIG_set0(signature.get(), r, s) != 1)
    {
        BN_free(r);
        BN_free(s);
        return {};
    }

    const int encoded_length = i2d_ECDSA_SIG(signature.get(), nullptr);
    if(encoded_length <= 0) return {};

    std::vector<unsigned char> der(static_cast<std::size_t>(encoded_length));
    unsigned char* output = der.data();
    if(i2d_ECDSA_SIG(signature.get(), &output) != encoded_length)
        return {};
    return der;
}
}

bool verify_ecdsa_p256_signature(const std::string& public_key_base64,
                                const std::string& signature_base64,
                                const std::string& message)
{
    const std::vector<unsigned char> public_key =
        decode_base64(public_key_base64);
    const std::vector<unsigned char> web_signature =
        decode_base64(signature_base64);
    if(public_key.empty() || web_signature.empty()) return false;

    const unsigned char* public_key_data = public_key.data();
    EvpPublicKey key(d2i_PUBKEY(nullptr, &public_key_data,
                                static_cast<long>(public_key.size())),
                     EVP_PKEY_free);
    if(!key || EVP_PKEY_base_id(key.get()) != EVP_PKEY_EC) return false;

    const std::vector<unsigned char> der_signature =
        web_crypto_signature_to_der(web_signature);
    if(der_signature.empty()) return false;

    EvpMessageDigest digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if(!digest) return false;

    if(EVP_DigestVerifyInit(digest.get(), nullptr, EVP_sha256(), nullptr,
                            key.get()) != 1)
        return false;
    if(EVP_DigestVerifyUpdate(digest.get(), message.data(), message.size()) != 1)
        return false;

    return EVP_DigestVerifyFinal(digest.get(), der_signature.data(),
                                 der_signature.size()) == 1;
}
