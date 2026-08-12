// log_utils.hpp
#ifndef LOG_UTILS_HPP
#define LOG_UTILS_HPP

#include<string>

void log_to_file(const std::string& log_line);
std::string current_timestamp();
std::string get_client_ip(int client_fd);

#endif // LOG_UTILS_HPP
