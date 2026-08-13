#include <arpa/inet.h>
#include <algorithm>
#include <cctype>
#include <csignal>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "auth_utils.hpp"
#include "block_merkle.hpp"
#include "db_utils.hpp"
#include "digital_signature.hpp"
#include "identifier_utils.hpp"
#include "log_utils.hpp"
#include "snapshot_adapter.hpp"
#include "snapshot_policy.hpp"
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
    "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";

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

struct IpfsEndpoint
{
    std::string host;
    std::string port;
};

std::optional<IpfsEndpoint> ipfs_endpoint()
{
    const char* configured = std::getenv("IPFS_API_URL");
    const std::string url = configured && *configured
        ? configured
        : "http://127.0.0.1:5002";
    if(url.rfind("http://", 0) != 0) return std::nullopt;

    const std::string authority = url.substr(7);
    const std::size_t path_start = authority.find('/');
    const std::string host_port = authority.substr(0, path_start);
    if(host_port.empty()) return std::nullopt;

    const std::size_t colon = host_port.rfind(':');
    IpfsEndpoint endpoint;
    endpoint.host = colon == std::string::npos
        ? host_port
        : host_port.substr(0, colon);
    endpoint.port = colon == std::string::npos
        ? "5002"
        : host_port.substr(colon + 1);
    if(endpoint.host.empty() || endpoint.port.empty()) return std::nullopt;
    return endpoint;
}

int connect_to_ipfs(const IpfsEndpoint& endpoint)
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

struct IpfsAddResult
{
    std::string cid;
};

std::optional<IpfsAddResult> add_file_to_ipfs(const MultipartPart& file)
{
    const auto endpoint = ipfs_endpoint();
    if(!endpoint) return std::nullopt;

    const int socket_fd = connect_to_ipfs(*endpoint);
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

struct WorkflowStep
{
    const char* id;
    const char* label;
    const char* role;
    const char* username;
};

const std::vector<WorkflowStep>& fixed_workflow()
{
    static const std::vector<WorkflowStep> workflow = {
        {"supplier", "Supplier", "supplier", "supplier01"},
        {"logistics", "Logistics", "logistics", "logistics01"},
        {"warehouse", "Warehouse", "warehouse", "warehouse01"},
        {"supermarket", "Supermarket", "supermarket", "supermarket01"}
    };
    return workflow;
}

int workflow_index_for_role(const std::string& role)
{
    const auto& workflow = fixed_workflow();
    for(std::size_t i = 0; i < workflow.size(); ++i)
    {
        if(role == workflow[i].role)
            return static_cast<int>(i);
    }
    return -1;
}

std::string stage_for_role(const std::string& role)
{
    const int index = workflow_index_for_role(role);
    if(index < 0) return role;
    return fixed_workflow()[static_cast<std::size_t>(index)].role;
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
    add_field("stage", stage_for_role(account.role));
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
    return "{\"role\":\"" + json_escape(policy.role) +
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

std::string eligible_snapshot_batches_json(
    const std::vector<SupplyChainBatch>& batches,
    const std::vector<SupplyChainRecord>& records)
{
    std::ostringstream json;
    json << "{\"batches\":[";
    bool first = true;
    for(const SupplyChainBatch& batch : batches)
    {
        const supermarket::snapshot::BatchInput input =
            make_snapshot_batch_input(batch, records);
        const auto eligibility = supermarket::snapshot::evaluate_eligibility(input);
        if(!eligibility.eligible) continue;
        if(!first) json << ',';
        first = false;
        json << "{\"batchId\":\"" << json_escape(batch.batch_id)
             << "\",\"product\":\"" << json_escape(batch.product)
             << "\",\"status\":\"" << json_escape(batch.status)
             << "\",\"finalPrivateBlockHash\":\""
             << json_escape(input.stages.back().block_hash)
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
         << "\",\"publicRoot\":\"" << json_escape(preview.public_root)
         << "\",\"finalPrivateBlockHash\":\""
         << json_escape(preview.final_private_block_hash)
         << "\",\"publicFieldCount\":" << preview.public_fields.size()
         << ",\"selectedEvidenceCount\":" << preview.public_evidence.size()
         << ",\"excludedFields\":"
         << string_array_json(preview.excluded_fields)
         << ",\"manifest\":" << preview.manifest_json << '}';
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

std::string records_json(const std::vector<SupplyChainRecord>& records)
{
    std::ostringstream json;
    json << '[';
    for(std::size_t i = 0; i < records.size(); ++i)
    {
        if(i > 0) json << ',';
        const SupplyChainRecord& record = records[i];
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
             << "\",\"eventData\":" << (record.event_data.empty()
                 ? "{}"
                 : record.event_data)
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
             << ",\"createdAt\":\"" << json_escape(record.created_at) << "\"}";
    }
    json << ']';
    return json.str();
}

std::string batches_json(const std::vector<SupplyChainBatch>& batches)
{
    std::ostringstream json;
    json << '[';
    for(std::size_t index = 0; index < batches.size(); ++index)
    {
        if(index > 0) json << ',';
        const SupplyChainBatch& batch = batches[index];
        const int current_index = workflow_index_for_role(batch.current_stage);
        const bool completed = batch.status == "completed" ||
            current_index == static_cast<int>(fixed_workflow().size()) - 1;
        const std::string next_stage = completed || current_index < 0 ||
            current_index + 1 >= static_cast<int>(fixed_workflow().size())
            ? ""
            : fixed_workflow()[static_cast<std::size_t>(current_index + 1)].role;
        json << "{\"batchId\":\"" << json_escape(batch.batch_id)
             << "\",\"product\":\"" << json_escape(batch.product)
             << "\",\"harvestDate\":\"" << json_escape(batch.harvest_date)
             << "\",\"farmLocation\":\""
             << json_escape(batch.farm_location)
             << "\",\"certificateId\":\""
             << json_escape(batch.certificate_id)
             << "\",\"currentStage\":\""
             << json_escape(batch.current_stage)
             << "\",\"nextStage\":\"" << json_escape(next_stage)
             << "\",\"status\":\"" << json_escape(batch.status)
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

std::string chain_json(const std::vector<SupplyChainRecord>& records,
                       const std::vector<BlockEdge>& edges)
{
    return "{\"nodes\":" + records_json(records) +
           ",\"edges\":" + edges_json(edges) + "}";
}

std::string workflow_json()
{
    const auto& workflow = fixed_workflow();
    std::ostringstream json;
    json << "{\"nodes\":[";
    for(std::size_t i = 0; i < workflow.size(); ++i)
    {
        if(i > 0) json << ',';
        const WorkflowStep& step = workflow[i];
        json << "{\"id\":\"" << json_escape(step.id)
             << "\",\"label\":\"" << json_escape(step.label)
             << "\",\"role\":\"" << json_escape(step.role)
             << "\",\"username\":\"" << json_escape(step.username)
             << "\"}";
    }
    json << "],\"edges\":[";
    for(std::size_t i = 1; i < workflow.size(); ++i)
    {
        if(i > 1) json << ',';
        json << "{\"from\":\"" << json_escape(workflow[i - 1].id)
             << "\",\"to\":\"" << json_escape(workflow[i].id)
             << "\"}";
    }
    json << "]}";
    return json.str();
}

const SupplyChainRecord* latest_batch_record(
    const std::vector<SupplyChainRecord>& records,
    const std::string& batch_id)
{
    const SupplyChainRecord* latest = nullptr;
    for(const SupplyChainRecord& record : records)
    {
        if(record.batch_id != batch_id) continue;
        if(!latest || record.block_id > latest->block_id)
            latest = &record;
    }
    return latest;
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

    if(!delete_expired_auth_sessions(database_path.string(), unix_time_now()))
        return 1;

    const std::vector<UserAccount> demo_accounts = {
        make_demo_account("uid-supplier-001", "supplier01", "supplier123",
                          "supplier", "supplier-demo"),
        make_demo_account("uid-logistics-001", "logistics01", "logistics123",
                          "logistics", "logistics-demo"),
        make_demo_account("uid-warehouse-001", "warehouse01", "warehouse123",
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

    ThreadPool thread_pool;
    std::cout << "Control server: http://127.0.0.1:" << port << '\n'
              << "Static root: " << static_root << '\n'
              << "Database: " << database_path << '\n'
              << "Restored blocks: " << stored_records.size() << '\n'
              << "Worker threads: " << thread_pool.worker_count() << '\n';

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
            request.target == "/api/confirmation/challenge" ||
            request.target == "/api/records" ||
            request.target == "/api/batches" ||
            request.target == "/api/ipfs/files" ||
            request.target == "/api/workflow" ||
            request.target == "/api/chains" ||
            request.target == "/api/snapshot/eligible-batches" ||
            request.target == "/api/snapshot/preview";

        if(request.method == "OPTIONS" && is_api_target)
        {
            status = "204 No Content";
            body.clear();
            send_response(client_fd, status, content_type, body, false, CORS_HEADERS);
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
        else if(request.method == "GET" && request.target == "/api/confirmation-policy")
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
                if(user->role == "admin")
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
        else if(request.method == "POST" && request.target == "/api/confirmation-policy")
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
                        policy.face = parse_boolean(*fields, role + "Face");
                        policy.updated_by_uid = user->uid;
                        if(!policy.typed_name && !policy.handwritten && !policy.face)
                            invalid_role = role;
                        policies.push_back(std::move(policy));
                    }

                    if(!invalid_role.empty())
                    {
                        status = "422 Unprocessable Entity";
                        body = json_error(
                            "At least one confirmation method must be enabled for " +
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
                    body = records_json(records);
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
                if(load_supply_chain_batches(database_path.string(), batches))
                {
                    status = "200 OK";
                    body = batches_json(batches);
                }
                else
                {
                    status = "500 Internal Server Error";
                    body = json_error("Failed to read supply-chain batches");
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "GET" && request.target == "/api/workflow")
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
                body = workflow_json();
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
                    body = chain_json(records, edges);
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
                    body = eligible_snapshot_batches_json(batches, records);
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
                            const auto input = make_snapshot_batch_input(
                                *batch, records);
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
                                status = "200 OK";
                                body = snapshot_preview_json(*preview);
                            }
                        }
                    }
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
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

                    const int current_index = workflow_index_for_role(user->role);
                    auto required_value = [&](const std::string& name) {
                        const auto item = fields->find(name);
                        return item != fields->end() &&
                               !trim(item->second).empty() &&
                               item->second.size() <= 256;
                    };

                    if(current_index < 0)
                        validation_error = "This account is not part of the preset workflow";
                    else if(current_index != 0 && !required_value("batchId"))
                        validation_error = "batchId is required for a continuing stage";
                    else if(current_index == 0 && !required_value("product"))
                        validation_error = "product is required for a new batch";
                    else if(current_index == 0 &&
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
                        if(!load_confirmation_policy(
                               database_path.string(), user->role, policy))
                        {
                            validation_error = "Confirmation policy is unavailable";
                        }
                        else
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

                                if(confirmation_method == "typed_name" &&
                                   !policy.typed_name)
                                    validation_error =
                                        "The selected confirmation method is disabled";
                                else if(confirmation_method == "handwritten" &&
                                        !policy.handwritten)
                                    validation_error =
                                        "The selected confirmation method is disabled";
                                else if(confirmation_method == "face" && !policy.face)
                                    validation_error =
                                        "The selected confirmation method is disabled";
                                else if(confirmation_method != "typed_name")
                                    validation_error =
                                        "Only typed-name confirmation is available in this demo";
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
                                    if(current_index == 0)
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
                    const int current_index = workflow_index_for_role(user->role);
                    std::string batch_id;
                    if(current_index == 0)
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
                    const SupplyChainRecord* parent =
                        latest_batch_record(stored_records, batch_id);
                    const auto& workflow = fixed_workflow();
                    const auto stored_batch =
                        find_supply_chain_batch(database_path.string(), batch_id);
                    std::string workflow_error;
                    SupplyChainBatch batch;

                    if(current_index < 0)
                    {
                        workflow_error = "This account is not part of the preset workflow";
                    }
                    else if(current_index == 0 && (parent || stored_batch))
                    {
                        workflow_error = "This batch ID is already in use";
                    }
                    else if(current_index != 0 && !stored_batch)
                    {
                        workflow_error = "Select an existing batch created by the Supplier stage";
                    }
                    else if(current_index != 0 && !parent)
                    {
                        workflow_error = "The selected batch has no previous block";
                    }
                    else if(parent)
                    {
                        const int parent_index = workflow_index_for_role(parent->role);
                        if(parent->chain_status == "completed" ||
                           parent_index == static_cast<int>(workflow.size()) - 1)
                        {
                            workflow_error =
                                "This batch has already reached the Supermarket stage";
                        }
                        else if(parent_index < 0)
                        {
                            workflow_error = "The previous block has an unknown workflow stage";
                        }
                        else if(current_index != parent_index + 1)
                        {
                            workflow_error = "The next required stage is " +
                                std::string(workflow[static_cast<std::size_t>(parent_index + 1)].label);
                        }
                    }

                    if(workflow_error.empty())
                    {
                        if(current_index == 0)
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
                            batch.current_stage = stage_for_role(user->role);
                            batch.status = current_index == static_cast<int>(workflow.size()) - 1
                                ? "completed"
                                : "in_progress";
                        }
                        else
                        {
                            batch = *stored_batch;
                        }
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
                            database_record.stage = stage_for_role(user->role);
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
                            database_record.chain_status =
                                current_index == static_cast<int>(workflow.size()) - 1
                                    ? "completed"
                                    : "in_progress";
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
                                       json_escape(
                                           current_index + 1 < static_cast<int>(workflow.size())
                                               ? workflow[static_cast<std::size_t>(current_index + 1)].role
                                               : "") +
                                       "\",\"ipfsCount\":" +
                                       std::to_string(references.size()) +
                                       ",\"signatureVerified\":" +
                                       (database_record.signature_verified ? "true" : "false") + "}";
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
