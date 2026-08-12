#include "auth_utils.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace
{
constexpr int PASSWORD_ITERATIONS = 120000;
constexpr int PASSWORD_KEY_LENGTH = 32;

std::string bytes_to_hex(const unsigned char* bytes, std::size_t length)
{
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for(std::size_t i = 0; i < length; ++i)
        result << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    return result.str();
}
}

std::string generate_random_hex(std::size_t byte_count)
{
    std::string bytes(byte_count, '\0');
    if(byte_count > 0 &&
       RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()),
                  static_cast<int>(byte_count)) != 1)
        throw std::runtime_error("Secure random generation failed");
    return bytes_to_hex(reinterpret_cast<const unsigned char*>(bytes.data()),
                        bytes.size());
}

std::string hash_session_token(const std::string& token)
{
    unsigned char digest[SHA256_DIGEST_LENGTH]{};
    SHA256(reinterpret_cast<const unsigned char*>(token.data()),
           token.size(), digest);
    return bytes_to_hex(digest, SHA256_DIGEST_LENGTH);
}

std::string hash_password(const std::string& password,
                          const std::string& salt)
{
    unsigned char derived_key[PASSWORD_KEY_LENGTH]{};
    const int succeeded = PKCS5_PBKDF2_HMAC(
        password.c_str(), static_cast<int>(password.size()),
        reinterpret_cast<const unsigned char*>(salt.data()),
        static_cast<int>(salt.size()), PASSWORD_ITERATIONS, EVP_sha256(),
        PASSWORD_KEY_LENGTH, derived_key);
    if(succeeded != 1)
        throw std::runtime_error("Password hashing failed");
    return bytes_to_hex(derived_key, PASSWORD_KEY_LENGTH);
}

bool secure_string_equal(const std::string& left,
                         const std::string& right)
{
    unsigned char difference =
        static_cast<unsigned char>(left.size() ^ right.size());
    const std::size_t length = std::max(left.size(), right.size());
    for(std::size_t i = 0; i < length; ++i)
    {
        const unsigned char left_byte = i < left.size()
            ? static_cast<unsigned char>(left[i])
            : 0;
        const unsigned char right_byte = i < right.size()
            ? static_cast<unsigned char>(right[i])
            : 0;
        difference |= static_cast<unsigned char>(left_byte ^ right_byte);
    }
    return difference == 0;
}
