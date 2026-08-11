#include <arpa/inet.h>
#include <algorithm>
#include <cctype>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unordered_map>
#include <unistd.h>

#include "MerkleTree.hpp"
#include "log_utils.hpp"

namespace fs = std::filesystem;

#ifndef SERVER_DEFAULT_STATIC_DIR
#define SERVER_DEFAULT_STATIC_DIR "../static"
#endif

namespace
{
constexpr std::size_t MAX_HEADER_SIZE = 64 * 1024;
constexpr std::size_t MAX_BODY_SIZE = 64 * 1024;

struct HttpRequest
{
    std::string method;
    std::string target;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

std::string trim(std::string value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();
    if(first >= last) return "";
    return std::string(first, last);
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ends_with(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string content_type_for(const fs::path& path)
{
    const std::string name = path.string();
    if(ends_with(name, ".html")) return "text/html; charset=utf-8";
    if(ends_with(name, ".css")) return "text/css; charset=utf-8";
    if(ends_with(name, ".js")) return "application/javascript; charset=utf-8";
    if(ends_with(name, ".json")) return "application/json; charset=utf-8";
    if(ends_with(name, ".jpg") || ends_with(name, ".jpeg")) return "image/jpeg";
    if(ends_with(name, ".png")) return "image/png";
    if(ends_with(name, ".gif")) return "image/gif";
    if(ends_with(name, ".svg")) return "image/svg+xml";
    if(ends_with(name, ".pdf")) return "application/pdf";
    if(ends_with(name, ".mp4")) return "video/mp4";
    return "application/octet-stream";
}

bool read_request(int client_fd, HttpRequest& request)
{
    std::string raw;
    char buffer[4096];

    while(raw.find("\r\n\r\n") == std::string::npos)
    {
        const ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));
        if(bytes_read <= 0) return false;

        raw.append(buffer, static_cast<std::size_t>(bytes_read));
        if(raw.size() > MAX_HEADER_SIZE) return false;
    }

    const std::size_t header_end = raw.find("\r\n\r\n");
    const std::size_t line_end = raw.find("\r\n");
    if(header_end == std::string::npos || line_end == std::string::npos) return false;

    std::istringstream line(raw.substr(0, line_end));
    if(!(line >> request.method >> request.target >> request.version)) return false;

    std::size_t cursor = line_end + 2;
    while(cursor < header_end)
    {
        const std::size_t next = raw.find("\r\n", cursor);
        if(next == std::string::npos || next > header_end) return false;

        const std::string header_line = raw.substr(cursor, next - cursor);
        const std::size_t colon = header_line.find(':');
        if(colon == std::string::npos) return false;

        request.headers[lower(trim(header_line.substr(0, colon)))] =
            trim(header_line.substr(colon + 1));
        cursor = next + 2;
    }

    std::size_t content_length = 0;
    const auto length_header = request.headers.find("content-length");
    if(length_header != request.headers.end())
    {
        try
        {
            content_length = std::stoull(length_header->second);
        }
        catch(const std::exception&)
        {
            return false;
        }
    }
    if(content_length > MAX_BODY_SIZE) return false;

    const std::size_t body_start = header_end + 4;
    while(raw.size() - body_start < content_length)
    {
        const ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));
        if(bytes_read <= 0) return false;
        raw.append(buffer, static_cast<std::size_t>(bytes_read));
    }

    request.body = raw.substr(body_start, content_length);
    return true;
}

bool write_all(int client_fd, const std::string& data)
{
    std::size_t written = 0;
    while(written < data.size())
    {
        const ssize_t count = write(client_fd,
                                    data.data() + written,
                                    data.size() - written);
        if(count <= 0) return false;
        written += static_cast<std::size_t>(count);
    }
    return true;
}

void send_response(int client_fd,
                   const std::string& status,
                   const std::string& content_type,
                   const std::string& body,
                   bool send_body,
                   const std::string& extra_headers = "")
{
    std::ostringstream response;
    response << "HTTP/1.1 " << status << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "X-Content-Type-Options: nosniff\r\n"
             << extra_headers
             << "\r\n";

    write_all(client_fd, response.str());
    if(send_body) write_all(client_fd, body);
}

std::optional<std::string> percent_decode(const std::string& value)
{
    auto hex_value = [](char ch) -> int {
        if(ch >= '0' && ch <= '9') return ch - '0';
        if(ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if(ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };

    std::string decoded;
    decoded.reserve(value.size());
    for(std::size_t i = 0; i < value.size(); i++)
    {
        if(value[i] != '%')
        {
            decoded += value[i];
            continue;
        }

        if(i + 2 >= value.size()) return std::nullopt;
        const int high = hex_value(value[i + 1]);
        const int low = hex_value(value[i + 2]);
        if(high < 0 || low < 0) return std::nullopt;

        const char decoded_char = static_cast<char>((high << 4) | low);
        if(decoded_char == '\0') return std::nullopt;
        decoded += decoded_char;
        i += 2;
    }
    return decoded;
}

std::optional<std::string> form_decode(std::string value)
{
    std::replace(value.begin(), value.end(), '+', ' ');
    return percent_decode(value);
}

std::optional<std::unordered_map<std::string, std::string>> parse_form(
    const std::string& body)
{
    std::unordered_map<std::string, std::string> fields;
    std::size_t cursor = 0;

    while(cursor <= body.size())
    {
        const std::size_t ampersand = body.find('&', cursor);
        const std::string pair = body.substr(
            cursor,
            ampersand == std::string::npos ? std::string::npos : ampersand - cursor);
        const std::size_t equals = pair.find('=');
        if(equals == std::string::npos) return std::nullopt;

        const auto key = form_decode(pair.substr(0, equals));
        const auto value = form_decode(pair.substr(equals + 1));
        if(!key || !value || key->empty()) return std::nullopt;
        fields[*key] = *value;

        if(ampersand == std::string::npos) break;
        cursor = ampersand + 1;
    }

    return fields;
}

std::string json_escape(const std::string& value)
{
    std::ostringstream escaped;
    for(const unsigned char ch : value)
    {
        switch(ch)
        {
            case '"': escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if(ch < 0x20)
                {
                    escaped << "\\u00";
                    const char* digits = "0123456789abcdef";
                    escaped << digits[(ch >> 4) & 0x0f] << digits[ch & 0x0f];
                }
                else
                {
                    escaped << static_cast<char>(ch);
                }
        }
    }
    return escaped.str();
}

std::string canonical_record(const std::unordered_map<std::string, std::string>& fields)
{
    const char* names[] = {"batchId", "product", "origin", "stage", "confirmedBy"};
    std::ostringstream record;
    for(const char* name : names)
    {
        const std::string& value = fields.at(name);
        record << name << ':' << value.size() << ':' << value << '\n';
    }
    return record.str();
}

std::string json_error(const std::string& message)
{
    return "{\"error\":\"" + json_escape(message) + "\"}";
}

bool is_inside(const fs::path& root, const fs::path& candidate)
{
    auto root_part = root.begin();
    auto candidate_part = candidate.begin();
    for(; root_part != root.end(); ++root_part, ++candidate_part)
    {
        if(candidate_part == candidate.end() || *root_part != *candidate_part)
            return false;
    }
    return true;
}

std::optional<fs::path> resolve_static_path(const fs::path& static_root,
                                            const std::string& target)
{
    const std::size_t query = target.find_first_of("?#");
    const std::string raw_path = target.substr(0, query);
    const auto decoded = percent_decode(raw_path);
    if(!decoded || decoded->empty() || decoded->front() != '/') return std::nullopt;

    std::string relative = decoded->substr(1);
    if(relative.empty()) relative = "Home.html";

    fs::path candidate = fs::weakly_canonical(static_root / fs::path(relative));
    if(!is_inside(static_root, candidate)) return std::nullopt;

    if(fs::is_directory(candidate)) candidate /= "Home.html";
    if(!fs::exists(candidate) && candidate.extension().empty())
        candidate = fs::weakly_canonical(candidate.string() + ".html");

    if(!is_inside(static_root, candidate)) return std::nullopt;
    return candidate;
}

std::string read_file(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if(!file) return "";
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

int parse_port(const char* value)
{
    try
    {
        const int port = std::stoi(value);
        if(port < 1 || port > 65535) throw std::out_of_range("port");
        return port;
    }
    catch(const std::exception&)
    {
        throw std::invalid_argument("port must be an integer from 1 to 65535");
    }
}
}

int main(int argc, char* argv[])
{
    std::signal(SIGPIPE, SIG_IGN);

    int port = 8080;
    try
    {
        if(argc >= 2) port = parse_port(argv[1]);
    }
    catch(const std::exception& error)
    {
        std::cerr << "Invalid port: " << error.what() << '\n';
        return 1;
    }

    const fs::path requested_static = argc >= 3
        ? fs::path(argv[2])
        : fs::path(SERVER_DEFAULT_STATIC_DIR);
    const fs::path static_root = fs::weakly_canonical(fs::absolute(requested_static));
    if(!fs::is_directory(static_root))
    {
        std::cerr << "Static directory not found: " << static_root << '\n';
        return 1;
    }

    MerkleTree merkle_tree(1);

    const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd == -1)
    {
        perror("socket");
        return 1;
    }

    int reuse_address = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               &reuse_address, sizeof(reuse_address));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(port));

    if(bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if(listen(server_fd, 16) == -1)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    std::cout << "HTTP server: http://127.0.0.1:" << port << '\n'
              << "Static root: " << static_root << '\n'
              << "Merkle Tree: in-memory\n";

    while(true)
    {
        const int client_fd = accept(server_fd, nullptr, nullptr);
        if(client_fd == -1)
        {
            perror("accept");
            continue;
        }

        HttpRequest request;
        if(!read_request(client_fd, request))
        {
            send_response(client_fd, "400 Bad Request", "text/plain; charset=utf-8",
                          "Bad Request\n", true);
            close(client_fd);
            continue;
        }

        std::string status;
        std::string body;
        std::string content_type = "text/plain; charset=utf-8";
        const bool is_head = request.method == "HEAD";

        if(request.method == "POST" && request.target == "/api/records")
        {
            content_type = "application/json; charset=utf-8";
            const auto content_type_header = request.headers.find("content-type");
            const bool correct_type = content_type_header != request.headers.end() &&
                lower(content_type_header->second).find(
                    "application/x-www-form-urlencoded") == 0;
            const auto fields = correct_type ? parse_form(request.body) : std::nullopt;

            const char* required[] = {
                "batchId", "product", "origin", "stage", "confirmedBy"
            };
            std::string validation_error;
            if(!correct_type)
                validation_error = "Content-Type must be application/x-www-form-urlencoded";
            else if(!fields)
                validation_error = "Malformed form body";
            else
            {
                for(const char* name : required)
                {
                    const auto item = fields->find(name);
                    if(item == fields->end() || trim(item->second).empty())
                    {
                        validation_error = std::string(name) + " is required";
                        break;
                    }
                    if(item->second.size() > 256)
                    {
                        validation_error = std::string(name) + " is too long";
                        break;
                    }
                }

                const auto confirmed = fields->find("confirmed");
                if(validation_error.empty() &&
                   (confirmed == fields->end() || confirmed->second != "true"))
                    validation_error = "Information must be confirmed";
            }

            if(!validation_error.empty())
            {
                status = "422 Unprocessable Entity";
                body = json_error(validation_error);
            }
            else
            {
                const int block_id = merkle_tree.GetBlockCount();
                const std::string record = canonical_record(*fields);
                if(!merkle_tree.Append(record))
                {
                    status = "500 Internal Server Error";
                    body = json_error("Failed to append Merkle block");
                }
                else
                {
                    const std::string proof = merkle_tree.ProverBlock(block_id);
                    const bool verified = merkle_tree.Verify(proof);
                    status = "201 Created";
                    body = "{\"blockID\":" + std::to_string(block_id) +
                           ",\"rootHash\":\"" +
                           json_escape(merkle_tree.GetRootHash()) +
                           "\",\"proof\":\"" + json_escape(proof) +
                           "\",\"verified\":" + (verified ? "true" : "false") + "}";
                }
            }

            send_response(client_fd, status, content_type, body, true);
        }
        else if(request.method != "GET" && !is_head)
        {
            status = "405 Method Not Allowed";
            body = "Method Not Allowed\n";
            send_response(client_fd, status, content_type, body, true,
                          "Allow: GET, HEAD\r\n");
        }
        else
        {
            const auto file_path = resolve_static_path(static_root, request.target);
            if(file_path && fs::is_regular_file(*file_path))
            {
                status = "200 OK";
                body = read_file(*file_path);
                content_type = content_type_for(*file_path);
            }
            else
            {
                status = file_path ? "404 Not Found" : "403 Forbidden";
                const fs::path error_page = static_root / "404.html";
                if(fs::is_regular_file(error_page))
                {
                    body = read_file(error_page);
                    content_type = "text/html; charset=utf-8";
                }
                else
                {
                    body = status + "\n";
                }
            }

            send_response(client_fd, status, content_type, body, !is_head);
        }

        const std::string log_line = "[" + current_timestamp() + "] " +
            get_client_ip(client_fd) + " " + request.method + " " +
            request.target + " -> " + status + "\n";
        std::cout << log_line;
        log_to_file(log_line);
        close(client_fd);
    }
}
