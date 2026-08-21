#include <arpa/inet.h>
#include <algorithm>
#include <cctype>
#include <csignal>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unordered_set>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "auth_utils.hpp"
#include "block_merkle.hpp"
#include "db_utils.hpp"
#include "digital_signature.hpp"
#include "identifier_utils.hpp"
#include "log_utils.hpp"
#include "gateway_payload.hpp"
#include "snapshot_adapter.hpp"
#include "snapshot_policy.hpp"
#include "snapshot_scheduler.hpp"
#include "snapshot_storage.hpp"
#include "thread_pool.hpp"

namespace fs = std::filesystem;

#ifndef CONTROL_DEFAULT_STATIC_DIR
#define CONTROL_DEFAULT_STATIC_DIR "../control_static"
#endif

#ifndef CONTROL_DEFAULT_DATABASE_PATH
#define CONTROL_DEFAULT_DATABASE_PATH "../PrivateChain/Database/supply_chain.db"
#endif

namespace
{
constexpr std::size_t MAX_HEADER_SIZE = 64 * 1024;
constexpr std::size_t MAX_BODY_SIZE = 32 * 1024 * 1024;
constexpr std::size_t MAX_IPFS_FILE_SIZE = 30 * 1024 * 1024;
constexpr const char* CORS_HEADERS =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type, Authorization, X-Publication-Token\r\n";

struct HttpRequest
{
    std::string method;
    std::string target;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct Session
{
    UserAccount account;
    std::chrono::steady_clock::time_point expires_at;
};

using SessionStore = std::unordered_map<std::string, Session>;

struct ConfirmationChallenge
{
    std::string uid;
    std::chrono::steady_clock::time_point expires_at;
};

using ConfirmationChallengeStore =
    std::unordered_map<std::string, ConfirmationChallenge>;

constexpr auto TEMPORARY_SESSION_LIFETIME = std::chrono::hours(8);
constexpr auto PERSISTENT_SESSION_LIFETIME = std::chrono::hours(24 * 30);
constexpr auto CONFIRMATION_CHALLENGE_LIFETIME = std::chrono::minutes(5);

std::int64_t unix_time_now()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

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

bool parse_boolean(const std::unordered_map<std::string, std::string>& fields,
                  const std::string& name)
{
    const auto item = fields.find(name);
    if(item == fields.end()) return false;
    const std::string value = lower(trim(item->second));
    return value == "true" || value == "1" || value == "on";
}

std::optional<std::string> authorization_token(const HttpRequest& request)
{
    const auto authorization = request.headers.find("authorization");
    if(authorization == request.headers.end()) return std::nullopt;

    const std::string header = authorization->second;
    if(header.size() < 7 || lower(header.substr(0, 7)) != "bearer ")
        return std::nullopt;

    const std::string token = trim(header.substr(7));
    if(token.empty()) return std::nullopt;
    return token;
}

std::optional<UserAccount> authenticated_user(
    const HttpRequest& request,
    const fs::path& database_path,
    SessionStore& sessions,
    std::mutex& sessions_mutex)
{
    const auto token = authorization_token(request);
    if(!token) return std::nullopt;

    {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        const auto session = sessions.find(*token);
        if(session != sessions.end())
        {
            if(session->second.expires_at <= std::chrono::steady_clock::now())
            {
                sessions.erase(session);
            }
            else
            {
                return session->second.account;
            }
        }
    }

    const auto persistent = find_persistent_auth_session(
        database_path.string(), hash_session_token(*token), unix_time_now());
    if(!persistent) return std::nullopt;
    return persistent->account;
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

std::string form_field_value(
    const std::unordered_map<std::string, std::string>& fields,
    const std::string& name)
{
    const auto item = fields.find(name);
    return item == fields.end() ? "" : item->second;
}

std::vector<std::string> split_text(const std::string& value, char separator);

std::string request_path(const std::string& target)
{
    const std::size_t query = target.find('?');
    return target.substr(0, query);
}

std::optional<std::string> query_parameter(const std::string& target,
                                           const std::string& name)
{
    const std::size_t query_start = target.find('?');
    if(query_start == std::string::npos) return std::nullopt;
    const std::string query = target.substr(query_start + 1);
    for(const std::string& pair : split_text(query, '&'))
    {
        const std::size_t equals = pair.find('=');
        if(equals == std::string::npos) continue;
        const auto key = percent_decode(pair.substr(0, equals));
        const auto value = percent_decode(pair.substr(equals + 1));
        if(key && value && *key == name) return *value;
    }
    return std::nullopt;
}

bool parse_integer(const std::string& value, int& output)
{
    try
    {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if(consumed != value.size()) return false;
        output = parsed;
        return true;
    }
    catch(const std::exception&)
    {
        return false;
    }
}

bool parse_workflow_nodes(const std::string& encoded,
                          std::vector<SupplyRouteNode>& nodes,
                          std::string& error)
{
    nodes.clear();
    if(encoded.empty())
    {
        error = "Workflow must contain route nodes";
        return false;
    }
    for(const std::string& token : split_text(encoded, ';'))
    {
        const auto parts = split_text(token, '|');
        if(parts.size() != 8)
        {
            error = "Malformed workflow node data";
            return false;
        }
        std::vector<std::string> decoded;
        for(const std::string& part : parts)
        {
            const auto value = percent_decode(part);
            if(!value)
            {
                error = "Malformed workflow node encoding";
                return false;
            }
            decoded.push_back(*value);
        }
        int x = 0;
        int y = 0;
        int step_index = -1;
        if(!parse_integer(decoded[5], x) || !parse_integer(decoded[6], y) ||
           !parse_integer(decoded[7], step_index))
        {
            error = "Malformed workflow node position";
            return false;
        }
        nodes.push_back(SupplyRouteNode{
            "", decoded[0], decoded[1], decoded[2], decoded[3], decoded[4],
            x, y, step_index
        });
    }
    return true;
}

bool parse_workflow_edges(const std::string& encoded,
                          std::vector<SupplyRouteEdge>& edges,
                          std::string& error)
{
    edges.clear();
    if(encoded.empty())
    {
        error = "Workflow must contain route connections";
        return false;
    }
    for(const std::string& token : split_text(encoded, ';'))
    {
        const auto parts = split_text(token, '|');
        if(parts.size() != 2)
        {
            error = "Malformed workflow edge data";
            return false;
        }
        const auto from = percent_decode(parts[0]);
        const auto to = percent_decode(parts[1]);
        if(!from || !to)
        {
            error = "Malformed workflow edge encoding";
            return false;
        }
        edges.push_back(SupplyRouteEdge{"", *from, *to});
    }
    return true;
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

struct LiveEvent
{
    std::uint64_t id = 0;
    std::string payload;
};

class LiveEventHub
{
public:
    void publish(const std::string& type, const std::string& batch_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(LiveEvent{
            ++sequence_,
            "{\"type\":\"" + json_escape(type) +
                "\",\"batchId\":\"" + json_escape(batch_id) + "\"}"
        });
        while(events_.size() > 64) events_.pop_front();
        condition_.notify_all();
    }

    std::uint64_t latest_id() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sequence_;
    }

    std::vector<LiveEvent> after(std::uint64_t id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<LiveEvent> result;
        for(const LiveEvent& event : events_)
        {
            if(event.id > id) result.push_back(event);
        }
        return result;
    }

    bool wait_for_change(std::uint64_t id) const
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_for(lock, std::chrono::seconds(25), [&] {
            return sequence_ > id;
        });
        return sequence_ > id;
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable condition_;
    std::deque<LiveEvent> events_;
    std::uint64_t sequence_ = 0;
};

LiveEventHub live_event_hub;

bool stream_live_events(int client_fd)
{
    const std::string headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "X-Content-Type-Options: nosniff\r\n" +
        std::string(CORS_HEADERS) + "\r\n";
    if(!write_all(client_fd, headers)) return false;
    if(!write_all(client_fd, ": connected\n\n")) return false;

    std::uint64_t last_id = live_event_hub.latest_id();
    const std::string initial_state =
        "id: " + std::to_string(last_id) +
        "\ndata: {\"type\":\"state_sync\",\"batchId\":\"\"}\n\n";
    if(!write_all(client_fd, initial_state)) return false;
    while(true)
    {
        if(!live_event_hub.wait_for_change(last_id))
        {
            if(!write_all(client_fd, ": keep-alive\n\n")) return false;
            continue;
        }

        const std::vector<LiveEvent> events = live_event_hub.after(last_id);
        for(const LiveEvent& event : events)
        {
            const std::string message = "id: " + std::to_string(event.id) +
                "\ndata: " + event.payload + "\n\n";
            if(!write_all(client_fd, message)) return false;
            last_id = event.id;
        }
    }
}

void skip_json_whitespace(const std::string& value, std::size_t& cursor)
{
    while(cursor < value.size() &&
          std::isspace(static_cast<unsigned char>(value[cursor])))
        ++cursor;
}

bool parse_json_value_syntax(const std::string& value, std::size_t& cursor);

bool parse_json_string_syntax(const std::string& value, std::size_t& cursor)
{
    if(cursor >= value.size() || value[cursor] != '"') return false;
    ++cursor;
    while(cursor < value.size())
    {
        const unsigned char character =
            static_cast<unsigned char>(value[cursor++]);
        if(character == '"') return true;
        if(character < 0x20) return false;
        if(character != '\\') continue;
        if(cursor >= value.size()) return false;
        const char escape = value[cursor++];
        if(escape == 'u')
        {
            if(cursor + 4 > value.size()) return false;
            for(std::size_t index = 0; index < 4; ++index)
            {
                if(!std::isxdigit(
                       static_cast<unsigned char>(value[cursor + index])))
                    return false;
            }
            cursor += 4;
        }
        else if(escape != '"' && escape != '\\' && escape != '/' &&
                escape != 'b' && escape != 'f' && escape != 'n' &&
                escape != 'r' && escape != 't')
        {
            return false;
        }
    }
    return false;
}

bool parse_json_number_syntax(const std::string& value, std::size_t& cursor)
{
    const std::size_t start = cursor;
    if(cursor < value.size() && value[cursor] == '-') ++cursor;
    if(cursor >= value.size()) return false;
    if(value[cursor] == '0')
    {
        ++cursor;
        if(cursor < value.size() && std::isdigit(
               static_cast<unsigned char>(value[cursor])))
            return false;
    }
    else
    {
        if(value[cursor] < '1' || value[cursor] > '9') return false;
        while(cursor < value.size() && std::isdigit(
                  static_cast<unsigned char>(value[cursor])))
            ++cursor;
    }
    if(cursor < value.size() && value[cursor] == '.')
    {
        ++cursor;
        const std::size_t fraction_start = cursor;
        while(cursor < value.size() && std::isdigit(
                  static_cast<unsigned char>(value[cursor])))
            ++cursor;
        if(cursor == fraction_start) return false;
    }
    if(cursor < value.size() && (value[cursor] == 'e' ||
                                 value[cursor] == 'E'))
    {
        ++cursor;
        if(cursor < value.size() &&
           (value[cursor] == '+' || value[cursor] == '-'))
            ++cursor;
        const std::size_t exponent_start = cursor;
        while(cursor < value.size() && std::isdigit(
                  static_cast<unsigned char>(value[cursor])))
            ++cursor;
        if(cursor == exponent_start) return false;
    }
    return cursor > start;
}

bool parse_json_array_syntax(const std::string& value, std::size_t& cursor)
{
    if(cursor >= value.size() || value[cursor] != '[') return false;
    ++cursor;
    skip_json_whitespace(value, cursor);
    if(cursor < value.size() && value[cursor] == ']')
    {
        ++cursor;
        return true;
    }
    while(cursor < value.size())
    {
        if(!parse_json_value_syntax(value, cursor)) return false;
        skip_json_whitespace(value, cursor);
        if(cursor < value.size() && value[cursor] == ']')
        {
            ++cursor;
            return true;
        }
        if(cursor >= value.size() || value[cursor++] != ',') return false;
        skip_json_whitespace(value, cursor);
    }
    return false;
}

bool parse_json_object_syntax(const std::string& value, std::size_t& cursor)
{
    if(cursor >= value.size() || value[cursor] != '{') return false;
    ++cursor;
    skip_json_whitespace(value, cursor);
    if(cursor < value.size() && value[cursor] == '}')
    {
        ++cursor;
        return true;
    }
    while(cursor < value.size())
    {
        if(!parse_json_string_syntax(value, cursor)) return false;
        skip_json_whitespace(value, cursor);
        if(cursor >= value.size() || value[cursor++] != ':') return false;
        skip_json_whitespace(value, cursor);
        if(!parse_json_value_syntax(value, cursor)) return false;
        skip_json_whitespace(value, cursor);
        if(cursor < value.size() && value[cursor] == '}')
        {
            ++cursor;
            return true;
        }
        if(cursor >= value.size() || value[cursor++] != ',') return false;
        skip_json_whitespace(value, cursor);
    }
    return false;
}

bool parse_json_value_syntax(const std::string& value, std::size_t& cursor)
{
    skip_json_whitespace(value, cursor);
    if(cursor >= value.size()) return false;
    if(value[cursor] == '"') return parse_json_string_syntax(value, cursor);
    if(value[cursor] == '{') return parse_json_object_syntax(value, cursor);
    if(value[cursor] == '[') return parse_json_array_syntax(value, cursor);
    if(value.compare(cursor, 4, "true") == 0)
    {
        cursor += 4;
        return true;
    }
    if(value.compare(cursor, 5, "false") == 0)
    {
        cursor += 5;
        return true;
    }
    if(value.compare(cursor, 4, "null") == 0)
    {
        cursor += 4;
        return true;
    }
    return parse_json_number_syntax(value, cursor);
}

std::string json_value_or_empty_object(const std::string& value)
{
    const std::string trimmed = trim(value);
    if(trimmed.empty()) return "{}";
    std::size_t cursor = 0;
    if(!parse_json_value_syntax(trimmed, cursor)) return "{}";
    skip_json_whitespace(trimmed, cursor);
    return cursor == trimmed.size() ? trimmed : "{}";
}

struct MultipartPart
{
    std::string name;
    std::string filename;
    std::string content_type = "application/octet-stream";
    std::string content;
};

std::optional<std::string> multipart_parameter(const std::string& value,
                                               const std::string& name)
{
    const std::string quoted_name = name + "=\"";
    const std::size_t quoted_start = value.find(quoted_name);
    if(quoted_start != std::string::npos)
    {
        const std::size_t value_start = quoted_start + quoted_name.size();
        const std::size_t value_end = value.find('"', value_start);
        if(value_end == std::string::npos) return std::nullopt;
        return value.substr(value_start, value_end - value_start);
    }

    const std::string plain_name = name + "=";
    const std::size_t plain_start = value.find(plain_name);
    if(plain_start == std::string::npos) return std::nullopt;
    const std::size_t value_start = plain_start + plain_name.size();
    const std::size_t value_end = value.find(';', value_start);
    return value.substr(value_start,
                        value_end == std::string::npos
                            ? std::string::npos
                            : value_end - value_start);
}

std::optional<std::vector<MultipartPart>> parse_multipart(
    const std::string& content_type,
    const std::string& body)
{
    const std::string lowered = lower(content_type);
    const std::size_t boundary_start = lowered.find("boundary=");
    if(lowered.find("multipart/form-data") != 0 ||
       boundary_start == std::string::npos)
        return std::nullopt;

    std::string boundary = trim(content_type.substr(boundary_start + 9));
    if(!boundary.empty() && boundary.front() == '"' && boundary.back() == '"')
        boundary = boundary.substr(1, boundary.size() - 2);
    if(boundary.empty()) return std::nullopt;

    const std::string marker = "--" + boundary;
    std::vector<MultipartPart> parts;
    std::size_t cursor = body.find(marker);
    while(cursor != std::string::npos)
    {
        const std::size_t marker_end = cursor + marker.size();
        if(marker_end + 2 <= body.size() && body.compare(marker_end, 2, "--") == 0)
            break;
        if(marker_end + 2 > body.size() || body.compare(marker_end, 2, "\r\n") != 0)
            return std::nullopt;

        const std::size_t headers_start = marker_end + 2;
        const std::size_t headers_end = body.find("\r\n\r\n", headers_start);
        if(headers_end == std::string::npos) return std::nullopt;

        std::unordered_map<std::string, std::string> headers;
        std::size_t header_cursor = headers_start;
        while(header_cursor < headers_end)
        {
            const std::size_t line_end = body.find("\r\n", header_cursor);
            if(line_end == std::string::npos || line_end > headers_end)
                return std::nullopt;
            const std::string line = body.substr(header_cursor, line_end - header_cursor);
            const std::size_t colon = line.find(':');
            if(colon == std::string::npos) return std::nullopt;
            headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
            header_cursor = line_end + 2;
        }

        const auto disposition = headers.find("content-disposition");
        if(disposition == headers.end()) return std::nullopt;
        const auto name = multipart_parameter(disposition->second, "name");
        if(!name) return std::nullopt;

        const std::size_t content_start = headers_end + 4;
        const std::size_t next_marker = body.find("\r\n" + marker, content_start);
        if(next_marker == std::string::npos) return std::nullopt;

        MultipartPart part;
        part.name = *name;
        part.filename = multipart_parameter(disposition->second, "filename")
            .value_or("");
        const auto content_type_header = headers.find("content-type");
        if(content_type_header != headers.end() && !content_type_header->second.empty())
            part.content_type = content_type_header->second;
        part.content = body.substr(content_start, next_marker - content_start);
        parts.push_back(std::move(part));

        cursor = next_marker + 2;
    }

    return parts;
}

struct HttpEndpoint
{
    std::string host;
    std::string port;
};

std::optional<HttpEndpoint> parse_http_endpoint(const std::string& url,
                                                const std::string& default_port)
{
    if(url.rfind("http://", 0) != 0) return std::nullopt;

    const std::string authority = url.substr(7);
    const std::size_t path_start = authority.find('/');
    const std::string host_port = authority.substr(0, path_start);
    if(host_port.empty()) return std::nullopt;

    const std::size_t colon = host_port.rfind(':');
    HttpEndpoint endpoint;
    endpoint.host = colon == std::string::npos
        ? host_port
        : host_port.substr(0, colon);
    endpoint.port = colon == std::string::npos
        ? default_port
        : host_port.substr(colon + 1);
    if(endpoint.host.empty() || endpoint.port.empty()) return std::nullopt;
    return endpoint;
}

std::optional<HttpEndpoint> ipfs_endpoint()
{
    const char* configured = std::getenv("IPFS_API_URL");
    return parse_http_endpoint(
        configured && *configured ? configured : "http://127.0.0.1:5002",
        "5002");
}

std::optional<HttpEndpoint> public_chain_endpoint()
{
    const char* configured = std::getenv("PUBLIC_CHAIN_SERVICE_URL");
    return parse_http_endpoint(
        configured && *configured ? configured : "http://127.0.0.1:8082",
        "8082");
}

std::string public_chain_publication_token()
{
    const char* configured = std::getenv("PUBLIC_CHAIN_PUBLICATION_TOKEN");
    return configured && *configured
        ? configured
        : "local-publication-demo-token";
}

int connect_to_endpoint(const HttpEndpoint& endpoint)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    if(getaddrinfo(endpoint.host.c_str(), endpoint.port.c_str(), &hints, &addresses) != 0)
        return -1;

    int socket_fd = -1;
    for(addrinfo* address = addresses; address; address = address->ai_next)
    {
        socket_fd = socket(address->ai_family, address->ai_socktype,
                           address->ai_protocol);
        if(socket_fd == -1) continue;
        if(connect(socket_fd, address->ai_addr, address->ai_addrlen) == 0)
            break;
        close(socket_fd);
        socket_fd = -1;
    }

    freeaddrinfo(addresses);
    return socket_fd;
}

std::string safe_header_filename(std::string filename)
{
    for(char& character : filename)
    {
        if(character == '"' || character == '\r' || character == '\n')
            character = '_';
    }
    return filename.empty() ? "upload.bin" : filename;
}

std::optional<std::string> json_string_value(const std::string& body,
                                             const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    const std::size_t key_position = body.find(needle);
    if(key_position == std::string::npos) return std::nullopt;
    const std::size_t colon = body.find(':', key_position + needle.size());
    if(colon == std::string::npos) return std::nullopt;
    const std::size_t quote_start = body.find('"', colon + 1);
    if(quote_start == std::string::npos) return std::nullopt;

    std::string value;
    for(std::size_t cursor = quote_start + 1; cursor < body.size(); ++cursor)
    {
        if(body[cursor] == '"') return value;
        if(body[cursor] == '\\' && cursor + 1 < body.size())
        {
            value += body[++cursor];
            continue;
        }
        value += body[cursor];
    }
    return std::nullopt;
}

std::optional<int> json_integer_value(const std::string& body,
                                      const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    const std::size_t key_position = body.find(needle);
    if(key_position == std::string::npos) return std::nullopt;
    const std::size_t colon = body.find(':', key_position + needle.size());
    if(colon == std::string::npos) return std::nullopt;

    std::size_t cursor = colon + 1;
    while(cursor < body.size() &&
          std::isspace(static_cast<unsigned char>(body[cursor])))
        ++cursor;
    const std::size_t number_start = cursor;
    if(cursor < body.size() && body[cursor] == '-') ++cursor;
    const std::size_t digits_start = cursor;
    while(cursor < body.size() &&
          std::isdigit(static_cast<unsigned char>(body[cursor])))
        ++cursor;
    if(cursor == digits_start) return std::nullopt;

    try
    {
        std::size_t parsed = 0;
        const int value = std::stoi(body.substr(number_start,
                                                 cursor - number_start),
                                    &parsed);
        return parsed == cursor - number_start ?
            std::optional<int>(value) : std::nullopt;
    }
    catch(const std::exception&)
    {
        return std::nullopt;
    }
}

struct IpfsAddResult
{
    std::string cid;
};

std::optional<IpfsAddResult> add_file_to_ipfs(const MultipartPart& file)
{
    const auto endpoint = ipfs_endpoint();
    if(!endpoint) return std::nullopt;

    const int socket_fd = connect_to_endpoint(*endpoint);
    if(socket_fd == -1) return std::nullopt;

    const std::string boundary = "supply-chain-ipfs-" + generate_random_hex(8);
    const std::string filename = safe_header_filename(file.filename);
    std::ostringstream multipart;
    multipart << "--" << boundary << "\r\n"
              << "Content-Disposition: form-data; name=\"file\"; filename=\""
              << filename << "\"\r\n"
              << "Content-Type: " << file.content_type << "\r\n\r\n";
    const std::string prefix = multipart.str();
    const std::string suffix = "\r\n--" + boundary + "--\r\n";
    const std::size_t body_size = prefix.size() + file.content.size() + suffix.size();

    std::ostringstream request;
    request << "POST /api/v0/add?pin=true&cid-version=1 HTTP/1.1\r\n"
            << "Host: " << endpoint->host << ':' << endpoint->port << "\r\n"
            << "Content-Type: multipart/form-data; boundary=" << boundary << "\r\n"
            << "Content-Length: " << body_size << "\r\n"
            << "Connection: close\r\n\r\n"
            << prefix;
    const std::string request_prefix = request.str();
    const std::string request_suffix = suffix;

    bool sent = write_all(socket_fd, request_prefix);
    if(sent && !file.content.empty())
        sent = write_all(socket_fd, file.content);
    if(sent) sent = write_all(socket_fd, request_suffix);
    if(!sent)
    {
        close(socket_fd);
        return std::nullopt;
    }

    std::string response;
    char buffer[8192];
    while(true)
    {
        const ssize_t bytes_read = read(socket_fd, buffer, sizeof(buffer));
        if(bytes_read == 0) break;
        if(bytes_read < 0)
        {
            close(socket_fd);
            return std::nullopt;
        }
        response.append(buffer, static_cast<std::size_t>(bytes_read));
        if(response.size() > MAX_BODY_SIZE) break;
    }
    close(socket_fd);

    const std::size_t status_end = response.find("\r\n");
    if(status_end == std::string::npos) return std::nullopt;
    std::istringstream status_line(response.substr(0, status_end));
    std::string version;
    int status_code = 0;
    status_line >> version >> status_code;
    if(status_code < 200 || status_code >= 300) return std::nullopt;

    const std::size_t header_end = response.find("\r\n\r\n");
    if(header_end == std::string::npos) return std::nullopt;
    const std::string response_body = response.substr(header_end + 4);
    const auto cid = json_string_value(response_body, "Hash");
    if(!cid || cid->empty()) return std::nullopt;
    return IpfsAddResult{*cid};
}

struct HttpJsonResult
{
    int status_code = 0;
    std::string body;
};

std::optional<HttpJsonResult> post_publication_candidate(
    const std::string& candidate_json)
{
    const auto endpoint = public_chain_endpoint();
    if(!endpoint) return std::nullopt;
    const int socket_fd = connect_to_endpoint(*endpoint);
    if(socket_fd == -1) return std::nullopt;

    std::ostringstream request;
    request << "POST /api/publish HTTP/1.1\r\n"
            << "Host: " << endpoint->host << ':' << endpoint->port << "\r\n"
            << "Content-Type: application/json\r\n"
            << "X-Publication-Token: "
            << public_chain_publication_token() << "\r\n"
            << "Content-Length: " << candidate_json.size() << "\r\n"
            << "Connection: close\r\n\r\n"
            << candidate_json;
    if(!write_all(socket_fd, request.str()))
    {
        close(socket_fd);
        return std::nullopt;
    }

    std::string response;
    char buffer[8192];
    while(response.size() <= MAX_BODY_SIZE)
    {
        const ssize_t bytes_read = read(socket_fd, buffer, sizeof(buffer));
        if(bytes_read == 0) break;
        if(bytes_read < 0)
        {
            close(socket_fd);
            return std::nullopt;
        }
        response.append(buffer, static_cast<std::size_t>(bytes_read));
    }
    close(socket_fd);
    if(response.size() > MAX_BODY_SIZE) return std::nullopt;

    const std::size_t status_end = response.find("\r\n");
    const std::size_t header_end = response.find("\r\n\r\n");
    if(status_end == std::string::npos || header_end == std::string::npos)
        return std::nullopt;
    std::istringstream status_line(response.substr(0, status_end));
    std::string version;
    HttpJsonResult result;
    status_line >> version >> result.status_code;
    if(result.status_code < 100) return std::nullopt;
    result.body = response.substr(header_end + 4);
    return result;
}

std::vector<std::string> role_event_fields(const std::string& role)
{
    if(role == "supplier")
        return {"harvestDate", "farmLocation", "certificateId"};
    if(role == "logistics")
        return {"shipmentId", "pickupLocation", "deliveryLocation",
                "departureTime", "arrivalTime", "temperature", "temperatureUnit",
                "humidity",
                "vehicleContainerId"};
    if(role == "warehouse")
        return {"storageLotId", "inboundTime", "outboundTime",
                "temperature", "temperatureUnit", "humidity", "storageZoneRackId"};
    if(role == "supermarket")
        return {"shelfPlacementDate", "expirationSellByDate", "storeLocationId"};
    return {};
}

std::vector<std::string> role_attachment_categories(const std::string& role)
{
    if(role == "supplier")
        return {"pesticideFertilizerRecords", "soilWeatherData", "harvestPhotos",
                "inspectionReports"};
    if(role == "logistics")
        return {"gpsTrackLogs", "temperatureLogs", "transportDocuments",
                "sealVerificationImages"};
    if(role == "warehouse")
        return {"inspectionReports", "fullTemperatureLogs", "energyUsageLogs"};
    if(role == "supermarket")
        return {"productPhotosLabels", "receiptTransactionRecords", "recallNotices",
                "consumerFeedbackData"};
    return {};
}

bool is_allowed_attachment_category(const std::string& role,
                                    const std::string& category)
{
    const auto categories = role_attachment_categories(role);
    return std::find(categories.begin(), categories.end(), category) != categories.end();
}

std::vector<std::string> split_text(const std::string& value, char separator)
{
    std::vector<std::string> pieces;
    std::size_t cursor = 0;
    while(cursor <= value.size())
    {
        const std::size_t next = value.find(separator, cursor);
        pieces.push_back(value.substr(
            cursor,
            next == std::string::npos ? std::string::npos : next - cursor));
        if(next == std::string::npos) break;
        cursor = next + 1;
    }
    return pieces;
}

bool parse_ipfs_references(const std::string& encoded,
                           std::vector<IpfsReference>& references,
                           std::string& error)
{
    references.clear();
    if(encoded.empty()) return true;
    if(encoded.size() > 128 * 1024)
    {
        error = "IPFS reference list is too large";
        return false;
    }

    for(const std::string& token : split_text(encoded, ','))
    {
        const auto pieces = split_text(token, '|');
        if(pieces.size() != 5)
        {
            error = "Malformed IPFS reference";
            return false;
        }

        const auto category = percent_decode(pieces[0]);
        const auto cid = percent_decode(pieces[1]);
        const auto filename = percent_decode(pieces[2]);
        const auto content_type = percent_decode(pieces[3]);
        if(!category || !cid || !filename || !content_type ||
           category->empty() || cid->empty())
        {
            error = "Malformed IPFS reference encoding";
            return false;
        }

        long long size = 0;
        try
        {
            size = std::stoll(pieces[4]);
        }
        catch(const std::exception&)
        {
            error = "Malformed IPFS reference size";
            return false;
        }
        if(size < 0 || size > static_cast<long long>(MAX_IPFS_FILE_SIZE))
        {
            error = "Invalid IPFS reference size";
            return false;
        }

        references.push_back(IpfsReference{
            *category, *cid, *filename, *content_type, size
        });
    }
    return true;
}

std::string percent_encode_component(const std::string& value)
{
    const char* hexadecimal = "0123456789ABCDEF";
    std::string encoded;
    for(const unsigned char character : value)
    {
        const bool unreserved =
            (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_' || character == '.' ||
            character == '!' || character == '~' || character == '*' ||
            character == '\'' || character == '(' || character == ')';
        if(unreserved)
        {
            encoded += static_cast<char>(character);
        }
        else
        {
            encoded += '%';
            encoded += hexadecimal[(character >> 4) & 0x0f];
            encoded += hexadecimal[character & 0x0f];
        }
    }
    return encoded;
}

std::string canonical_ipfs_references(
    const std::vector<IpfsReference>& references)
{
    std::vector<IpfsReference> sorted = references;
    std::sort(sorted.begin(), sorted.end(),
              [](const IpfsReference& left, const IpfsReference& right) {
                  if(left.category != right.category)
                      return left.category < right.category;
                  if(left.cid != right.cid) return left.cid < right.cid;
                  if(left.filename != right.filename)
                      return left.filename < right.filename;
                  if(left.content_type != right.content_type)
                      return left.content_type < right.content_type;
                  return left.size < right.size;
              });

    std::ostringstream encoded;
    for(std::size_t index = 0; index < sorted.size(); ++index)
    {
        if(index > 0) encoded << ',';
        const IpfsReference& reference = sorted[index];
        encoded << percent_encode_component(reference.category) << '|'
                << percent_encode_component(reference.cid) << '|'
                << percent_encode_component(reference.filename) << '|'
                << percent_encode_component(reference.content_type) << '|'
                << reference.size;
    }
    return encoded.str();
}

std::string signature_field(const std::string& name,
                            const std::string& value)
{
    return name + ":" + std::to_string(value.size()) + ":" + value + "\n";
}

std::string confirmation_signature_payload(
    const std::string& challenge,
    const UserAccount& account,
    const std::string& method,
    const std::string& confirmation_name,
    const std::string& batch_id,
    const std::string& product,
    const std::unordered_map<std::string, std::string>& fields,
    const std::vector<IpfsReference>& references,
    bool confirmed)
{
    std::string payload;
    auto add_field = [&](const std::string& name, const std::string& value) {
        payload += signature_field(name, value);
    };

    add_field("challenge", challenge);
    add_field("uid", account.uid);
    add_field("username", account.username);
    add_field("role", account.role);
    add_field("confirmationMethod", method);
    add_field("confirmationName", confirmation_name);
    add_field("batchId", batch_id);
    add_field("product", product);
    add_field("confirmed", confirmed ? "true" : "false");
    for(const std::string& name : role_event_fields(account.role))
    {
        const auto item = fields.find(name);
        add_field("event." + name,
                  item == fields.end() ? "" : item->second);
    }
    add_field("ipfsReferences", canonical_ipfs_references(references));
    return payload;
}

std::string effective_display_name(const UserAccount& account)
{
    return account.display_name.empty() ? account.username : account.display_name;
}

std::string event_data_json(const std::string& role,
                            const std::unordered_map<std::string, std::string>& fields)
{
    std::ostringstream json;
    json << '{';
    const auto names = role_event_fields(role);
    for(std::size_t index = 0; index < names.size(); ++index)
    {
        if(index > 0) json << ',';
        const auto item = fields.find(names[index]);
        const std::string value = item == fields.end() ? "" : item->second;
        json << '"' << json_escape(names[index]) << "\":\""
             << json_escape(value) << '"';
    }
    json << '}';
    return json.str();
}

struct RuntimeWorkflow
{
    std::string route_id;
    std::vector<SupplyRouteNode> nodes;
    std::vector<SupplyRouteEdge> edges;
};

std::optional<RuntimeWorkflow> load_runtime_workflow(
    const std::string& database_path,
    const std::string& batch_id = "")
{
    RuntimeWorkflow workflow;
    if(!load_workflow_route(database_path, batch_id, workflow.route_id,
                            workflow.nodes, workflow.edges))
        return std::nullopt;
    return workflow;
}

const SupplyRouteNode* workflow_node(const RuntimeWorkflow& workflow,
                                     const std::string& node_id)
{
    for(const SupplyRouteNode& node : workflow.nodes)
    {
        if(node.node_id == node_id) return &node;
    }
    return nullptr;
}

const SupplyRouteNode* workflow_node_for_record(
    const RuntimeWorkflow& workflow,
    const SupplyChainRecord& record)
{
    if(!record.route_node_id.empty())
    {
        if(const auto* node = workflow_node(workflow, record.route_node_id);
           node && node->role == record.role &&
           node->username == record.confirmed_by)
            return node;
        return nullptr;
    }

    if(!record.route_id.empty()) return nullptr;

    const SupplyRouteNode* legacy_match = nullptr;
    for(const SupplyRouteNode& node : workflow.nodes)
    {
        if(node.role == record.role && !node.username.empty() &&
           node.username == record.confirmed_by &&
           (record.route_step_index < 0 ||
            node.step_index == record.route_step_index))
        {
            if(legacy_match) return nullptr;
            legacy_match = &node;
        }
    }
    return legacy_match;
}

const SupplyRouteNode* first_workflow_node(const RuntimeWorkflow& workflow)
{
    for(const SupplyRouteNode& node : workflow.nodes)
    {
        const bool has_incoming = std::any_of(
            workflow.edges.begin(), workflow.edges.end(),
            [&](const SupplyRouteEdge& edge) {
                return edge.to_node_id == node.node_id;
            });
        if(!has_incoming) return &node;
    }
    return nullptr;
}

const SupplyRouteNode* workflow_successor(const RuntimeWorkflow& workflow,
                                          const std::string& node_id)
{
    for(const SupplyRouteEdge& edge : workflow.edges)
    {
        if(edge.from_node_id != node_id) continue;
        return workflow_node(workflow, edge.to_node_id);
    }
    return nullptr;
}

bool ordered_workflow_nodes(const RuntimeWorkflow& workflow,
                            std::vector<const SupplyRouteNode*>& ordered,
                            std::string& error)
{
    ordered.clear();
    error.clear();
    const SupplyRouteNode* current = first_workflow_node(workflow);
    std::unordered_set<std::string> visited;
    while(current)
    {
        if(!visited.insert(current->node_id).second)
        {
            error = "The saved route contains a cycle";
            return false;
        }
        ordered.push_back(current);
        current = workflow_successor(workflow, current->node_id);
    }
    if(ordered.size() != workflow.nodes.size())
    {
        error = "The saved route does not contain one connected linear path";
        return false;
    }
    return !ordered.empty();
}

std::string numbered_route_identifier(
    const RuntimeWorkflow& workflow,
    const SupplyRouteNode& target,
    const std::string& role,
    const std::string& prefix)
{
    if(target.role != role) return "";

    std::vector<const SupplyRouteNode*> ordered;
    std::string error;
    if(!ordered_workflow_nodes(workflow, ordered, error)) return "";

    int transport_sequence = 0;
    bool target_found = false;
    for(const SupplyRouteNode* node : ordered)
    {
        if(node->role == role) ++transport_sequence;
        if(node->node_id == target.node_id)
        {
            target_found = true;
            break;
        }
    }
    if(!target_found || transport_sequence <= 0 || transport_sequence > 9999)
        return "";

    std::ostringstream identifier;
    identifier << prefix << std::setw(4) << std::setfill('0')
                << transport_sequence;
    return identifier.str();
}

std::string default_transport_shipment_id(
    const RuntimeWorkflow& workflow,
    const SupplyRouteNode& target)
{
    return numbered_route_identifier(workflow, target, "logistics", "SHIP-");
}

std::string default_transport_vehicle_id(
    const RuntimeWorkflow& workflow,
    const SupplyRouteNode& target)
{
    return numbered_route_identifier(workflow, target, "logistics", "VEHICLE-");
}

std::string default_storage_lot_id(
    const RuntimeWorkflow& workflow,
    const SupplyRouteNode& target)
{
    return numbered_route_identifier(workflow, target, "warehouse", "STORAGE-");
}

std::string default_storage_zone_id(
    const RuntimeWorkflow& workflow,
    const SupplyRouteNode& target)
{
    return numbered_route_identifier(workflow, target, "warehouse", "ZONE-");
}

bool local_datetime_range_is_valid(const std::string& start,
                                   const std::string& end)
{
    if(start.empty() || end.empty()) return true;
    return start <= end;
}

struct WorkflowProgress
{
    const SupplyRouteNode* next_node = nullptr;
    const SupplyChainRecord* parent_record = nullptr;
    bool complete = false;
    std::string error;
};

WorkflowProgress evaluate_workflow_progress(
    const RuntimeWorkflow& workflow,
    const std::vector<SupplyChainRecord>& records,
    const std::string& batch_id)
{
    WorkflowProgress progress;
    std::vector<const SupplyRouteNode*> ordered;
    if(!ordered_workflow_nodes(workflow, ordered, progress.error))
        return progress;

    std::unordered_map<std::string, const SupplyChainRecord*> records_by_node;
    std::unordered_map<int, const SupplyChainRecord*> records_by_block;
    for(const SupplyChainRecord& record : records)
    {
        if(record.batch_id != batch_id) continue;
        if(!record.verified || !record.signature_verified) continue;

        records_by_block[record.block_id] = &record;

        const auto* node = workflow_node_for_record(workflow, record);
        // A Block for a deleted or reassigned node remains historical data. It
        // does not participate in the current route progress calculation.
        if(!node) continue;
        if(records_by_node.find(node->node_id) != records_by_node.end())
        {
            progress.error = "Route node " + node->label +
                " has more than one committed block";
            return progress;
        }
        records_by_node[node->node_id] = &record;
    }

    const SupplyChainRecord* previous_record = nullptr;
    const SupplyChainRecord* latest_record = nullptr;
    for(const auto& item : records_by_block)
    {
        if(!latest_record || item.second->block_id > latest_record->block_id)
            latest_record = item.second;
    }

    for(std::size_t index = 0; index < ordered.size(); ++index)
    {
        const SupplyRouteNode* node = ordered[index];
        const auto current = records_by_node.find(node->node_id);
        if(current == records_by_node.end())
        {
            if(index > 0 && !previous_record)
            {
                progress.error = "Route stage " + node->label +
                    " has no committed direct predecessor";
                return progress;
            }
            progress.next_node = node;
            // New stages are appended to the immutable block chain even when
            // the saved route inserts them before an existing later stage.
            progress.parent_record = latest_record
                ? latest_record
                : previous_record;
            return progress;
        }

        const SupplyChainRecord* record = current->second;
        if(index == 0)
        {
            if(record->parent_block_id != -1 ||
               record->parent_block_hash != "GENESIS")
            {
                progress.error = "Supplier block is not the genesis block";
                return progress;
            }
        }
        else
        {
            bool parent_matches = previous_record &&
                record->parent_block_id == previous_record->block_id &&
                record->parent_block_hash == previous_record->block_hash;
            if(!parent_matches)
            {
                const auto historical_parent = records_by_block.find(
                    record->parent_block_id);
                parent_matches = historical_parent != records_by_block.end() &&
                    historical_parent->second->block_id < record->block_id &&
                    historical_parent->second->block_hash ==
                        record->parent_block_hash;
            }
            if(!parent_matches)
            {
                progress.error = "Block " + std::to_string(record->block_id) +
                    " does not link to the saved route history";
                return progress;
            }
        }
        previous_record = record;
    }

    progress.complete = true;
    return progress;
}

const SupplyRouteNode* next_pending_workflow_node(
    const RuntimeWorkflow& workflow,
    const std::vector<SupplyChainRecord>& records,
    const std::string& batch_id)
{
    return evaluate_workflow_progress(workflow, records, batch_id).next_node;
}

bool workflow_is_valid(const std::vector<SupplyRouteNode>& nodes,
                       const std::vector<SupplyRouteEdge>& edges,
                       std::string& error)
{
    error.clear();
    if(nodes.size() < 2)
    {
        error = "A route needs at least a Supplier and a Supermarket";
        return false;
    }

    std::unordered_map<std::string, const SupplyRouteNode*> by_id;
    int supplier_count = 0;
    int supermarket_count = 0;
    for(const SupplyRouteNode& node : nodes)
    {
        if(node.node_id.empty() || node.label.empty() || node.role.empty() ||
           by_id.find(node.node_id) != by_id.end())
        {
            error = "Route node IDs must be unique and non-empty";
            return false;
        }
        if(node.username.empty())
        {
            error = "Every route node must have an assigned account";
            return false;
        }
        if(node.role != "supplier" && node.role != "logistics" &&
           node.role != "warehouse" && node.role != "supermarket")
        {
            error = "Route node has an unsupported role";
            return false;
        }
        supplier_count += node.role == "supplier" ? 1 : 0;
        supermarket_count += node.role == "supermarket" ? 1 : 0;
        by_id[node.node_id] = &node;
    }
    if(supplier_count != 1 || supermarket_count != 1)
    {
        error = "A route must have exactly one Supplier and one Supermarket";
        return false;
    }

    std::unordered_map<std::string, int> incoming;
    std::unordered_map<std::string, int> outgoing;
    std::unordered_set<std::string> unique_edges;
    for(const SupplyRouteEdge& edge : edges)
    {
        if(by_id.find(edge.from_node_id) == by_id.end() ||
           by_id.find(edge.to_node_id) == by_id.end() ||
           edge.from_node_id == edge.to_node_id)
        {
            error = "Route edges must connect two different route nodes";
            return false;
        }
        const std::string edge_key = edge.from_node_id + "\x1f" + edge.to_node_id;
        if(!unique_edges.insert(edge_key).second)
        {
            error = "Route connections must be unique";
            return false;
        }
        ++outgoing[edge.from_node_id];
        ++incoming[edge.to_node_id];
    }

    const SupplyRouteNode* supplier = nullptr;
    const SupplyRouteNode* supermarket = nullptr;
    for(const SupplyRouteNode& node : nodes)
    {
        if(node.role == "supplier") supplier = &node;
        if(node.role == "supermarket") supermarket = &node;
    }
    if(!supplier || !supermarket || incoming[supplier->node_id] != 0 ||
       outgoing[supermarket->node_id] != 0)
    {
        error = "Supplier must start the route and Supermarket must end it";
        return false;
    }

    for(const SupplyRouteNode& node : nodes)
    {
        if(node.role != "supplier" && incoming[node.node_id] != 1)
        {
            error = "Every non-Supplier route node must have one incoming connection";
            return false;
        }
        if(node.role != "supermarket" && outgoing[node.node_id] != 1)
        {
            error = "Every non-Supermarket route node must have one outgoing connection";
            return false;
        }
    }

    std::unordered_set<std::string> visited;
    std::string current = supplier->node_id;
    int expected_step_index = 0;
    while(true)
    {
        if(!visited.insert(current).second)
        {
            error = "Route cannot contain a cycle";
            return false;
        }
        const auto current_node = by_id.find(current);
        if(current_node == by_id.end() ||
           current_node->second->step_index != expected_step_index)
        {
            error = "Route step indexes must follow the saved connection order";
            return false;
        }
        if(current == supermarket->node_id) break;
        std::string next;
        for(const SupplyRouteEdge& edge : edges)
        {
            if(edge.from_node_id != current) continue;
            if(!next.empty())
            {
                error = "This demo route must have one outgoing connection per node";
                return false;
            }
            next = edge.to_node_id;
        }
        if(next.empty())
        {
            error = "Every route node must lead to the Supermarket";
            return false;
        }
        current = next;
        ++expected_step_index;
    }
    if(visited.size() != nodes.size())
    {
        error = "Every route node must be connected to the same route";
        return false;
    }
    return true;
}

std::vector<MerkleField> block_merkle_fields(
    int block_id,
    const SupplyChainBatch& batch,
    const std::unordered_map<std::string, std::string>& fields,
    const UserAccount& account,
    const std::vector<IpfsReference>& references,
    const SupplyChainRecord* parent,
    const std::string& confirmation_method,
    const std::string& confirmation_name,
    const std::string& signature_public_key_hash,
    const std::string& signed_payload_hash)
{
    std::vector<MerkleField> merkle_fields;
    auto add_field = [&](const std::string& name, const std::string& value) {
        merkle_fields.push_back(MerkleField{name, value});
    };

    add_field("blockId", std::to_string(block_id));
    add_field("batchId", batch.batch_id);
    add_field("product", batch.product);
    add_field("harvestDate", batch.harvest_date);
    add_field("farmLocation", batch.farm_location);
    add_field("certificateId", batch.certificate_id);
    add_field("stage", account.role);
    if(!batch.route_id.empty())
        add_field("routeId", batch.route_id);
    if(!batch.route_node_id.empty())
        add_field("routeNodeId", batch.route_node_id);
    if(batch.route_step_index >= 0)
        add_field("routeStepIndex", std::to_string(batch.route_step_index));
    add_field("parentBlockId", parent ? std::to_string(parent->block_id) : "GENESIS");
    add_field("parentBlockHash", parent ? parent->block_hash : "GENESIS");
    add_field("confirmationMethod", confirmation_method);
    add_field("confirmationName", confirmation_name);
    add_field("signaturePublicKeyHash", signature_public_key_hash);
    add_field("signedPayloadHash", signed_payload_hash);

    for(const std::string& name : role_event_fields(account.role))
    {
        const auto item = fields.find(name);
        add_field("event." + name, item == fields.end() ? "" : item->second);
    }

    std::vector<IpfsReference> sorted_references = references;
    std::sort(sorted_references.begin(), sorted_references.end(),
              [](const IpfsReference& left, const IpfsReference& right) {
                  if(left.category != right.category)
                      return left.category < right.category;
                  return left.cid < right.cid;
              });
    for(std::size_t index = 0; index < sorted_references.size(); ++index)
    {
        const IpfsReference& reference = sorted_references[index];
        const std::string prefix = "ipfs." + std::to_string(index) + ".";
        add_field(prefix + "category", reference.category);
        add_field(prefix + "cid", reference.cid);
        add_field(prefix + "filename", reference.filename);
        add_field(prefix + "contentType", reference.content_type);
        add_field(prefix + "size", std::to_string(reference.size));
    }

    add_field("confirmedBy", account.username);
    add_field("uid", account.uid);
    add_field("role", account.role);
    add_field("organizationId", account.organization_id);
    return merkle_fields;
}

std::string json_error(const std::string& message)
{
    return "{\"error\":\"" + json_escape(message) + "\"}";
}

std::string user_json(const UserAccount& account)
{
    return "{\"uid\":\"" + json_escape(account.uid) +
           "\",\"username\":\"" + json_escape(account.username) +
           "\",\"role\":\"" + json_escape(account.role) +
           "\",\"organizationId\":\"" + json_escape(account.organization_id) +
           "\",\"displayName\":\"" +
           json_escape(effective_display_name(account)) +
           "\",\"publicKeyRegistered\":" +
           (account.public_key.empty() ? "false" : "true") +
           "}";
}

std::string confirmation_policy_json(const ConfirmationPolicy& policy)
{
    return "{\"routeId\":\"" + json_escape(policy.route_id) +
           "\",\"nodeId\":\"" + json_escape(policy.node_id) +
           "\",\"nodeLabel\":\"" + json_escape(policy.node_label) +
           "\",\"role\":\"" + json_escape(policy.role) +
           "\",\"username\":\"" + json_escape(policy.username) +
           "\",\"typedName\":" + (policy.typed_name ? "true" : "false") +
           ",\"handwritten\":" + (policy.handwritten ? "true" : "false") +
           ",\"face\":" + (policy.face ? "true" : "false") +
           ",\"updatedAt\":\"" + json_escape(policy.updated_at) + "\"}";
}

std::string confirmation_policies_json(
    const std::vector<ConfirmationPolicy>& policies)
{
    std::string json = "{\"policies\":[";
    for(std::size_t index = 0; index < policies.size(); ++index)
    {
        if(index > 0) json += ',';
        json += confirmation_policy_json(policies[index]);
    }
    return json + "]}";
}

std::string string_array_json(const std::vector<std::string>& values)
{
    std::ostringstream json;
    json << '[';
    for(std::size_t index = 0; index < values.size(); ++index)
    {
        if(index > 0) json << ',';
        json << '"' << json_escape(values[index]) << '"';
    }
    json << ']';
    return json.str();
}

std::string snapshot_evidence_json(
    const supermarket::snapshot::BatchInput& batch)
{
    std::ostringstream json;
    json << '[';
    bool first = true;
    for(const supermarket::snapshot::StageInput& stage : batch.stages)
    {
        for(const supermarket::snapshot::EvidenceInput& evidence : stage.evidence)
        {
            const auto* policy =
                supermarket::snapshot::find_evidence_policy(evidence.category);
            if(!policy || evidence.cid.empty()) continue;
            if(!first) json << ',';
            first = false;
            json << "{\"stage\":\"" << json_escape(stage.stage)
                 << "\",\"category\":\"" << json_escape(evidence.category)
                 << "\",\"type\":\"" << json_escape(policy->public_type)
                 << "\",\"label\":\"" << json_escape(policy->label)
                 << "\",\"cid\":\"" << json_escape(evidence.cid)
                 << "\",\"selectedByDefault\":"
                 << (policy->selected_by_default ? "true" : "false") << '}';
        }
    }
    json << ']';
    return json.str();
}

std::string latest_snapshot_block_hash(
    const supermarket::snapshot::BatchInput& input)
{
    int latest_block_id = -1;
    std::string latest_block_hash;
    for(const auto& stage : input.stages)
    {
        if(stage.block_id > latest_block_id && !stage.block_hash.empty())
        {
            latest_block_id = stage.block_id;
            latest_block_hash = stage.block_hash;
        }
    }
    for(const auto& block : input.historical_blocks)
    {
        if(block.block_id > latest_block_id && !block.block_hash.empty())
        {
            latest_block_id = block.block_id;
            latest_block_hash = block.block_hash;
        }
    }
    return latest_block_hash;
}

std::string eligible_snapshot_batches_json(
    const std::string& database_path,
    const std::vector<SupplyChainBatch>& batches,
    const std::vector<SupplyChainRecord>& records)
{
    std::ostringstream json;
    json << "{\"batches\":[";
    bool first = true;
    for(const SupplyChainBatch& batch : batches)
    {
        const auto workflow = load_runtime_workflow(database_path, batch.batch_id);
        if(!workflow) continue;
        const supermarket::snapshot::BatchInput input =
            make_snapshot_batch_input(
                batch, records, workflow->nodes, workflow->edges);
        const auto eligibility = supermarket::snapshot::evaluate_eligibility(input);
        if(!eligibility.eligible || input.stages.empty()) continue;
        if(!first) json << ',';
        first = false;
        json << "{\"batchId\":\"" << json_escape(batch.batch_id)
             << "\",\"product\":\"" << json_escape(batch.product)
             << "\",\"status\":\"" << json_escape(input.status)
             << "\",\"finalPrivateBlockHash\":\""
             << json_escape(latest_snapshot_block_hash(input))
             << "\",\"routeFingerprint\":\""
             << json_escape(supermarket::snapshot::route_fingerprint(
                    input.route_nodes, input.route_edges))
             << "\",\"evidence\":" << snapshot_evidence_json(input) << '}';
    }
    json << "]}";
    return json.str();
}

std::string snapshot_preview_json(const supermarket::snapshot::Preview& preview)
{
    std::ostringstream json;
    json << "{\"snapshotId\":\"" << json_escape(preview.snapshot_id)
         << "\",\"snapshotVersion\":" << preview.snapshot_version
         << ",\"generatedAt\":\"" << json_escape(preview.generated_at)
         << "\",\"batchId\":\"" << json_escape(preview.batch_id)
         << "\",\"routeFingerprint\":\""
         << json_escape(preview.route_fingerprint)
         << "\",\"publicRoot\":\"" << json_escape(preview.public_root)
         << "\",\"finalPrivateBlockHash\":\""
         << json_escape(preview.final_private_block_hash)
         << "\",\"publicFieldCount\":" << preview.public_fields.size()
         << ",\"selectedEvidenceCount\":" << preview.public_evidence.size()
         << ",\"excludedFields\":"
         << string_array_json(preview.excluded_fields)
         << ",\"manifest\":"
         << json_value_or_empty_object(preview.manifest_json)
         << ",\"publicationCandidate\":"
         << json_value_or_empty_object(
                supermarket::snapshot::publication_candidate_json(preview))
         << '}';
    return json.str();
}

std::optional<supermarket::snapshot_storage::SnapshotPreview>
snapshot_storage_preview_from_candidate(const std::string& candidate_json)
{
    const auto protocol = json_string_value(candidate_json, "protocol");
    const auto snapshot_id = json_string_value(candidate_json, "snapshotId");
    const auto schema_version = json_integer_value(candidate_json, "snapshotVersion");
    const auto generated_at = json_string_value(candidate_json, "generatedAt");
    const auto batch_id = json_string_value(candidate_json, "batchId");
    const auto route_fingerprint =
        json_string_value(candidate_json, "routeFingerprint");
    const auto manifest_json =
        json_string_value(candidate_json, "manifestCanonical");
    const auto public_root = json_string_value(candidate_json, "publicRoot");
    const auto source_block_hash =
        json_string_value(candidate_json, "sourceBlockHash");
    if(!protocol || !snapshot_id || !schema_version || !generated_at ||
       !batch_id || !route_fingerprint || !manifest_json || !public_root ||
       !source_block_hash)
        return std::nullopt;

    supermarket::snapshot_storage::SnapshotPreview preview;
    preview.protocol = *protocol;
    preview.snapshot_id = *snapshot_id;
    preview.schema_version = *schema_version;
    preview.generated_at = *generated_at;
    preview.batch_id = *batch_id;
    preview.manifest_json = *manifest_json;
    preview.public_root = *public_root;
    preview.source_block_hash = *source_block_hash;
    preview.route_fingerprint = *route_fingerprint;
    preview.candidate_json = candidate_json;
    return preview;
}

std::string workflow_route_fingerprint(
    const std::vector<SupplyRouteNode>& route_nodes,
    const std::vector<SupplyRouteEdge>& route_edges)
{
    std::vector<supermarket::snapshot::RouteNodeInput> fingerprint_nodes;
    fingerprint_nodes.reserve(route_nodes.size());
    for(const SupplyRouteNode& node : route_nodes)
    {
        fingerprint_nodes.push_back(supermarket::snapshot::RouteNodeInput{
            node.node_id, node.label, node.role, node.username,
            node.step_index, node.node_type
        });
    }

    std::vector<supermarket::snapshot::RouteEdgeInput> fingerprint_edges;
    fingerprint_edges.reserve(route_edges.size());
    for(const SupplyRouteEdge& edge : route_edges)
    {
        fingerprint_edges.push_back(supermarket::snapshot::RouteEdgeInput{
            edge.from_node_id, edge.to_node_id
        });
    }
    return supermarket::snapshot::route_fingerprint(
        fingerprint_nodes, fingerprint_edges);
}

std::string public_route_state_json(const std::string& database_path,
                                    const std::string& batch_id)
{
    std::string route_id;
    std::vector<SupplyRouteNode> route_nodes;
    std::vector<SupplyRouteEdge> route_edges;
    if(!load_workflow_route(database_path, batch_id, route_id,
                            route_nodes, route_edges))
        return "";

    std::sort(route_nodes.begin(), route_nodes.end(),
              [](const SupplyRouteNode& left, const SupplyRouteNode& right) {
                  if(left.step_index != right.step_index)
                      return left.step_index < right.step_index;
                  return left.node_id < right.node_id;
              });
    std::ostringstream route_shape;
    for(std::size_t index = 0; index < route_nodes.size(); ++index)
    {
        if(index > 0) route_shape << '|';
        route_shape << route_nodes[index].role;
    }

    std::ostringstream json;
    json << "{\"batchId\":\"" << json_escape(batch_id)
         << "\",\"routeId\":\"" << json_escape(route_id)
         << "\",\"routeFingerprint\":\""
         << json_escape(workflow_route_fingerprint(route_nodes, route_edges))
         << "\",\"routeShape\":\"" << json_escape(route_shape.str())
         << "\"}";
    return json.str();
}

bool parse_snapshot_evidence_selection(
    const std::string& encoded,
    std::vector<supermarket::snapshot::EvidenceInput>& selections,
    std::string& error)
{
    selections.clear();
    if(encoded.empty()) return true;
    if(encoded.size() > 128 * 1024)
    {
        error = "Selected evidence list is too large";
        return false;
    }

    for(const std::string& token : split_text(encoded, ','))
    {
        const auto pieces = split_text(token, '|');
        if(pieces.size() != 3 || trim(pieces[0]).empty() ||
           trim(pieces[1]).empty() || trim(pieces[2]).empty())
        {
            error = "Malformed public evidence selection";
            return false;
        }
        selections.push_back(supermarket::snapshot::EvidenceInput{
            trim(pieces[0]), trim(pieces[1]), trim(pieces[2])
        });
    }
    return true;
}

bool snapshot_evidence_selection_from_manifest(
    const std::string& manifest_json,
    std::vector<supermarket::snapshot::EvidenceInput>& selections,
    std::string& error)
{
    selections.clear();
    error.clear();
    const std::string key = "\"public_evidence\"";
    const std::size_t key_position = manifest_json.find(key);
    if(key_position == std::string::npos) return true;
    const std::size_t array_start = manifest_json.find('[', key_position + key.size());
    if(array_start == std::string::npos)
    {
        error = "Published Snapshot evidence list is malformed";
        return false;
    }

    std::size_t cursor = array_start + 1;
    while(cursor < manifest_json.size())
    {
        while(cursor < manifest_json.size() &&
              std::isspace(static_cast<unsigned char>(manifest_json[cursor])))
            ++cursor;
        if(cursor >= manifest_json.size()) break;
        if(manifest_json[cursor] == ']') return true;
        if(manifest_json[cursor] != '{')
        {
            error = "Published Snapshot evidence list is malformed";
            return false;
        }

        const std::size_t object_start = cursor;
        int depth = 0;
        bool in_string = false;
        bool escaped = false;
        bool closed = false;
        for(; cursor < manifest_json.size(); ++cursor)
        {
            const char character = manifest_json[cursor];
            if(in_string)
            {
                if(escaped)
                {
                    escaped = false;
                    continue;
                }
                if(character == '\\')
                {
                    escaped = true;
                    continue;
                }
                if(character == '"') in_string = false;
                continue;
            }
            if(character == '"')
            {
                in_string = true;
                continue;
            }
            if(character == '{') ++depth;
            if(character == '}' && --depth == 0)
            {
                ++cursor;
                closed = true;
                break;
            }
        }
        if(!closed)
        {
            error = "Published Snapshot evidence object is malformed";
            return false;
        }

        const std::string object = manifest_json.substr(
            object_start, cursor - object_start);
        const auto stage = json_string_value(object, "stage");
        const auto type = json_string_value(object, "type");
        const auto cid = json_string_value(object, "cid");
        if(!stage || !type || !cid || stage->empty() || type->empty() ||
           cid->empty())
        {
            error = "Published Snapshot evidence object is incomplete";
            return false;
        }

        const auto policy = std::find_if(
            supermarket::snapshot::public_evidence_policy().begin(),
            supermarket::snapshot::public_evidence_policy().end(),
            [&](const supermarket::snapshot::EvidencePolicy& item) {
                return item.public_type == *type;
            });
        if(policy == supermarket::snapshot::public_evidence_policy().end())
        {
            error = "Published Snapshot evidence type is unsupported";
            return false;
        }
        selections.push_back(supermarket::snapshot::EvidenceInput{
            *stage, policy->category, *cid
        });

        while(cursor < manifest_json.size() &&
              std::isspace(static_cast<unsigned char>(manifest_json[cursor])))
            ++cursor;
        if(cursor >= manifest_json.size()) break;
        if(manifest_json[cursor] == ',')
        {
            ++cursor;
            continue;
        }
        if(manifest_json[cursor] == ']') return true;
        error = "Published Snapshot evidence list is malformed";
        return false;
    }
    error = "Published Snapshot evidence list is malformed";
    return false;
}

std::string snapshot_check_error_message(
    const supermarket::snapshot::Eligibility& eligibility)
{
    if(eligibility.errors.empty())
        return "The current route is not ready for publication";
    std::ostringstream message;
    for(std::size_t index = 0; index < eligibility.errors.size(); ++index)
    {
        if(index > 0) message << "; ";
        message << eligibility.errors[index];
    }
    return message.str();
}

std::vector<supermarket::snapshot::EvidenceInput>
snapshot_available_evidence_selection(
    const supermarket::snapshot::BatchInput& input,
    const std::vector<supermarket::snapshot::EvidenceInput>& previous)
{
    std::vector<supermarket::snapshot::EvidenceInput> available;
    for(const auto& selection : previous)
    {
        const bool still_available = std::any_of(
            input.stages.begin(), input.stages.end(),
            [&](const supermarket::snapshot::StageInput& stage) {
                if(stage.stage != selection.stage) return false;
                return std::any_of(
                    stage.evidence.begin(), stage.evidence.end(),
                    [&](const supermarket::snapshot::EvidenceInput& evidence) {
                        return evidence.category == selection.category &&
                            evidence.cid == selection.cid;
                    });
            });
        if(still_available) available.push_back(selection);
    }
    return available;
}

std::string snapshot_status_json(
    const supermarket::snapshot_storage::SnapshotStore& snapshot_store,
    const std::string& batch_id)
{
    const auto active = snapshot_store.active_snapshot(batch_id);
    if(!active) return "";
    const auto verification = snapshot_store.verification_status(batch_id);
    std::ostringstream json;
    json << "{\"batchId\":\"" << json_escape(batch_id)
         << "\",\"snapshotId\":\"" << json_escape(active->snapshot_id)
         << "\",\"revision\":" << active->revision
         << ",\"routeFingerprint\":\""
         << json_escape(active->route_fingerprint)
         << "\",\"publishedAt\":\""
         << json_escape(active->published_at)
         << "\",\"latestVerificationAt\":\""
         << json_escape(verification ? verification->checked_at : "")
         << "\",\"verificationStatus\":\""
         << json_escape(verification ? verification->status : "")
         << "\",\"verificationMessage\":\""
         << json_escape(verification ? verification->message : "")
         << "\"}";
    return json.str();
}

void record_snapshot_check(
    supermarket::snapshot_storage::SnapshotStore& snapshot_store,
    const std::string& batch_id,
    const std::string& snapshot_id,
    const std::string& status,
    const std::string& message)
{
    std::string error;
    if(!snapshot_store.touch_verification(
           batch_id, snapshot_id, "", status, message, error))
    {
        std::cerr << "Snapshot verification status update failed: "
                  << error << '\n';
    }
    live_event_hub.publish("snapshot_checked", batch_id);
}

void automatic_snapshot_refresh(
    const std::string& database_path,
    supermarket::snapshot_storage::SnapshotStore& snapshot_store,
    std::mutex& chain_mutex)
{
    std::lock_guard<std::mutex> chain_lock(chain_mutex);
    std::vector<SupplyChainBatch> batches;
    std::vector<SupplyChainRecord> records;
    if(!load_supply_chain_batches(database_path, batches) ||
       !load_supply_chain_records(database_path, records))
    {
        std::cerr << "Automatic Snapshot refresh could not read source data\n";
        return;
    }

    for(const SupplyChainBatch& batch : batches)
    {
        const auto active = snapshot_store.active_snapshot(batch.batch_id);
        const auto latest = snapshot_store.latest_publication(batch.batch_id);
        if(!active && !latest) continue;
        const std::string last_snapshot_id = active
            ? active->snapshot_id
            : latest->snapshot_id;

        const auto workflow = load_runtime_workflow(database_path, batch.batch_id);
        if(!workflow)
        {
            record_snapshot_check(
                snapshot_store, batch.batch_id, last_snapshot_id,
                "error", "The published Snapshot route is unavailable");
            continue;
        }

        const auto input = make_snapshot_batch_input(
            batch, records, workflow->nodes, workflow->edges);
        const auto eligibility = supermarket::snapshot::evaluate_eligibility(input);
        if(!eligibility.eligible || input.stages.empty())
        {
            record_snapshot_check(
                snapshot_store, batch.batch_id, last_snapshot_id,
                "waiting", snapshot_check_error_message(eligibility));
            continue;
        }

        const std::string source_block_hash = latest_snapshot_block_hash(input);
        const std::string route_fingerprint = supermarket::snapshot::route_fingerprint(
            input.route_nodes, input.route_edges);
        if(active && active->source_block_hash == source_block_hash &&
           active->route_fingerprint == route_fingerprint)
        {
            record_snapshot_check(
                snapshot_store, batch.batch_id, active->snapshot_id,
                "unchanged", "Published Snapshot is still current");
            continue;
        }

        std::vector<supermarket::snapshot::EvidenceInput> selected_evidence;
        std::string evidence_error;
        const auto previous = active ? active : latest;
        if(!snapshot_evidence_selection_from_manifest(
               previous->manifest_json, selected_evidence, evidence_error))
        {
            record_snapshot_check(
                snapshot_store, batch.batch_id, last_snapshot_id,
                "error", evidence_error);
            continue;
        }
        selected_evidence = snapshot_available_evidence_selection(
            input, selected_evidence);

        std::string preview_error;
        const auto preview = supermarket::snapshot::build_preview(
            input, selected_evidence, preview_error);
        if(!preview)
        {
            record_snapshot_check(
                snapshot_store, batch.batch_id, last_snapshot_id,
                "error", preview_error);
            continue;
        }
        const std::string candidate_json =
            supermarket::snapshot::publication_candidate_json(*preview);
        const auto storage_preview =
            snapshot_storage_preview_from_candidate(candidate_json);
        if(!storage_preview)
        {
            record_snapshot_check(
                snapshot_store, batch.batch_id, last_snapshot_id,
                "error", "Automatic Snapshot candidate metadata is incomplete");
            continue;
        }

        const auto published = post_publication_candidate(candidate_json);
        if(!published)
        {
            record_snapshot_check(
                snapshot_store, batch.batch_id, last_snapshot_id,
                "error", "The local public-chain service is unavailable");
            continue;
        }
        if(published->status_code < 200 || published->status_code >= 300)
        {
            const std::string message = published->body.empty()
                ? "The public-chain service rejected the automatic Snapshot"
                : published->body.substr(0, 512);
            record_snapshot_check(
                snapshot_store, batch.batch_id, last_snapshot_id,
                "error", message);
            continue;
        }

        const auto transaction_hash = json_string_value(
            published->body, "transactionHash");
        std::string storage_error;
        if(!snapshot_store.mark_published(
               *storage_preview, published->body,
               transaction_hash.value_or(""), storage_error))
        {
            record_snapshot_check(
                snapshot_store, batch.batch_id, storage_preview->snapshot_id,
                "error", "Snapshot was published but local indexing failed: " +
                    storage_error);
            continue;
        }
        record_snapshot_check(
            snapshot_store, batch.batch_id, storage_preview->snapshot_id,
            "published", "A new Snapshot was published from changed source data");
        live_event_hub.publish("snapshot_published", batch.batch_id);
    }
}

std::string ipfs_refs_json(const std::vector<IpfsReference>& references)
{
    std::ostringstream json;
    json << '[';
    for(std::size_t index = 0; index < references.size(); ++index)
    {
        if(index > 0) json << ',';
        const IpfsReference& reference = references[index];
        json << "{\"category\":\"" << json_escape(reference.category)
             << "\",\"cid\":\"" << json_escape(reference.cid)
             << "\",\"filename\":\"" << json_escape(reference.filename)
             << "\",\"contentType\":\""
             << json_escape(reference.content_type)
             << "\",\"size\":" << reference.size << '}';
    }
    json << ']';
    return json.str();
}

std::string merkle_leaves_json(const std::vector<MerkleLeafRecord>& leaves)
{
    std::ostringstream json;
    json << '[';
    for(std::size_t index = 0; index < leaves.size(); ++index)
    {
        if(index > 0) json << ',';
        const MerkleLeafRecord& leaf = leaves[index];
        json << "{\"index\":" << leaf.leaf_index
             << ",\"fieldName\":\"" << json_escape(leaf.field_name)
             << "\",\"value\":\"" << json_escape(leaf.leaf_value)
             << "\",\"leafHash\":\"" << json_escape(leaf.leaf_hash)
             << "\",\"proof\":\"" << json_escape(leaf.proof)
             << "\",\"verified\":" << (leaf.verified ? "true" : "false")
             << '}';
    }
    json << ']';
    return json.str();
}

struct MerkleDisplayNode
{
    std::string kind;
    std::string hash;
    int leaf_index = -1;
    std::string field_name;
    std::string leaf_value;
    std::string proof;
    bool verified = false;
    std::vector<MerkleDisplayNode> children;
};

std::optional<MerkleDisplayNode> build_merkle_display_tree(
    const std::vector<MerkleLeafRecord>& leaves)
{
    if(leaves.empty()) return std::nullopt;

    std::vector<MerkleDisplayNode> current;
    current.reserve(leaves.size());
    for(const MerkleLeafRecord& leaf : leaves)
    {
        MerkleDisplayNode node;
        node.kind = "leaf";
        node.hash = leaf.leaf_hash;
        node.leaf_index = leaf.leaf_index;
        node.field_name = leaf.field_name;
        node.leaf_value = leaf.leaf_value;
        node.proof = leaf.proof;
        node.verified = leaf.verified;
        current.push_back(std::move(node));
    }

    while(current.size() > 1)
    {
        if(current.size() % 2 != 0)
        {
            MerkleDisplayNode duplicate;
            duplicate.kind = "duplicate";
            duplicate.hash = current.back().hash;
            duplicate.verified = current.back().verified;
            current.push_back(std::move(duplicate));
        }

        std::vector<MerkleDisplayNode> parents;
        parents.reserve(current.size() / 2);
        for(std::size_t index = 0; index < current.size(); index += 2)
        {
            MerkleDisplayNode parent;
            parent.kind = "internal";
            parent.hash = sha256_value(current[index].hash + current[index + 1].hash);
            parent.verified = current[index].verified && current[index + 1].verified;
            parent.children.push_back(std::move(current[index]));
            parent.children.push_back(std::move(current[index + 1]));
            parents.push_back(std::move(parent));
        }
        current = std::move(parents);
    }

    return std::move(current.front());
}

std::string merkle_display_node_json(const MerkleDisplayNode& node,
                                     const std::string& node_id,
                                     bool is_root)
{
    std::ostringstream json;
    json << "{\"nodeId\":\"" << json_escape(node_id)
         << "\",\"kind\":\"" << (is_root ? "root" : node.kind)
         << "\",\"hash\":\"" << json_escape(node.hash)
         << "\",\"verified\":" << (node.verified ? "true" : "false")
         << ",\"leafIndex\":" << node.leaf_index
         << ",\"fieldName\":\"" << json_escape(node.field_name)
         << "\",\"value\":\"" << json_escape(node.leaf_value)
         << "\",\"proof\":\"" << json_escape(node.proof)
         << "\",\"children\":[";

    for(std::size_t index = 0; index < node.children.size(); ++index)
    {
        if(index > 0) json << ',';
        const char* side = index == 0 ? "left" : "right";
        json << merkle_display_node_json(
            node.children[index], node_id + "." + side, false);
    }
    json << "]}";
    return json.str();
}

std::string merkle_tree_json(const SupplyChainRecord& record)
{
    const auto tree = build_merkle_display_tree(record.merkle_leaves);
    if(!tree)
    {
        return "{\"root\":null,\"leafCount\":0,\"calculatedRootHash\":\"\","
               "\"storedRootHash\":\"" + json_escape(record.root_hash) +
               "\",\"consistent\":false,\"verified\":false}";
    }

    const bool consistent = tree->hash == record.root_hash;
    std::ostringstream json;
    json << "{\"root\":"
         << merkle_display_node_json(*tree, "root", true)
         << ",\"leafCount\":" << record.merkle_leaves.size()
         << ",\"calculatedRootHash\":\"" << json_escape(tree->hash)
         << "\",\"storedRootHash\":\"" << json_escape(record.root_hash)
         << "\",\"consistent\":" << (consistent ? "true" : "false")
         << ",\"verified\":"
         << (consistent && record.verified ? "true" : "false") << '}';
    return json.str();
}

std::string records_json(const std::string& database_path,
                         const std::vector<SupplyChainRecord>& records)
{
    std::ostringstream json;
    json << '[';
    for(std::size_t i = 0; i < records.size(); ++i)
    {
        if(i > 0) json << ',';
        const SupplyChainRecord& record = records[i];
        std::string route_id = record.route_id;
        std::string route_node_id = record.route_node_id;
        int route_step_index = record.route_step_index;
        std::string route_node_label;
        std::string route_node_role;
        std::string route_node_username;
        if(!route_id.empty() && !route_node_id.empty())
        {
            std::vector<SupplyRouteNode> historical_nodes;
            std::vector<SupplyRouteEdge> historical_edges;
            if(load_workflow_route_by_id(database_path, route_id,
                                         historical_nodes, historical_edges))
            {
                for(const SupplyRouteNode& node : historical_nodes)
                {
                    if(node.node_id != route_node_id) continue;
                    route_node_label = node.label;
                    route_node_role = node.role;
                    route_node_username = node.username;
                    break;
                }
            }
        }
        else
        {
            const auto historical_workflow =
                load_runtime_workflow(database_path, record.batch_id);
            if(historical_workflow)
            {
                const auto* node = workflow_node_for_record(
                    *historical_workflow, record);
                if(node)
                {
                    route_id = historical_workflow->route_id;
                    route_node_id = node->node_id;
                    route_step_index = node->step_index;
                    route_node_label = node->label;
                    route_node_role = node->role;
                    route_node_username = node->username;
                }
            }
        }
        json << "{\"blockID\":" << record.block_id
             << ",\"batchId\":\"" << json_escape(record.batch_id)
             << "\",\"product\":\"" << json_escape(record.product)
             << "\",\"locationSummary\":\""
             << json_escape(record.location_summary)
             << "\",\"batchHarvestDate\":\""
             << json_escape(record.batch_harvest_date)
             << "\",\"batchFarmLocation\":\""
             << json_escape(record.batch_farm_location)
             << "\",\"certificateId\":\""
             << json_escape(record.certificate_id)
             << "\",\"stage\":\"" << json_escape(record.stage)
             << "\",\"confirmedBy\":\"" << json_escape(record.confirmed_by)
             << "\",\"uid\":\"" << json_escape(record.uid)
             << "\",\"role\":\"" << json_escape(record.role)
             << "\",\"organizationId\":\""
             << json_escape(record.organization_id)
             << "\",\"eventData\":"
             << json_value_or_empty_object(record.event_data)
             << ",\"ipfsRefs\":" << ipfs_refs_json(record.ipfs_refs)
             << ",\"parentBlockId\":" << record.parent_block_id
             << ",\"parentBlockHash\":\""
             << json_escape(record.parent_block_hash)
             << "\",\"rootHash\":\"" << json_escape(record.root_hash)
             << "\",\"verified\":" << (record.verified ? "true" : "false")
             << ",\"merkleLeaves\":" << merkle_leaves_json(record.merkle_leaves)
             << ",\"merkleTree\":" << merkle_tree_json(record)
             << ",\"blockHash\":\"" << json_escape(record.block_hash)
             << "\",\"chainStatus\":\"" << json_escape(record.chain_status)
             << "\",\"confirmationMethod\":\""
             << json_escape(record.confirmation_method)
             << "\",\"confirmationName\":\""
             << json_escape(record.confirmation_name)
             << "\",\"signatureAlgorithm\":\""
             << json_escape(record.signature_algorithm)
             << "\",\"signature\":\""
             << json_escape(record.signature)
             << "\",\"signaturePublicKeyHash\":\""
             << json_escape(record.signature_public_key_hash)
             << "\",\"signedPayloadHash\":\""
             << json_escape(record.signed_payload_hash)
             << "\",\"signatureVerified\":"
             << (record.signature_verified ? "true" : "false")
             << ",\"routeId\":\"" << json_escape(route_id)
             << "\",\"routeNodeId\":\""
             << json_escape(route_node_id)
             << "\",\"routeStepIndex\":" << route_step_index
             << ",\"routeNodeLabel\":\""
             << json_escape(route_node_label)
             << "\",\"routeNodeRole\":\""
             << json_escape(route_node_role)
             << "\",\"routeNodeUsername\":\""
             << json_escape(route_node_username)
             << "\",\"transportShipmentId\":\""
             << json_escape(record.transport_shipment_id)
             << "\",\"transportVehicleContainerId\":\""
             << json_escape(record.transport_vehicle_container_id)
             << "\",\"createdAt\":\"" << json_escape(record.created_at) << "\"}";
    }
    json << ']';
    return json.str();
}

std::string batches_json(const std::string& database_path,
                         const std::vector<SupplyChainBatch>& batches,
                         const std::vector<SupplyChainRecord>& records)
{
    std::ostringstream json;
    json << '[';
    for(std::size_t index = 0; index < batches.size(); ++index)
    {
        if(index > 0) json << ',';
        const SupplyChainBatch& batch = batches[index];
        const auto workflow = load_runtime_workflow(database_path, batch.batch_id);
        const WorkflowProgress progress = workflow
            ? evaluate_workflow_progress(*workflow, records, batch.batch_id)
            : WorkflowProgress{};
        const SupplyRouteNode* next = progress.next_node;
        const SupplyRouteNode* destination = workflow && next
            ? workflow_successor(*workflow, next->node_id)
            : nullptr;
        const std::string next_shipment_id = workflow && next
            ? default_transport_shipment_id(*workflow, *next)
            : "";
        const std::string next_vehicle_container_id = workflow && next
            ? default_transport_vehicle_id(*workflow, *next)
            : "";
        const std::string next_storage_lot_id = workflow && next
            ? default_storage_lot_id(*workflow, *next)
            : "";
        const std::string next_storage_zone_id = workflow && next
            ? default_storage_zone_id(*workflow, *next)
            : "";
        std::unordered_set<std::string> recorded_route_nodes;
        if(workflow)
        {
            for(const SupplyChainRecord& record : records)
            {
                if(record.batch_id != batch.batch_id) continue;
                if(!record.verified || !record.signature_verified) continue;
                if(const auto* node = workflow_node_for_record(*workflow, record))
                    recorded_route_nodes.insert(node->node_id);
            }
        }
        std::vector<const SupplyRouteNode*> pending_route_nodes;
        if(workflow)
        {
            for(const SupplyRouteNode& node : workflow->nodes)
            {
                if(recorded_route_nodes.count(node.node_id) == 0)
                    pending_route_nodes.push_back(&node);
            }
        }
        const bool route_complete = workflow && progress.error.empty() &&
            progress.complete;
        std::string current_stage = batch.current_stage;
        if(workflow)
        {
            const SupplyRouteNode* latest_node = nullptr;
            for(const SupplyRouteNode& node : workflow->nodes)
            {
                if(recorded_route_nodes.count(node.node_id) == 0) continue;
                if(!latest_node || node.step_index > latest_node->step_index)
                    latest_node = &node;
            }
            if(latest_node) current_stage = latest_node->role;
        }
        const std::string effective_route_id = workflow
            ? workflow->route_id
            : batch.route_id;
        const std::string effective_status = workflow
            ? (!progress.error.empty()
                   ? "route_error"
                   : (route_complete ? "completed" : "in_progress"))
            : batch.status;
        json << "{\"batchId\":\"" << json_escape(batch.batch_id)
             << "\",\"product\":\"" << json_escape(batch.product)
             << "\",\"harvestDate\":\"" << json_escape(batch.harvest_date)
             << "\",\"farmLocation\":\""
             << json_escape(batch.farm_location)
             << "\",\"certificateId\":\""
             << json_escape(batch.certificate_id)
             << "\",\"currentStage\":\""
             << json_escape(current_stage)
             << "\",\"routeId\":\"" << json_escape(effective_route_id)
             << "\",\"nextStage\":\""
             << json_escape(next ? next->role : "")
             << "\",\"nextNodeId\":\""
             << json_escape(next ? next->node_id : "")
             << "\",\"nextNodeLabel\":\""
             << json_escape(next ? next->label : "")
             << "\",\"nextNodeType\":\""
             << json_escape(next ? next->node_type : "")
             << "\",\"nextNodeUsername\":\""
             << json_escape(next ? next->username : "")
             << "\",\"nextDestinationLabel\":\""
             << json_escape(destination ? destination->label : "")
             << "\",\"nextShipmentId\":\""
             << json_escape(next_shipment_id)
             << "\",\"nextVehicleContainerId\":\""
             << json_escape(next_vehicle_container_id)
             << "\",\"nextStorageLotId\":\""
             << json_escape(next_storage_lot_id)
             << "\",\"nextStorageZoneRackId\":\""
             << json_escape(next_storage_zone_id)
             << "\",\"routeReady\":"
             << (route_complete ? "true" : "false")
             << ",\"routeError\":\"" << json_escape(progress.error) << "\""
             << ",\"routeNodeCount\":"
             << (workflow ? workflow->nodes.size() : 0)
             << ",\"recordedNodeCount\":" << recorded_route_nodes.size()
             << ",\"pendingRouteNodes\":[";
        for(std::size_t pending_index = 0;
            pending_index < pending_route_nodes.size(); ++pending_index)
        {
            if(pending_index > 0) json << ',';
            const SupplyRouteNode& node = *pending_route_nodes[pending_index];
            json << "{\"id\":\"" << json_escape(node.node_id)
                 << "\",\"label\":\"" << json_escape(node.label)
                 << "\",\"role\":\"" << json_escape(node.role)
                 << "\",\"username\":\"" << json_escape(node.username)
                 << "\"}";
        }
        json << "]"
             << ",\"status\":\"" << json_escape(effective_status)
             << "\",\"createdAt\":\"" << json_escape(batch.created_at)
             << "\"}";
    }
    json << ']';
    return json.str();
}

std::string edges_json(const std::vector<BlockEdge>& edges)
{
    std::ostringstream json;
    json << '[';
    for(std::size_t i = 0; i < edges.size(); ++i)
    {
        if(i > 0) json << ',';
        const BlockEdge& edge = edges[i];
        json << "{\"from\":" << edge.from_block_id
             << ",\"to\":" << edge.to_block_id
             << ",\"batchId\":\"" << json_escape(edge.batch_id)
             << "\",\"relation\":\"" << json_escape(edge.relation)
             << "\"}";
    }
    json << ']';
    return json.str();
}

std::string chain_json(const std::string& database_path,
                       const std::vector<SupplyChainRecord>& records,
                       const std::vector<BlockEdge>& edges)
{
    return "{\"nodes\":" + records_json(database_path, records) +
           ",\"edges\":" + edges_json(edges) + "}";
}

std::string workflow_json(const std::string& database_path,
                          const std::string& batch_id)
{
    const auto workflow = load_runtime_workflow(database_path, batch_id);
    if(!workflow)
        return "{\"nodes\":[],\"edges\":[],\"accounts\":[],\"routeId\":\"\"}";
    std::vector<UserAccount> accounts;
    load_user_accounts(database_path, "", accounts);
    std::ostringstream json;
    json << "{\"nodes\":[";
    for(std::size_t i = 0; i < workflow->nodes.size(); ++i)
    {
        if(i > 0) json << ',';
        const SupplyRouteNode& node = workflow->nodes[i];
        json << "{\"id\":\"" << json_escape(node.node_id)
             << "\",\"nodeType\":\"" << json_escape(node.node_type)
             << "\",\"label\":\"" << json_escape(node.label)
             << "\",\"role\":\"" << json_escape(node.role)
             << "\",\"username\":\"" << json_escape(node.username)
             << "\",\"x\":" << node.position_x
             << ",\"y\":" << node.position_y
             << ",\"stepIndex\":" << node.step_index << "}";
    }
    json << "],\"edges\":[";
    for(std::size_t i = 0; i < workflow->edges.size(); ++i)
    {
        if(i > 0) json << ',';
        const SupplyRouteEdge& edge = workflow->edges[i];
        json << "{\"from\":\"" << json_escape(edge.from_node_id)
             << "\",\"to\":\"" << json_escape(edge.to_node_id)
             << "\"}";
    }
    json << "],\"accounts\":[";
    for(std::size_t i = 0; i < accounts.size(); ++i)
    {
        if(i > 0) json << ',';
        const UserAccount& account = accounts[i];
        json << "{\"uid\":\"" << json_escape(account.uid)
             << "\",\"username\":\"" << json_escape(account.username)
             << "\",\"role\":\"" << json_escape(account.role)
             << "\",\"organizationId\":\""
             << json_escape(account.organization_id)
             << "\",\"displayName\":\""
             << json_escape(account.display_name)
             << "\",\"active\":" << (account.active ? "true" : "false")
             << "}";
    }
    json << "],\"routeId\":\"" << json_escape(workflow->route_id) << "\"}";
    return json.str();
}

std::string calculate_block_hash(const SupplyChainRecord& record)
{
    std::ostringstream input;
    input << "blockId:" << record.block_id << '\n'
          << "parentBlockId:" << record.parent_block_id << '\n'
          << "parentHash:" << record.parent_block_hash << '\n'
          << "merkleRoot:" << record.root_hash << '\n'
          << "signatureAlgorithm:" << record.signature_algorithm << '\n'
          << "signature:" << record.signature << '\n'
          << "signaturePublicKeyHash:"
          << record.signature_public_key_hash << '\n'
          << "signedPayloadHash:" << record.signed_payload_hash << '\n'
          << "canonical:" << record.canonical_record;
    return sha256_value(input.str());
}

bool same_merkle_leaf(const MerkleLeafRecord& left,
                      const MerkleLeafRecord& right)
{
    return left.leaf_index == right.leaf_index &&
           left.field_name == right.field_name &&
           left.leaf_value == right.leaf_value &&
           left.leaf_hash == right.leaf_hash &&
           left.proof == right.proof &&
           left.verified == right.verified;
}

bool validate_stored_chain(const std::vector<SupplyChainRecord>& records,
                           const std::vector<BlockEdge>& edges,
                           std::string& error)
{
    std::unordered_map<int, const SupplyChainRecord*> by_block_id;
    std::unordered_map<std::string, const SupplyChainRecord*> latest_by_batch;

    for(std::size_t index = 0; index < records.size(); ++index)
    {
        const SupplyChainRecord& record = records[index];
        if(record.block_id != static_cast<int>(index))
        {
            error = "Stored block IDs are not sequential";
            return false;
        }

        const auto merkle = build_block_merkle(record.merkle_fields);
        if(!merkle || merkle->canonical_record != record.canonical_record ||
           merkle->root_hash != record.root_hash ||
           merkle->verified != record.verified ||
           merkle->leaves.size() != record.merkle_leaves.size())
        {
            error = "Merkle data mismatch at block " + std::to_string(record.block_id);
            return false;
        }
        for(std::size_t leaf_index = 0; leaf_index < merkle->leaves.size(); ++leaf_index)
        {
            if(!same_merkle_leaf(merkle->leaves[leaf_index],
                                 record.merkle_leaves[leaf_index]))
            {
                error = "Merkle leaf mismatch at block " +
                    std::to_string(record.block_id);
                return false;
            }
        }

        const auto previous = latest_by_batch.find(record.batch_id);
        const int expected_parent_id = previous == latest_by_batch.end()
            ? -1
            : previous->second->block_id;
        const std::string expected_parent_hash = previous == latest_by_batch.end()
            ? "GENESIS"
            : previous->second->block_hash;
        if(record.parent_block_id != expected_parent_id ||
           record.parent_block_hash != expected_parent_hash)
        {
            error = "Parent link mismatch at block " +
                std::to_string(record.block_id);
            return false;
        }

        if(record.parent_block_id >= 0)
        {
            const auto parent = by_block_id.find(record.parent_block_id);
            if(parent == by_block_id.end() ||
               parent->second->batch_id != record.batch_id)
            {
                error = "Invalid parent block at block " +
                    std::to_string(record.block_id);
                return false;
            }
        }

        if(calculate_block_hash(record) != record.block_hash)
        {
            error = "Block hash mismatch at block " +
                std::to_string(record.block_id);
            return false;
        }

        by_block_id[record.block_id] = &record;
        latest_by_batch[record.batch_id] = &record;
    }

    for(const SupplyChainRecord& record : records)
    {
        int matching_edges = 0;
        for(const BlockEdge& edge : edges)
        {
            if(edge.to_block_id != record.block_id) continue;
            ++matching_edges;
            if(record.parent_block_id < 0 ||
               edge.from_block_id != record.parent_block_id ||
               edge.batch_id != record.batch_id ||
               edge.relation != "continues")
            {
                error = "Block edge mismatch at block " +
                    std::to_string(record.block_id);
                return false;
            }
        }

        const int expected_edges = record.parent_block_id < 0 ? 0 : 1;
        if(matching_edges != expected_edges)
        {
            error = "Missing or duplicate block edge at block " +
                std::to_string(record.block_id);
            return false;
        }
    }

    for(const BlockEdge& edge : edges)
    {
        if(edge.from_block_id < 0 || edge.to_block_id < 0 ||
           edge.from_block_id >= static_cast<int>(records.size()) ||
           edge.to_block_id >= static_cast<int>(records.size()) ||
           edge.from_block_id >= edge.to_block_id ||
           records[static_cast<std::size_t>(edge.from_block_id)].batch_id !=
               edge.batch_id ||
           records[static_cast<std::size_t>(edge.to_block_id)].batch_id !=
               edge.batch_id)
        {
            error = "Invalid block edge table entry";
            return false;
        }
    }
    return true;
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

UserAccount make_demo_account(const std::string& uid,
                              const std::string& username,
                              const std::string& password,
                              const std::string& role,
                              const std::string& organization_id)
{
    UserAccount account;
    account.uid = uid;
    account.username = username;
    account.password_salt = generate_random_hex(16);
    account.password_hash = hash_password(password, account.password_salt);
    account.role = role;
    account.organization_id = organization_id;
    account.display_name = username;
    account.active = true;
    return account;
}
}

int main(int argc, char* argv[])
{
    std::signal(SIGPIPE, SIG_IGN);

    int port = 8081;
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
        : fs::path(CONTROL_DEFAULT_STATIC_DIR);
    const fs::path static_root = fs::weakly_canonical(fs::absolute(requested_static));
    if(!fs::is_directory(static_root))
    {
        std::cerr << "Static directory not found: " << static_root << '\n';
        return 1;
    }

    const fs::path requested_database = argc >= 4
        ? fs::path(argv[3])
        : fs::path(CONTROL_DEFAULT_DATABASE_PATH);
    const fs::path database_path = fs::absolute(requested_database).lexically_normal();
    if(!init_database(database_path.string()))
        return 1;

    const fs::path snapshot_archive_root =
        database_path.parent_path() / "snapshot-archive";
    supermarket::snapshot_storage::SnapshotStore snapshot_store(
        database_path.string(), snapshot_archive_root.string());
    std::string snapshot_storage_error;
    if(!snapshot_store.initialize(snapshot_storage_error))
    {
        std::cerr << "Snapshot storage initialization failed: "
                  << snapshot_storage_error << '\n';
        return 1;
    }

    if(!delete_expired_auth_sessions(database_path.string(), unix_time_now()))
        return 1;

    const std::vector<UserAccount> demo_accounts = {
        make_demo_account("uid-supplier-001", "supplier01", "supplier123",
                          "supplier", "supplier-demo"),
        make_demo_account("uid-logistics-001", "logistics01", "logistics123",
                          "logistics", "logistics-demo"),
        make_demo_account("uid-logistics-002", "logistics02", "logistics123",
                          "logistics", "logistics-demo"),
        make_demo_account("uid-logistics-003", "logistics03", "logistics123",
                          "logistics", "logistics-demo"),
        make_demo_account("uid-warehouse-001", "warehouse01", "warehouse123",
                          "warehouse", "warehouse-demo"),
        make_demo_account("uid-warehouse-002", "warehouse02", "warehouse123",
                          "warehouse", "warehouse-demo"),
        make_demo_account("uid-warehouse-003", "warehouse03", "warehouse123",
                          "warehouse", "warehouse-demo"),
        make_demo_account("uid-supermarket-001", "supermarket01", "supermarket123",
                          "supermarket", "supermarket-demo"),
        make_demo_account("uid-admin-001", "admin01", "admin123",
                          "admin", "control-demo")
    };
    for(const UserAccount& account : demo_accounts)
    {
        if(!insert_user_account(database_path.string(), account))
            return 1;
    }

    std::vector<SupplyChainRecord> stored_records;
    if(!load_supply_chain_records(database_path.string(), stored_records))
        return 1;

    std::vector<BlockEdge> chain_edges;
    if(!load_block_edges(database_path.string(), chain_edges))
        return 1;

    std::string chain_validation_error;
    if(!validate_stored_chain(stored_records, chain_edges, chain_validation_error))
    {
        std::cerr << "Stored chain validation failed: "
                  << chain_validation_error << '\n';
        return 1;
    }

    SessionStore sessions;
    std::mutex sessions_mutex;
    ConfirmationChallengeStore confirmation_challenges;
    std::mutex confirmation_challenges_mutex;
    std::mutex chain_mutex;

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

    // The control, user, and customer pages each keep one live event stream.
    // Reserve enough workers so those long-lived connections cannot starve
    // normal route, record, and snapshot requests on low-core machines.
    ThreadPool thread_pool(8);
    const auto snapshot_refresh_interval =
        supermarket::snapshot_scheduler::interval_from_environment();
    supermarket::snapshot_scheduler::SnapshotScheduler snapshot_scheduler(
        snapshot_refresh_interval,
        [&]() {
            automatic_snapshot_refresh(
                database_path.string(), snapshot_store, chain_mutex);
        });
    snapshot_scheduler.start();
    std::cout << "Control server: http://127.0.0.1:" << port << '\n'
             << "Static root: " << static_root << '\n'
             << "Database: " << database_path << '\n'
             << "Restored blocks: " << stored_records.size() << '\n'
             << "Worker threads: " << thread_pool.worker_count() << '\n';
    std::cout << "Automatic Snapshot refresh interval: "
              << snapshot_refresh_interval.count() << " seconds\n";

    while(true)
    {
        const int client_fd = accept(server_fd, nullptr, nullptr);
        if(client_fd == -1)
        {
            perror("accept");
            continue;
        }

        if(!thread_pool.submit([&, client_fd]() {
        try
        {
        HttpRequest request;
        if(!read_request(client_fd, request))
        {
            send_response(client_fd, "400 Bad Request", "text/plain; charset=utf-8",
                          "Bad Request\n", true);
            close(client_fd);
            return;
        }

        std::string status;
        std::string body;
        std::string content_type = "text/plain; charset=utf-8";
        const bool is_head = request.method == "HEAD";

        const bool is_api_target =
            request.target == "/api/auth/login" ||
            request.target == "/api/auth/logout" ||
            request.target == "/api/auth/me" ||
            request.target == "/api/confirmation-policy" ||
            request_path(request.target) == "/api/confirmation-policy" ||
            request.target == "/api/confirmation/challenge" ||
            request.target == "/api/records" ||
            request.target == "/api/batches" ||
            request.target == "/api/ipfs/files" ||
            request.target == "/api/workflow" ||
            request_path(request.target) == "/api/workflow" ||
            request_path(request.target) == "/api/events" ||
            request.target == "/api/chains" ||
            request_path(request.target) == "/api/public-route-state" ||
            request_path(request.target) == "/api/snapshot/status" ||
            request.target == "/api/snapshot/eligible-batches" ||
            request.target == "/api/snapshot/preview" ||
            request.target == "/api/snapshot/publish";

        if(request.method == "OPTIONS" && is_api_target)
        {
            status = "204 No Content";
            body.clear();
            send_response(client_fd, status, content_type, body, false, CORS_HEADERS);
        }
        else if(request.method == "GET" &&
                request_path(request.target) == "/api/events")
        {
            status = "200 OK";
            stream_live_events(client_fd);
        }
        else if(request.method == "POST" && request.target == "/api/auth/login")
        {
            content_type = "application/json; charset=utf-8";
            const auto content_type_header = request.headers.find("content-type");
            const bool correct_type = content_type_header != request.headers.end() &&
                lower(content_type_header->second).find(
                    "application/x-www-form-urlencoded") == 0;
            const auto fields = correct_type ? parse_form(request.body) : std::nullopt;

            const auto username = fields
                ? fields->find("username")
                : std::unordered_map<std::string, std::string>::const_iterator{};
            const auto password = fields
                ? fields->find("password")
                : std::unordered_map<std::string, std::string>::const_iterator{};

            if(!correct_type || !fields ||
               username == fields->end() || password == fields->end() ||
               username->second.empty() || password->second.empty() ||
               username->second.size() > 128 || password->second.size() > 256)
            {
                status = "422 Unprocessable Entity";
                body = json_error("Username and password are required");
            }
            else
            {
                const auto account = find_user_account(
                    database_path.string(), username->second);
                const bool password_matches = account &&
                    account->active &&
                    secure_string_equal(
                        hash_password(password->second, account->password_salt),
                        account->password_hash);

                if(!password_matches)
                {
                    status = "401 Unauthorized";
                    body = json_error("Invalid username or password");
                }
                else
                {
                    const auto remember = fields->find("remember");
                    const bool persistent = remember != fields->end() &&
                        (remember->second == "true" ||
                         remember->second == "1" ||
                         remember->second == "on");
                    const std::string token = generate_random_hex(32);
                    if(persistent)
                    {
                        const std::int64_t expires_at = unix_time_now() +
                            std::chrono::duration_cast<std::chrono::seconds>(
                                PERSISTENT_SESSION_LIFETIME).count();
                        if(!create_persistent_auth_session(
                               database_path.string(), hash_session_token(token),
                               account->uid, expires_at))
                        {
                            status = "500 Internal Server Error";
                            body = json_error("Failed to save persistent session");
                        }
                        else
                        {
                            status = "200 OK";
                            body = "{\"token\":\"" + json_escape(token) +
                                   "\",\"user\":" + user_json(*account) +
                                   "}";
                        }
                    }
                    else
                    {
                        std::lock_guard<std::mutex> lock(sessions_mutex);
                        sessions[token] = Session{
                            *account,
                            std::chrono::steady_clock::now() +
                                TEMPORARY_SESSION_LIFETIME
                        };
                        status = "200 OK";
                        body = "{\"token\":\"" + json_escape(token) +
                               "\",\"user\":" + user_json(*account) + "}";
                    }
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "POST" && request.target == "/api/auth/logout")
        {
            content_type = "application/json; charset=utf-8";
            const auto token = authorization_token(request);
            bool memory_erased = false;
            bool persistent_erased = false;
            bool persistent_delete_succeeded = true;
            if(token)
            {
                {
                    std::lock_guard<std::mutex> lock(sessions_mutex);
                    memory_erased = sessions.erase(*token) != 0;
                }
                persistent_delete_succeeded = delete_persistent_auth_session(
                    database_path.string(), hash_session_token(*token),
                    persistent_erased);
            }
            if(!persistent_delete_succeeded)
            {
                status = "500 Internal Server Error";
                body = json_error("Failed to remove persistent session");
                send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
            }
            else if(!token || (!memory_erased && !persistent_erased))
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
                send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
            }
            else
            {
                status = "204 No Content";
                body.clear();
                send_response(client_fd, status, content_type, body, false, CORS_HEADERS);
            }
        }
        else if(request.method == "GET" && request.target == "/api/auth/me")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(
                request, database_path, sessions, sessions_mutex);
            if(!user)
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
            }
            else
            {
                status = "200 OK";
                body = user_json(*user);
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "GET" &&
                request_path(request.target) == "/api/confirmation-policy")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(
                request, database_path, sessions, sessions_mutex);
            if(!user)
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
            }
            else
            {
                const std::string batch_id = query_parameter(
                    request.target, "batchId").value_or("");
                const std::string requested_route_id = query_parameter(
                    request.target, "routeId").value_or("");
                const std::string requested_node_id = query_parameter(
                    request.target, "nodeId").value_or("");
                const std::string requested_role = query_parameter(
                    request.target, "role").value_or("");
                const std::string requested_username = query_parameter(
                    request.target, "username").value_or("");
                std::string route_id;
                std::vector<SupplyRouteNode> route_nodes;
                std::vector<SupplyRouteEdge> route_edges;
                const bool route_requested = !batch_id.empty() ||
                    !requested_route_id.empty();
                bool route_loaded = false;
                if(!batch_id.empty())
                {
                    route_loaded = load_workflow_route(
                        database_path.string(), batch_id, route_id,
                        route_nodes, route_edges);
                }
                else if(!requested_route_id.empty())
                {
                    route_id = requested_route_id;
                    route_loaded = requested_route_id == "route-default"
                        ? load_workflow_route(
                              database_path.string(), "", route_id,
                              route_nodes, route_edges)
                        : load_workflow_route_by_id(
                              database_path.string(), route_id,
                              route_nodes, route_edges);
                }

                if(route_requested && !route_loaded)
                {
                    status = "404 Not Found";
                    body = json_error("The requested route was not found");
                }
                else if(route_loaded && !ensure_route_confirmation_policies(
                            database_path.string(), route_id, route_nodes, route_edges))
                {
                    status = "500 Internal Server Error";
                    body = json_error(
                        "Failed to initialize route-node confirmation policies");
                }
                else if(route_loaded)
                {
                    if(user->role == "admin")
                    {
                        std::vector<ConfirmationPolicy> policies;
                        if(!load_route_confirmation_policies(
                               database_path.string(), route_id, policies))
                        {
                            status = "500 Internal Server Error";
                            body = json_error(
                                "Failed to load route-node confirmation policies");
                        }
                        else
                        {
                            status = "200 OK";
                            body = confirmation_policies_json(policies);
                        }
                    }
                    else
                    {
                        RuntimeWorkflow workflow{
                            route_id, route_nodes, route_edges};
                        const SupplyRouteNode* target_node = nullptr;
                        if(!batch_id.empty())
                        {
                            std::vector<SupplyChainRecord> records;
                            if(!load_supply_chain_records(
                                   database_path.string(), records))
                            {
                                status = "500 Internal Server Error";
                                body = json_error(
                                    "Failed to load records for the selected route");
                            }
                            else
                            {
                                target_node = next_pending_workflow_node(
                                    workflow, records, batch_id);
                            }
                        }
                        if(status.empty() && !target_node &&
                           !requested_node_id.empty())
                            target_node = workflow_node(
                                workflow, requested_node_id);
                        if(status.empty() && !target_node &&
                           (!requested_role.empty() ||
                            !requested_username.empty()))
                        {
                            for(const SupplyRouteNode& node : workflow.nodes)
                            {
                                if((requested_role.empty() ||
                                    node.role == requested_role) &&
                                   (requested_username.empty() ||
                                    node.username == requested_username))
                                {
                                    target_node = &node;
                                    break;
                                }
                            }
                        }
                        if(status.empty() && !target_node)
                        {
                            for(const SupplyRouteNode& node : workflow.nodes)
                            {
                                if(node.role == user->role &&
                                   node.username == user->username)
                                {
                                    target_node = &node;
                                    break;
                                }
                            }
                        }

                        if(status.empty() && !target_node)
                        {
                            status = "404 Not Found";
                            body = json_error(
                                "No route node is assigned to this account");
                        }
                        else if(status.empty() &&
                                (target_node->role != user->role ||
                                 target_node->username != user->username))
                        {
                            status = "403 Forbidden";
                            body = json_error(
                                "This route node is assigned to another account");
                        }
                        else if(status.empty())
                        {
                            ConfirmationPolicy policy;
                            if(!load_route_confirmation_policy(
                                   database_path.string(), route_id,
                                   target_node->node_id, policy))
                            {
                                status = "500 Internal Server Error";
                                body = json_error(
                                    "Failed to load the route-node confirmation policy");
                            }
                            else
                            {
                                status = "200 OK";
                                body = confirmation_policy_json(policy);
                            }
                        }
                    }
                }
                else if(user->role == "admin")
                {
                    std::vector<ConfirmationPolicy> policies;
                    if(!load_confirmation_policies(database_path.string(), policies))
                    {
                        status = "500 Internal Server Error";
                        body = json_error("Failed to load confirmation policies");
                    }
                    else
                    {
                        status = "200 OK";
                        body = confirmation_policies_json(policies);
                    }
                }
                else
                {
                    ConfirmationPolicy policy;
                    if(!load_confirmation_policy(
                           database_path.string(), user->role, policy))
                    {
                        status = "500 Internal Server Error";
                        body = json_error("Failed to load confirmation policy");
                    }
                    else
                    {
                        status = "200 OK";
                        body = confirmation_policy_json(policy);
                    }
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "POST" &&
                request_path(request.target) == "/api/confirmation-policy")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(
                request, database_path, sessions, sessions_mutex);
            if(!user)
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
            }
            else if(user->role != "admin")
            {
                status = "403 Forbidden";
                body = json_error("Admin role required");
            }
            else
            {
                const auto content_type_header = request.headers.find("content-type");
                const bool correct_type = content_type_header != request.headers.end() &&
                    lower(content_type_header->second).find(
                        "application/x-www-form-urlencoded") == 0;
                const auto fields = correct_type ? parse_form(request.body) : std::nullopt;
                if(!correct_type || !fields)
                {
                    status = "422 Unprocessable Entity";
                    body = json_error(
                        "Content-Type must be application/x-www-form-urlencoded");
                }
                else if(fields->count("policies") || fields->count("routeId"))
                {
                    const std::string batch_id = form_field_value(*fields, "batchId");
                    std::string route_id = form_field_value(*fields, "routeId");
                    std::vector<SupplyRouteNode> route_nodes;
                    std::vector<SupplyRouteEdge> route_edges;
                    bool route_loaded = false;
                    if(!batch_id.empty())
                    {
                        route_loaded = load_workflow_route(
                            database_path.string(), batch_id, route_id,
                            route_nodes, route_edges);
                    }
                    else if(!route_id.empty())
                    {
                        route_loaded = load_workflow_route_by_id(
                            database_path.string(), route_id,
                            route_nodes, route_edges);
                    }

                    std::vector<ConfirmationPolicy> policies;
                    std::unordered_set<std::string> seen_node_ids;
                    const std::unordered_set<std::string> linked_node_ids =
                        supplier_route_path_node_ids(route_nodes, route_edges);
                    std::string policy_error;
                    const std::string encoded = form_field_value(*fields, "policies");
                    if(!route_loaded)
                        policy_error = "The selected route was not found";
                    else if(encoded.empty())
                        policy_error = "At least one route-node policy is required";
                    else
                    {
                        for(const std::string& token : split_text(encoded, ';'))
                        {
                            const auto parts = split_text(token, '|');
                            if(parts.size() != 4 || parts[0].empty())
                            {
                                policy_error = "Malformed route-node policy data";
                                break;
                            }
                            if(!seen_node_ids.insert(parts[0]).second)
                            {
                                policy_error =
                                    "A route-node policy was submitted more than once";
                                break;
                            }
                            const SupplyRouteNode* node = nullptr;
                            for(const SupplyRouteNode& candidate : route_nodes)
                            {
                                if(candidate.node_id == parts[0])
                                {
                                    node = &candidate;
                                    break;
                                }
                            }
                            if(!node || linked_node_ids.count(node->node_id) == 0)
                            {
                                policy_error =
                                    "A submitted policy references an unconnected route node";
                                break;
                            }
                            ConfirmationPolicy policy;
                            policy.route_id = route_id;
                            policy.node_id = node->node_id;
                            policy.node_label = node->label;
                            policy.role = node->role;
                            policy.username = node->username;
                            policy.typed_name = parts[1] == "true" ||
                                parts[1] == "1" || parts[1] == "on";
                            policy.handwritten = parts[2] == "true" ||
                                parts[2] == "1" || parts[2] == "on";
                            policy.face = parts[3] == "true" ||
                                parts[3] == "1" || parts[3] == "on";
                            policy.updated_by_uid = user->uid;
                            if(!policy.typed_name && !policy.handwritten && !policy.face)
                            {
                                policy_error =
                                    "At least one confirmation method is required for " +
                                    node->label;
                                break;
                            }
                            policies.push_back(std::move(policy));
                        }
                        if(policy_error.empty() &&
                           seen_node_ids.size() != linked_node_ids.size())
                        {
                            policy_error =
                                "A confirmation policy is required for every connected route node";
                        }
                    }

                    if(!policy_error.empty())
                    {
                        status = "422 Unprocessable Entity";
                        body = json_error(policy_error);
                    }
                    else if(!save_route_confirmation_policies(
                                database_path.string(), route_id, policies))
                    {
                        status = "500 Internal Server Error";
                        body = json_error(
                            "Failed to save route-node confirmation policies");
                    }
                    else
                    {
                        std::vector<ConfirmationPolicy> saved;
                        if(!load_route_confirmation_policies(
                               database_path.string(), route_id, saved))
                        {
                            status = "500 Internal Server Error";
                            body = json_error(
                                "Failed to reload route-node confirmation policies");
                        }
                        else
                        {
                            status = "200 OK";
                            body = confirmation_policies_json(saved);
                        }
                    }
                }
                else
                {
                    const std::vector<std::string> roles = {
                        "supplier", "logistics", "warehouse", "supermarket"
                    };
                    std::vector<ConfirmationPolicy> policies;
                    std::string invalid_role;
                    for(const std::string& role : roles)
                    {
                        ConfirmationPolicy policy;
                        policy.role = role;
                        policy.typed_name = parse_boolean(
                            *fields, role + "TypedName");
                        policy.handwritten = parse_boolean(
                            *fields, role + "Handwritten");
                        policy.face = parse_boolean(
                            *fields, role + "Face");
                        policy.updated_by_uid = user->uid;
                        if(!policy.typed_name && !policy.handwritten && !policy.face)
                            invalid_role = role;
                        policies.push_back(std::move(policy));
                    }

                    if(!invalid_role.empty())
                    {
                        status = "422 Unprocessable Entity";
                        body = json_error(
                            "At least one confirmation method is required for " +
                            invalid_role);
                    }
                    else if(!save_confirmation_policies(
                                database_path.string(), policies))
                    {
                        status = "500 Internal Server Error";
                        body = json_error("Failed to save confirmation policies");
                    }
                    else
                    {
                        std::vector<ConfirmationPolicy> saved;
                        if(!load_confirmation_policies(
                               database_path.string(), saved))
                        {
                            status = "500 Internal Server Error";
                            body = json_error("Failed to reload confirmation policies");
                        }
                        else
                        {
                            status = "200 OK";
                            body = confirmation_policies_json(saved);
                        }
                    }
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "GET" && request.target == "/api/confirmation/challenge")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(
                request, database_path, sessions, sessions_mutex);
            if(!user)
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
            }
            else
            {
                const std::string challenge = generate_random_hex(32);
                {
                    std::lock_guard<std::mutex> lock(confirmation_challenges_mutex);
                    for(auto item = confirmation_challenges.begin();
                        item != confirmation_challenges.end();)
                    {
                        if(item->second.expires_at <= std::chrono::steady_clock::now())
                            item = confirmation_challenges.erase(item);
                        else
                            ++item;
                    }
                    confirmation_challenges[challenge] = ConfirmationChallenge{
                        user->uid,
                        std::chrono::steady_clock::now() +
                            CONFIRMATION_CHALLENGE_LIFETIME
                    };
                }
                status = "200 OK";
                body = "{\"challenge\":\"" + json_escape(challenge) +
                       "\",\"expiresInSeconds\":300}";
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "GET" && request.target == "/api/records")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(
                request, database_path, sessions, sessions_mutex);
            if(!user)
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
            }
            else if(user->role != "admin")
            {
                status = "403 Forbidden";
                body = json_error("Admin role required");
            }
            else
            {
                std::vector<SupplyChainRecord> records;
                if(load_supply_chain_records(database_path.string(), records))
                {
                    status = "200 OK";
                    body = records_json(database_path.string(), records);
                }
                else
                {
                    status = "500 Internal Server Error";
                    body = json_error("Failed to read supply-chain records");
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "GET" && request.target == "/api/batches")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(
                request, database_path, sessions, sessions_mutex);
            if(!user)
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
            }
            else
            {
                std::vector<SupplyChainBatch> batches;
                std::vector<SupplyChainRecord> records;
                if(load_supply_chain_batches(database_path.string(), batches) &&
                   load_supply_chain_records(database_path.string(), records))
                {
                    status = "200 OK";
                    body = batches_json(database_path.string(), batches, records);
                }
                else
                {
                    status = "500 Internal Server Error";
                    body = json_error("Failed to read supply-chain batches");
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "POST" && request_path(request.target) == "/api/workflow")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(
                request, database_path, sessions, sessions_mutex);
            if(!user)
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
            }
            else if(user->role != "admin")
            {
                status = "403 Forbidden";
                body = json_error("Admin role required");
            }
            else
            {
                const auto content_type_header = request.headers.find("content-type");
                const bool correct_type = content_type_header != request.headers.end() &&
                    lower(content_type_header->second).find(
                        "application/x-www-form-urlencoded") == 0;
                const auto fields = correct_type ? parse_form(request.body) : std::nullopt;
                std::vector<SupplyRouteNode> nodes;
                std::vector<SupplyRouteEdge> edges;
                std::string workflow_error;
                const std::string batch_id = fields
                    ? trim(form_field_value(*fields, "batchId"))
                    : "";
                const bool allow_incomplete = fields && parse_boolean(*fields, "draft");
                if(!correct_type || !fields)
                    workflow_error = "Workflow updates require form data";
                else if(!parse_workflow_nodes(
                            form_field_value(*fields, "nodes"), nodes, workflow_error))
                {
                }
                else if(!parse_workflow_edges(
                            form_field_value(*fields, "edges"), edges, workflow_error))
                {
                }
                else if(!allow_incomplete && !workflow_is_valid(nodes, edges, workflow_error))
                {
                }
                if(!workflow_error.empty())
                {
                    status = "422 Unprocessable Entity";
                    body = json_error(workflow_error);
                }
                else
                {
                    // Route revisions and Block commits both change the active
                    // batch workflow. Serialize them so a record validated
                    // against one revision cannot write that older revision
                    // back after the administrator has switched the route.
                    std::lock_guard<std::mutex> chain_lock(chain_mutex);
                    std::string previous_route_fingerprint;
                    if(!batch_id.empty())
                    {
                        std::string previous_route_id;
                        std::vector<SupplyRouteNode> previous_nodes;
                        std::vector<SupplyRouteEdge> previous_edges;
                        if(load_workflow_route(
                               database_path.string(), batch_id,
                               previous_route_id, previous_nodes, previous_edges))
                        {
                            previous_route_fingerprint = workflow_route_fingerprint(
                                previous_nodes, previous_edges);
                        }
                    }
                    std::string route_id;
                    if(!save_workflow_route(database_path.string(), batch_id, nodes,
                                            edges, route_id, workflow_error,
                                            allow_incomplete))
                    {
                        status = "500 Internal Server Error";
                        body = json_error(workflow_error);
                    }
                    else if(!ensure_route_confirmation_policies(
                                database_path.string(), route_id, nodes, edges))
                    {
                        status = "500 Internal Server Error";
                        body = json_error(
                            "Failed to initialize route-node confirmation policies after route save");
                    }
                    else
                    {
                        const std::string next_route_fingerprint =
                            workflow_route_fingerprint(nodes, edges);
                        if(!batch_id.empty() &&
                           !previous_route_fingerprint.empty() &&
                           previous_route_fingerprint != next_route_fingerprint)
                        {
                            std::string invalidation_error;
                            if(!snapshot_store.invalidate_batch(
                                   batch_id, invalidation_error))
                            {
                                std::cerr
                                    << "Snapshot invalidation after route change failed: "
                                    << invalidation_error << '\n';
                            }
                        }
                        status = "200 OK";
                        body = "{\"routeId\":\"" + json_escape(route_id) +
                               "\",\"batchId\":\"" + json_escape(batch_id) +
                               "\",\"draft\":" + (allow_incomplete ? "true" : "false") +
                               ",\"saved\":true}";
                        live_event_hub.publish("route_changed", batch_id);
                    }
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "GET" && request_path(request.target) == "/api/workflow")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(
                request, database_path, sessions, sessions_mutex);
            if(!user)
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
            }
            else if(user->role != "admin")
            {
                status = "403 Forbidden";
                body = json_error("Admin role required");
            }
            else
            {
                status = "200 OK";
                body = workflow_json(
                    database_path.string(),
                    query_parameter(request.target, "batchId").value_or(""));
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "GET" &&
                request_path(request.target) == "/api/public-route-state")
        {
            content_type = "application/json; charset=utf-8";
            const auto token = request.headers.find("x-publication-token");
            const std::string batch_id = query_parameter(
                request.target, "batchId").value_or("");
            if(token == request.headers.end() ||
               token->second != public_chain_publication_token())
            {
                status = "403 Forbidden";
                body = json_error("Publication authorization failed");
            }
            else if(batch_id.empty() || batch_id.size() > 256)
            {
                status = "422 Unprocessable Entity";
                body = json_error("A valid batchId is required");
            }
            else
            {
                body = public_route_state_json(database_path.string(), batch_id);
                if(body.empty())
                {
                    status = "404 Not Found";
                    body = json_error("The selected batch route is unavailable");
                }
                else
                {
                    status = "200 OK";
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "GET" &&
                request_path(request.target) == "/api/snapshot/status")
        {
            content_type = "application/json; charset=utf-8";
            const auto token = request.headers.find("x-publication-token");
            const std::string batch_id = query_parameter(
                request.target, "batchId").value_or("");
            if(token == request.headers.end() ||
               token->second != public_chain_publication_token())
            {
                status = "403 Forbidden";
                body = json_error("Publication authorization failed");
            }
            else if(batch_id.empty() || batch_id.size() > 256)
            {
                status = "422 Unprocessable Entity";
                body = json_error("A valid batchId is required");
            }
            else
            {
                body = snapshot_status_json(snapshot_store, batch_id);
                if(body.empty())
                {
                    status = "404 Not Found";
                    body = json_error("The selected batch has no active Snapshot");
                }
                else
                {
                    status = "200 OK";
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "GET" && request.target == "/api/chains")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(
                request, database_path, sessions, sessions_mutex);
            if(!user)
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
            }
            else if(user->role != "admin")
            {
                status = "403 Forbidden";
                body = json_error("Admin role required");
            }
            else
            {
                std::vector<SupplyChainRecord> records;
                std::vector<BlockEdge> edges;
                if(load_supply_chain_records(database_path.string(), records) &&
                   load_block_edges(database_path.string(), edges))
                {
                    status = "200 OK";
                    body = chain_json(database_path.string(), records, edges);
                }
                else
                {
                    status = "500 Internal Server Error";
                    body = json_error("Failed to read supply-chain graph");
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "GET" &&
                request.target == "/api/snapshot/eligible-batches")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(
                request, database_path, sessions, sessions_mutex);
            if(!user)
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
            }
            else if(user->role != "admin")
            {
                status = "403 Forbidden";
                body = json_error("Admin role required");
            }
            else
            {
                std::vector<SupplyChainBatch> batches;
                std::vector<SupplyChainRecord> records;
                if(load_supply_chain_batches(database_path.string(), batches) &&
                   load_supply_chain_records(database_path.string(), records))
                {
                    status = "200 OK";
                    body = eligible_snapshot_batches_json(
                        database_path.string(), batches, records);
                }
                else
                {
                    status = "500 Internal Server Error";
                    body = json_error("Failed to read snapshot candidates");
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "POST" &&
                request.target == "/api/snapshot/preview")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(
                request, database_path, sessions, sessions_mutex);
            if(!user)
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
            }
            else if(user->role != "admin")
            {
                status = "403 Forbidden";
                body = json_error("Admin role required");
            }
            else
            {
                const auto content_type_header = request.headers.find("content-type");
                const bool correct_type = content_type_header != request.headers.end() &&
                    lower(content_type_header->second).find(
                        "application/x-www-form-urlencoded") == 0;
                const auto fields = correct_type
                    ? parse_form(request.body)
                    : std::nullopt;
                const auto batch_field = fields
                    ? fields->find("batchId")
                    : std::unordered_map<std::string, std::string>::const_iterator{};
                if(!correct_type || !fields || batch_field == fields->end() ||
                   trim(batch_field->second).empty() || batch_field->second.size() > 256)
                {
                    status = "422 Unprocessable Entity";
                    body = json_error("A valid batchId is required");
                }
                else
                {
                    const std::string batch_id = trim(batch_field->second);
                    const auto batch = find_supply_chain_batch(
                        database_path.string(), batch_id);
                    std::vector<SupplyChainRecord> records;
                    if(!batch)
                    {
                        status = "404 Not Found";
                        body = json_error("The selected batch does not exist");
                    }
                    else if(!load_supply_chain_records(
                                database_path.string(), records))
                    {
                        status = "500 Internal Server Error";
                        body = json_error("Failed to read snapshot source records");
                    }
                    else
                    {
                        std::vector<supermarket::snapshot::EvidenceInput>
                            selected_evidence;
                        const auto selected_field = fields->find("selectedEvidence");
                        std::string selection_error;
                        if(!parse_snapshot_evidence_selection(
                               selected_field == fields->end()
                                   ? ""
                                   : selected_field->second,
                               selected_evidence,
                               selection_error))
                        {
                            status = "422 Unprocessable Entity";
                            body = json_error(selection_error);
                        }

                        if(status.empty())
                        {
                            std::string route_id;
                            std::vector<SupplyRouteNode> route_nodes;
                            std::vector<SupplyRouteEdge> route_edges;
                            if(!load_workflow_route(
                                   database_path.string(), batch_id, route_id,
                                   route_nodes, route_edges))
                            {
                                status = "422 Unprocessable Entity";
                                body = json_error(
                                    "The selected batch route is unavailable");
                            }
                            if(status.empty())
                            {
                                const auto input = make_snapshot_batch_input(
                                    *batch, records, route_nodes, route_edges);
                                std::string preview_error;
                                const auto preview = supermarket::snapshot::build_preview(
                                    input, selected_evidence, preview_error);
                            if(!preview)
                            {
                                status = "422 Unprocessable Entity";
                                body = json_error(preview_error);
                            }
                            else
                            {
                                const std::string candidate_json =
                                    supermarket::snapshot::publication_candidate_json(
                                        *preview);
                                supermarket::snapshot_storage::SnapshotPreview
                                    storage_preview;
                                storage_preview.protocol = preview->protocol;
                                storage_preview.snapshot_id = preview->snapshot_id;
                                storage_preview.schema_version =
                                    preview->snapshot_version;
                                storage_preview.generated_at = preview->generated_at;
                                storage_preview.batch_id = preview->batch_id;
                                storage_preview.manifest_json = preview->manifest_json;
                                storage_preview.public_root = preview->public_root;
                                storage_preview.source_block_hash =
                                    preview->final_private_block_hash;
                                storage_preview.route_fingerprint =
                                    preview->route_fingerprint;
                                storage_preview.candidate_json = candidate_json;
                                std::string storage_error;
                                if(!snapshot_store.save_preview(
                                       storage_preview, storage_error))
                                {
                                    status = "500 Internal Server Error";
                                    body = json_error(
                                        "Failed to store the Snapshot preview: " +
                                        storage_error);
                                }
                                else
                                {
                                    status = "200 OK";
                                    body = snapshot_preview_json(*preview);
                                }
                            }
                            }
                        }
                    }
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "POST" &&
                request.target == "/api/snapshot/publish")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(
                request, database_path, sessions, sessions_mutex);
            const auto type = request.headers.find("content-type");
            const bool correct_type = type != request.headers.end() &&
                lower(type->second).find("application/json") == 0;
            if(!user)
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
            }
            else if(user->role != "admin")
            {
                status = "403 Forbidden";
                body = json_error("Admin role required");
            }
            else if(!correct_type || request.body.empty())
            {
                status = "422 Unprocessable Entity";
                body = json_error("A publication candidate is required");
            }
            else
            {
                const auto published = post_publication_candidate(request.body);
                if(!published)
                {
                    status = "503 Service Unavailable";
                    body = json_error(
                        "The local public-chain service is unavailable");
                }
                else
                {
                    body = published->body;
                    if(published->status_code >= 200 &&
                       published->status_code < 300)
                    {
                        status = "201 Created";
                        const auto storage_preview =
                            snapshot_storage_preview_from_candidate(request.body);
                        if(storage_preview)
                        {
                            const auto transaction_hash = json_string_value(
                                published->body, "transactionHash");
                            std::string storage_error;
                            if(!snapshot_store.mark_published(
                                  *storage_preview,
                                  published->body,
                                  transaction_hash.value_or(""),
                                  storage_error))
                           {
                                std::cerr
                                    << "Snapshot publication storage update failed: "
                                    << storage_error << '\n';
                            }
                           else
                           {
                               std::string verification_error;
                               if(!snapshot_store.touch_verification(
                                      storage_preview->batch_id,
                                      storage_preview->snapshot_id,
                                      "", "published",
                                      "Snapshot was published manually",
                                      verification_error))
                               {
                                   std::cerr
                                       << "Snapshot publication verification status update failed: "
                                       << verification_error << '\n';
                               }
                           }
                        }
                        else
                        {
                            std::cerr
                                << "Snapshot publication response could not be indexed locally\n";
                        }
                       const auto published_batch_id =
                           json_string_value(published->body, "batchId");
                       live_event_hub.publish(
                           "snapshot_published",
                            published_batch_id.value_or(""));
                    }
                    else if(published->status_code == 503)
                        status = "503 Service Unavailable";
                    else
                        status = "422 Unprocessable Entity";
                }
            }
            send_response(client_fd, status, content_type, body, true,
                          CORS_HEADERS);
        }
        else if(request.method == "POST" && request.target == "/api/ipfs/files")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(
                request, database_path, sessions, sessions_mutex);
            if(!user)
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
            }
            else
            {
                const auto content_type_header = request.headers.find("content-type");
                const auto parts = content_type_header == request.headers.end()
                    ? std::nullopt
                    : parse_multipart(content_type_header->second, request.body);
                const MultipartPart* file_part = nullptr;
                std::string category;
                if(parts)
                {
                    for(const MultipartPart& part : *parts)
                    {
                        if(part.name == "category") category = part.content;
                        if(part.name == "file" && !part.filename.empty())
                            file_part = &part;
                    }
                }

                std::string validation_error;
                if(!parts)
                    validation_error = "Content-Type must be multipart/form-data";
                else if(!file_part)
                    validation_error = "A file is required";
                else if(!is_allowed_attachment_category(user->role, category))
                    validation_error = "The attachment category is not allowed for this role";
                else if(file_part->content.size() > MAX_IPFS_FILE_SIZE)
                    validation_error = "The file exceeds the local demo size limit";

                if(!validation_error.empty())
                {
                    status = "422 Unprocessable Entity";
                    body = json_error(validation_error);
                }
                else
                {
                    const auto added = add_file_to_ipfs(*file_part);
                    if(!added)
                    {
                        status = "503 Service Unavailable";
                        body = json_error(
                            "IPFS upload failed. Start a local IPFS API at IPFS_API_URL");
                    }
                    else
                    {
                        status = "201 Created";
                        body = "{\"category\":\"" + json_escape(category) +
                               "\",\"cid\":\"" + json_escape(added->cid) +
                               "\",\"filename\":\"" +
                               json_escape(file_part->filename) +
                               "\",\"contentType\":\"" +
                               json_escape(file_part->content_type) +
                               "\",\"size\":" +
                               std::to_string(file_part->content.size()) + "}";
                    }
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "POST" && request.target == "/api/records")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(
                request, database_path, sessions, sessions_mutex);
            if(!user)
            {
                status = "401 Unauthorized";
                body = json_error("Authentication required");
            }
            else
            {
                const auto content_type_header = request.headers.find("content-type");
                const bool correct_type = content_type_header != request.headers.end() &&
                    lower(content_type_header->second).find(
                        "application/x-www-form-urlencoded") == 0;
                auto fields = correct_type ? parse_form(request.body) : std::nullopt;

                std::string validation_error;
                std::vector<IpfsReference> references;
                std::string confirmation_method = "none";
                std::string confirmation_name;
                std::string signature_algorithm;
                std::string signature;
                std::string signature_public_key_hash;
                std::string signed_payload_hash;
                bool signature_verified = false;
                if(!correct_type)
                    validation_error = "Content-Type must be application/x-www-form-urlencoded";
                else if(!fields)
                    validation_error = "Malformed form body";
                else
                {
                    for(auto& field : *fields)
                    {
                        if(field.first != "signaturePayload")
                            field.second = trim(field.second);
                    }

                    const bool is_supplier = user->role == "supplier";
                    auto required_value = [&](const std::string& name) {
                        const auto item = fields->find(name);
                        return item != fields->end() &&
                               !trim(item->second).empty() &&
                               item->second.size() <= 256;
                    };

                    if(user->role != "supplier" && user->role != "logistics" &&
                       user->role != "warehouse" && user->role != "supermarket")
                        validation_error = "This account is not part of the configured workflow";
                    else if(!is_supplier && !required_value("batchId"))
                        validation_error = "batchId is required for a continuing stage";
                    else if(is_supplier && !required_value("product"))
                        validation_error = "product is required for a new batch";
                    else if(is_supplier &&
                            !normalize_product_code(
                                trim(form_field_value(*fields, "product"))))
                        validation_error =
                            "product must contain at least one letter or number";
                    else if(fields->find("confirmed") == fields->end() ||
                            form_field_value(*fields, "confirmed") != "true")
                        validation_error = "Information must be confirmed";
                    else
                    {
                        for(const std::string& name : role_event_fields(user->role))
                        {
                            if(!required_value(name))
                            {
                                validation_error = name + " is required";
                                break;
                            }
                        }

                        if(validation_error.empty())
                        {
                            for(const std::string& name : role_event_fields(user->role))
                            {
                                const auto item = fields->find(name);
                                if(item == fields->end()) continue;
                                const std::string identifier_error =
                                    identifier_format_error(name, trim(item->second));
                                if(!identifier_error.empty())
                                {
                                    validation_error = identifier_error;
                                    break;
                                }
                            }
                        }

                        if(validation_error.empty())
                        {
                            for(const std::string& name : role_event_fields(user->role))
                            {
                                const auto item = fields->find(name);
                                if(item == fields->end()) continue;
                                const std::string measurement_error =
                                    measurement_format_error(name, trim(item->second));
                                if(!measurement_error.empty())
                                {
                                    validation_error = measurement_error;
                                    break;
                                }
                            }
                        }

                        if(validation_error.empty() && user->role == "logistics" &&
                           !local_datetime_range_is_valid(
                               trim(form_field_value(*fields, "departureTime")),
                               trim(form_field_value(*fields, "arrivalTime"))))
                        {
                            validation_error =
                                "Arrival Time must be after Departure Time";
                        }
                        if(validation_error.empty() && user->role == "warehouse" &&
                           !local_datetime_range_is_valid(
                               trim(form_field_value(*fields, "inboundTime")),
                               trim(form_field_value(*fields, "outboundTime"))))
                        {
                            validation_error =
                                "Outbound Time must be after Inbound Time";
                        }
                    }

                    if(validation_error.empty())
                    {
                        const auto references_field = fields->find("ipfsRefs");
                        if(!parse_ipfs_references(
                               references_field == fields->end()
                                   ? ""
                                   : references_field->second,
                               references,
                               validation_error))
                        {
                            // The parser supplies the validation message.
                        }
                        else
                        {
                            for(const IpfsReference& reference : references)
                            {
                                if(!is_allowed_attachment_category(
                                       user->role, reference.category))
                                {
                                    validation_error =
                                        "The attachment category is not allowed for this role";
                                    break;
                                }
                            }
                        }
                    }

                    if(validation_error.empty())
                    {
                        ConfirmationPolicy policy;
                        std::optional<RuntimeWorkflow> policy_workflow;
                        if(is_supplier)
                            policy_workflow = load_runtime_workflow(
                                database_path.string(), "");
                        else
                            policy_workflow = load_runtime_workflow(
                                database_path.string(),
                                trim(form_field_value(*fields, "batchId")));

                        const SupplyRouteNode* policy_node = nullptr;
                        if(policy_workflow)
                        {
                            if(is_supplier)
                            {
                                for(const SupplyRouteNode& node : policy_workflow->nodes)
                                {
                                    if(node.role == user->role &&
                                       node.username == user->username)
                                    {
                                        policy_node = &node;
                                        break;
                                    }
                                }
                            }
                            else
                            {
                                // The in-memory record list is updated by other
                                // request workers after a successful Block
                                // commit. Protect this progress read from a
                                // concurrent vector mutation.
                                std::lock_guard<std::mutex> chain_lock(chain_mutex);
                                policy_node = next_pending_workflow_node(
                                    *policy_workflow, stored_records,
                                    trim(form_field_value(*fields, "batchId")));
                            }
                        }

                        if(validation_error.empty() && !policy_workflow)
                            validation_error = "The configured route is unavailable";
                        else if(validation_error.empty() && !policy_node)
                            validation_error =
                                "No confirmation policy is assigned to this route stage";
                        else if(validation_error.empty() &&
                                (policy_node->role != user->role ||
                                 policy_node->username != user->username))
                            validation_error =
                                "This account is not assigned to the next route stage";
                        else if(validation_error.empty() && !is_supplier &&
                                trim(form_field_value(*fields, "routeId")) !=
                                    policy_workflow->route_id)
                            validation_error =
                                "The submitted route does not match the selected batch";
                        else if(validation_error.empty() && !is_supplier &&
                                trim(form_field_value(*fields, "routeNodeId")) !=
                                    policy_node->node_id)
                            validation_error =
                                "The submitted route node does not match the next route stage";
                        else if(validation_error.empty() &&
                                !load_route_confirmation_policy(
                                    database_path.string(),
                                    policy_workflow->route_id,
                                    policy_node->node_id,
                                    policy))
                        {
                            validation_error =
                                "Confirmation policy is unavailable for this route stage";
                        }
                        if(validation_error.empty())
                        {
                            const auto method_item = fields->find("confirmationMethod");
                            const bool has_method = method_item != fields->end() &&
                                !method_item->second.empty();
                            if(!has_method)
                            {
                                validation_error =
                                    "Select a configured confirmation method";
                            }
                            else
                            {
                                confirmation_method = method_item == fields->end()
                                    ? ""
                                    : method_item->second;
                                confirmation_name = fields->count("confirmationName")
                                    ? fields->at("confirmationName")
                                    : "";
                                signature_algorithm = fields->count("signatureAlgorithm")
                                    ? fields->at("signatureAlgorithm")
                                    : "";
                                signature = fields->count("signature")
                                    ? fields->at("signature")
                                    : "";
                                const std::string public_key = fields->count("signaturePublicKey")
                                    ? fields->at("signaturePublicKey")
                                    : "";
                                const std::string challenge = fields->count(
                                    "confirmationChallenge")
                                    ? fields->at("confirmationChallenge")
                                    : "";
                                const std::string submitted_payload = fields->count(
                                    "signaturePayload")
                                    ? fields->at("signaturePayload")
                                    : "";

                                if(confirmation_method != "typed_name")
                                    validation_error =
                                        "Only typed-name confirmation is supported";
                                else if(!policy.typed_name)
                                    validation_error =
                                        "Typed-name confirmation is disabled for this route stage";
                                else if(confirmation_name != effective_display_name(*user))
                                    validation_error =
                                        "The typed name must match the registered display name";
                                else if(signature_algorithm != "ECDSA-P256-SHA256" ||
                                        public_key.empty() || signature.empty() ||
                                        challenge.empty() || submitted_payload.empty())
                                    validation_error =
                                        "A complete digital signature is required";
                                else if(public_key.size() > 4096 || signature.size() > 4096 ||
                                        submitted_payload.size() > 1024 * 1024)
                                    validation_error = "The digital signature payload is too large";
                                else
                                {
                                    std::string signed_batch_id = "SERVER_ALLOCATED";
                                    std::string signed_product;
                                    if(is_supplier)
                                    {
                                        signed_product = trim(
                                            form_field_value(*fields, "product"));
                                    }
                                    else
                                    {
                                        signed_batch_id = trim(
                                            form_field_value(*fields, "batchId"));
                                        const auto batch = find_supply_chain_batch(
                                            database_path.string(), signed_batch_id);
                                        if(!batch)
                                            validation_error =
                                                "The selected batch does not exist";
                                        else
                                            signed_product = batch->product;
                                    }

                                    if(validation_error.empty())
                                    {
                                        const std::string expected_payload =
                                            confirmation_signature_payload(
                                                challenge, *user, confirmation_method,
                                                confirmation_name, signed_batch_id,
                                                signed_product, *fields, references, true);
                                        if(submitted_payload != expected_payload)
                                            validation_error =
                                                "The signed confirmation payload does not match";
                                        else
                                        {
                                            bool challenge_valid = false;
                                            {
                                                std::lock_guard<std::mutex> lock(
                                                    confirmation_challenges_mutex);
                                                const auto challenge_item =
                                                    confirmation_challenges.find(challenge);
                                                if(challenge_item != confirmation_challenges.end() &&
                                                   challenge_item->second.uid == user->uid &&
                                                   challenge_item->second.expires_at >
                                                       std::chrono::steady_clock::now())
                                                {
                                                    challenge_valid = true;
                                                    confirmation_challenges.erase(challenge_item);
                                                }
                                            }
                                            if(!challenge_valid)
                                                validation_error =
                                                    "The confirmation challenge is expired or invalid";
                                            else if(!verify_ecdsa_p256_signature(
                                                        public_key, signature,
                                                        submitted_payload))
                                                validation_error =
                                                    "Digital signature verification failed";
                                            else if(!save_user_public_key(
                                                        database_path.string(), user->uid,
                                                        public_key))
                                                validation_error =
                                                    "This account is already bound to another signing key";
                                            else
                                            {
                                                signature_public_key_hash = sha256_value(public_key);
                                                signed_payload_hash = sha256_value(submitted_payload);
                                                signature_verified = true;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                if(!validation_error.empty())
                {
                    status = "422 Unprocessable Entity";
                    body = json_error(validation_error);
                }
                else
                {
                    std::lock_guard<std::mutex> chain_lock(chain_mutex);
                    const bool is_supplier = user->role == "supplier";
                    std::string batch_id;
                    if(is_supplier)
                    {
                        std::vector<SupplyChainBatch> batches;
                        if(!load_supply_chain_batches(
                               database_path.string(), batches))
                        {
                            status = "500 Internal Server Error";
                            body = json_error("Failed to read existing batches");
                            send_response(
                                client_fd, status, content_type, body, true, CORS_HEADERS);
                            close(client_fd);
                            return;
                        }

                        const auto generated_batch_id = next_batch_id_for_product(
                            batches,
                            trim(form_field_value(*fields, "product")));
                        if(!generated_batch_id)
                        {
                            status = "409 Conflict";
                            body = json_error("Unable to allocate a batch ID for this product");
                            send_response(
                                client_fd, status, content_type, body, true, CORS_HEADERS);
                            close(client_fd);
                            return;
                        }
                        batch_id = *generated_batch_id;
                    }
                    else
                    {
                        batch_id = trim(form_field_value(*fields, "batchId"));
                    }
                    const auto stored_batch =
                        find_supply_chain_batch(database_path.string(), batch_id);
                    const auto workflow = load_runtime_workflow(
                        database_path.string(), is_supplier ? "" : batch_id);
                    const WorkflowProgress progress = workflow
                        ? evaluate_workflow_progress(*workflow, stored_records, batch_id)
                        : WorkflowProgress{};
                    const SupplyChainRecord* parent = progress.parent_record;
                    const SupplyRouteNode* required_node = progress.next_node;
                    const SupplyRouteNode* destination_node = workflow && required_node
                        ? workflow_successor(*workflow, required_node->node_id)
                        : nullptr;
                    const auto has_expected_sequence = [](const std::string& value,
                                                          const std::string& expected) {
                        return expected.size() >= 4 && value.size() >= 4 &&
                            value.substr(value.size() - 4) ==
                                expected.substr(expected.size() - 4);
                    };
                    const auto has_prefix = [](const std::string& value,
                                               const std::string& first,
                                               const std::string& second) {
                        return value.rfind(first, 0) == 0 ||
                            value.rfind(second, 0) == 0;
                    };
                    std::string workflow_error;
                    SupplyChainBatch batch;

                    if(!workflow)
                    {
                        workflow_error = "The configured route is unavailable";
                    }
                    else if(!progress.error.empty())
                    {
                        workflow_error = progress.error;
                    }
                    else if(is_supplier && (parent || stored_batch))
                    {
                        workflow_error = "This batch ID is already in use";
                    }
                    else if(!is_supplier && !stored_batch)
                    {
                        workflow_error = "Select an existing batch created by the Supplier stage";
                    }
                    else if(!is_supplier && !parent)
                    {
                        workflow_error = "The selected batch has no previous block";
                    }
                    else if(!required_node)
                    {
                        workflow_error = "This batch has already reached the Supermarket stage";
                    }
                    else if(required_node->role != user->role)
                    {
                        workflow_error = "The next required stage is " + required_node->label;
                    }
                    else if(required_node->username.empty())
                    {
                        workflow_error = "The next route stage has no assigned active account";
                    }
                    else if(required_node->username != user->username)
                    {
                        workflow_error = "The next route stage is assigned to " +
                            required_node->username;
                    }
                    else if(!is_supplier &&
                            trim(form_field_value(*fields, "routeId")) !=
                                workflow->route_id)
                    {
                        workflow_error =
                            "The route changed while this form was open; reload the assigned stage";
                    }
                    else if(!is_supplier &&
                            trim(form_field_value(*fields, "routeNodeId")) !=
                                required_node->node_id)
                    {
                        workflow_error =
                            "The assigned route stage changed while this form was open";
                    }
                    else if(user->role == "logistics" && !destination_node)
                    {
                        workflow_error = "The configured transport stage has no destination";
                    }
                    else if(user->role == "logistics" &&
                            trim(form_field_value(*fields, "shipmentId")) !=
                                default_transport_shipment_id(*workflow, *required_node))
                    {
                        workflow_error = "Shipment ID must be " +
                            default_transport_shipment_id(*workflow, *required_node) +
                            " for " + required_node->label;
                    }
                    else if(user->role == "logistics" &&
                            (!has_prefix(
                                 trim(form_field_value(*fields, "vehicleContainerId")),
                                 "VEHICLE-", "CONTAINER-") ||
                             !has_expected_sequence(
                                 trim(form_field_value(*fields, "vehicleContainerId")),
                                 default_transport_vehicle_id(*workflow, *required_node))))
                    {
                        const std::string expected_vehicle = default_transport_vehicle_id(
                            *workflow, *required_node);
                        workflow_error = "Vehicle / Container ID must use sequence " +
                            expected_vehicle.substr(expected_vehicle.size() - 4) +
                            " for " + required_node->label;
                    }
                    else if(user->role == "warehouse" &&
                            trim(form_field_value(*fields, "storageLotId")) !=
                                default_storage_lot_id(*workflow, *required_node))
                    {
                        workflow_error = "Storage Lot ID must be " +
                            default_storage_lot_id(*workflow, *required_node) +
                            " for " + required_node->label;
                    }
                    else if(user->role == "warehouse" &&
                            (!has_prefix(
                                 trim(form_field_value(*fields, "storageZoneRackId")),
                                 "ZONE-", "RACK-") ||
                             !has_expected_sequence(
                                 trim(form_field_value(*fields, "storageZoneRackId")),
                                 default_storage_zone_id(*workflow, *required_node))))
                    {
                        const std::string expected_zone =
                            default_storage_zone_id(*workflow, *required_node);
                        workflow_error = "Storage Zone / Rack ID must use sequence " +
                            expected_zone.substr(expected_zone.size() - 4) +
                            " for " + required_node->label;
                    }
                    else if(user->role == "logistics" &&
                            trim(form_field_value(*fields, "deliveryLocation")) !=
                                destination_node->label)
                    {
                        workflow_error = "Delivery location must match the configured route";
                    }

                    if(workflow_error.empty())
                    {
                        if(user->role == "logistics")
                            (*fields)["deliveryLocation"] = destination_node->label;
                        const bool is_final_node = required_node->role == "supermarket" ||
                            std::none_of(
                                workflow->edges.begin(), workflow->edges.end(),
                                [&](const SupplyRouteEdge& edge) {
                                    return edge.from_node_id == required_node->node_id;
                                });
                        if(is_supplier)
                        {
                            batch.batch_id = batch_id;
                            batch.product = trim(
                                form_field_value(*fields, "product"));
                            batch.harvest_date = form_field_value(
                                *fields, "harvestDate");
                            batch.farm_location = form_field_value(
                                *fields, "farmLocation");
                            batch.certificate_id = form_field_value(
                                *fields, "certificateId");
                            batch.created_by_uid = user->uid;
                            batch.current_stage = required_node->role;
                            batch.status = is_final_node ? "completed" : "in_progress";
                        }
                        else
                        {
                            batch = *stored_batch;
                        }
                        batch.route_id = workflow->route_id;
                        batch.route_node_id = required_node->node_id;
                        batch.route_step_index = required_node->step_index;
                    }

                    if(!workflow_error.empty())
                    {
                        status = "409 Conflict";
                        body = json_error(workflow_error);
                    }
                    else
                    {
                        const int block_id = static_cast<int>(stored_records.size());
                        const std::vector<MerkleField> merkle_fields = block_merkle_fields(
                            block_id, batch, *fields, *user, references, parent,
                            confirmation_method, confirmation_name,
                            signature_public_key_hash, signed_payload_hash);
                        const auto merkle = build_block_merkle(merkle_fields);
                        if(!merkle)
                        {
                            status = "500 Internal Server Error";
                            body = json_error("Failed to build block Merkle Tree");
                        }
                        else
                        {
                            SupplyChainRecord database_record;
                            database_record.block_id = block_id;
                            database_record.parent_block_id = parent
                                ? parent->block_id
                                : -1;
                            database_record.parent_block_hash = parent
                                ? parent->block_hash
                                : "GENESIS";
                            database_record.batch_id = batch.batch_id;
                            database_record.product = batch.product;
                            if(user->role == "supplier")
                                database_record.location_summary = form_field_value(
                                    *fields, "farmLocation");
                            else if(user->role == "logistics")
                                database_record.location_summary = form_field_value(
                                    *fields, "pickupLocation") + " -> " +
                                    form_field_value(*fields, "deliveryLocation");
                            else if(user->role == "warehouse")
                                database_record.location_summary =
                                    form_field_value(*fields, "storageZoneRackId");
                            else
                                database_record.location_summary =
                                    form_field_value(*fields, "storeLocationId");
                            database_record.batch_harvest_date = batch.harvest_date;
                            database_record.batch_farm_location = batch.farm_location;
                            database_record.certificate_id = batch.certificate_id;
                            database_record.stage = required_node->role;
                            database_record.confirmed_by = user->username;
                            database_record.uid = user->uid;
                            database_record.role = user->role;
                            database_record.organization_id = user->organization_id;
                            database_record.event_data = event_data_json(user->role, *fields);
                            database_record.ipfs_refs = references;
                            database_record.merkle_fields = merkle_fields;
                            database_record.canonical_record = merkle->canonical_record;
                            database_record.root_hash = merkle->root_hash;
                            database_record.merkle_leaves = merkle->leaves;
                            database_record.verified = merkle->verified;
                            database_record.confirmation_method = confirmation_method;
                            database_record.confirmation_name = confirmation_name;
                            database_record.signature_algorithm = signature_algorithm;
                            database_record.signature = signature;
                            database_record.signature_public_key_hash =
                                signature_public_key_hash;
                            database_record.signed_payload_hash = signed_payload_hash;
                            database_record.signature_verified = signature_verified;
                            database_record.route_id = workflow->route_id;
                            database_record.route_node_id = required_node->node_id;
                            database_record.route_step_index = required_node->step_index;
                            database_record.transport_shipment_id = user->role == "logistics"
                                ? form_field_value(*fields, "shipmentId")
                                : "";
                            database_record.transport_vehicle_container_id = user->role == "logistics"
                                ? form_field_value(*fields, "vehicleContainerId")
                                : "";
                            database_record.chain_status = "in_progress";
                            std::vector<SupplyChainRecord> projected_records =
                                stored_records;
                            projected_records.push_back(database_record);
                            const WorkflowProgress projected_progress =
                                evaluate_workflow_progress(
                                    *workflow, projected_records, batch_id);
                            if(projected_progress.error.empty() &&
                               projected_progress.complete)
                                database_record.chain_status = "completed";
                            database_record.block_hash = calculate_block_hash(database_record);

                            std::vector<BlockEdge> new_edges;
                            if(parent)
                            {
                                new_edges.push_back(BlockEdge{
                                    parent->block_id,
                                    block_id,
                                    batch_id,
                                    "continues"
                                });
                            }

                            if(!insert_supply_chain_block(
                                   database_path.string(), database_record, new_edges))
                            {
                                status = "500 Internal Server Error";
                                body = json_error("Failed to save supply-chain block");
                            }
                            else
                            {
                                stored_records.push_back(database_record);
                                chain_edges.insert(
                                    chain_edges.end(), new_edges.begin(), new_edges.end());
                                const WorkflowProgress updated_progress =
                                    evaluate_workflow_progress(
                                        *workflow, stored_records, batch_id);
                                const SupplyRouteNode* next_required_node =
                                    updated_progress.error.empty()
                                        ? updated_progress.next_node
                                        : nullptr;
                                status = "201 Created";
                                body = "{\"blockID\":" + std::to_string(block_id) +
                                       ",\"verified\":" +
                                       (database_record.verified ? "true" : "false") +
                                       ",\"chainStatus\":\"" +
                                       json_escape(database_record.chain_status) +
                                       "\",\"stage\":\"" +
                                       json_escape(database_record.stage) +
                                       "\",\"batchId\":\"" +
                                       json_escape(database_record.batch_id) +
                                       "\",\"nextStage\":\"" +
                                       json_escape(next_required_node
                                           ? next_required_node->role
                                           : "") +
                                       "\",\"nextNodeId\":\"" +
                                       json_escape(next_required_node
                                           ? next_required_node->node_id
                                           : "") +
                                       "\",\"nextNodeLabel\":\"" +
                                       json_escape(next_required_node
                                           ? next_required_node->label
                                           : "") +
                                       "\",\"ipfsCount\":" +
                                       std::to_string(references.size()) +
                                       ",\"signatureVerified\":" +
                                       (database_record.signature_verified ? "true" : "false") + "}";
                                live_event_hub.publish("batch_changed", database_record.batch_id);
                            }
                        }
                    }
                }
            }

            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
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
        catch(const std::exception& error)
        {
            std::cerr << "Request handling failed: " << error.what() << '\n';
            send_response(
                client_fd,
                "500 Internal Server Error",
                "application/json; charset=utf-8",
                json_error("Internal server error while processing the request"),
                true,
                CORS_HEADERS);
            close(client_fd);
        }
        catch(...)
        {
            std::cerr << "Request handling failed with an unknown exception\n";
            send_response(
                client_fd,
                "500 Internal Server Error",
                "application/json; charset=utf-8",
                json_error("Internal server error while processing the request"),
                true,
                CORS_HEADERS);
            close(client_fd);
        }
        }))
        {
            send_response(client_fd, "503 Service Unavailable",
                          "text/plain; charset=utf-8",
                          "Server is busy. Please retry the request.\n", true);
            close(client_fd);
        }
    }
}
