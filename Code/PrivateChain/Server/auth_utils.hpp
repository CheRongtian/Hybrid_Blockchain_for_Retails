#ifndef AUTH_UTILS_HPP
#define AUTH_UTILS_HPP

#include <cstddef>
#include <string>

std::string generate_random_hex(std::size_t byte_count);
std::string hash_session_token(const std::string& token);
std::string hash_password(const std::string& password,
                          const std::string& salt);
bool secure_string_equal(const std::string& left,
                         const std::string& right);

#endif
