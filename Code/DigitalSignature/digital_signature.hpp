#ifndef SUPPLY_CHAIN_DIGITAL_SIGNATURE_HPP
#define SUPPLY_CHAIN_DIGITAL_SIGNATURE_HPP

#include <string>

bool verify_ecdsa_p256_signature(const std::string& public_key_base64,
                                const std::string& signature_base64,
                                const std::string& message);

#endif
