#include <arpa/inet.h>
#include <algorithm>
#include <cctype>
#include <csignal>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "auth_utils.hpp"
#include "db_utils.hpp"
#include "MerkleTree.hpp"
#include "log_utils.hpp"

namespace fs = std::filesystem;

#ifndef CONTROL_DEFAULT_STATIC_DIR
#define CONTROL_DEFAULT_STATIC_DIR "../control_static"
#endif

#ifndef CONTROL_DEFAULT_DATABASE_PATH
#define CONTROL_DEFAULT_DATABASE_PATH "../../Database/supply_chain.db"
#endif

namespace
{
constexpr std::size_t MAX_HEADER_SIZE = 64 * 1024;
constexpr std::size_t MAX_BODY_SIZE = 64 * 1024;
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

std::optional<UserAccount> authenticated_user(const HttpRequest& request,
                                              SessionStore& sessions)
{
    const auto authorization = request.headers.find("authorization");
    if(authorization == request.headers.end()) return std::nullopt;

    const std::string header = authorization->second;
    if(header.size() < 7 || lower(header.substr(0, 7)) != "bearer ")
        return std::nullopt;

    const std::string token = trim(header.substr(7));
    const auto session = sessions.find(token);
    if(session == sessions.end()) return std::nullopt;

    if(session->second.expires_at <= std::chrono::steady_clock::now())
    {
        sessions.erase(session);
        return std::nullopt;
    }

    return session->second.account;
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

std::string canonical_record(const std::unordered_map<std::string, std::string>& fields,
                             const UserAccount& account)
{
    const char* names[] = {"batchId", "product", "origin", "stage"};
    std::ostringstream record;
    for(const char* name : names)
    {
        const std::string& value = fields.at(name);
        record << name << ':' << value.size() << ':' << value << '\n';
    }
    record << "confirmedBy:" << account.username.size() << ':'
           << account.username << '\n';
    record << "uid:" << account.uid.size() << ':' << account.uid << '\n';
    record << "role:" << account.role.size() << ':' << account.role << '\n';
    record << "organizationId:" << account.organization_id.size() << ':'
           << account.organization_id << '\n';
    return record.str();
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
           "\"}";
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
             << "\",\"origin\":\"" << json_escape(record.origin)
             << "\",\"stage\":\"" << json_escape(record.stage)
             << "\",\"confirmedBy\":\"" << json_escape(record.confirmed_by)
             << "\",\"uid\":\"" << json_escape(record.uid)
             << "\",\"role\":\"" << json_escape(record.role)
             << "\",\"organizationId\":\""
             << json_escape(record.organization_id)
             << "\",\"rootHash\":\"" << json_escape(record.root_hash)
             << "\",\"proof\":\"" << json_escape(record.proof)
             << "\",\"verified\":" << (record.verified ? "true" : "false")
             << ",\"createdAt\":\"" << json_escape(record.created_at) << "\"}";
    }
    json << ']';
    return json.str();
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

    MerkleTree merkle_tree(1);
    std::vector<std::string> canonical_records;
    canonical_records.reserve(stored_records.size());
    for(const SupplyChainRecord& stored_record : stored_records)
    {
        if(stored_record.block_id != merkle_tree.GetBlockCount() ||
           !merkle_tree.Append(stored_record.canonical_record))
        {
            std::cerr << "Cannot rebuild Merkle Tree at stored block "
                      << stored_record.block_id << '\n';
            return 1;
        }

        canonical_records.push_back(stored_record.canonical_record);
    }

    SessionStore sessions;

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

    std::cout << "Control server: http://127.0.0.1:" << port << '\n'
              << "Static root: " << static_root << '\n'
              << "Database: " << database_path << '\n'
              << "Restored Merkle blocks: " << canonical_records.size() << '\n';

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

        const bool is_api_target =
            request.target == "/api/auth/login" ||
            request.target == "/api/auth/me" ||
            request.target == "/api/records";

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
                    const std::string token = generate_random_hex(32);
                    sessions[token] = Session{
                        *account,
                        std::chrono::steady_clock::now() + std::chrono::hours(8)
                    };
                    status = "200 OK";
                    body = "{\"token\":\"" + json_escape(token) +
                           "\",\"user\":" + user_json(*account) + "}";
                }
            }
            send_response(client_fd, status, content_type, body, true, CORS_HEADERS);
        }
        else if(request.method == "GET" && request.target == "/api/auth/me")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(request, sessions);
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
        else if(request.method == "GET" && request.target == "/api/records")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(request, sessions);
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
        else if(request.method == "POST" && request.target == "/api/records")
        {
            content_type = "application/json; charset=utf-8";
            const auto user = authenticated_user(request, sessions);
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
                const auto fields = correct_type ? parse_form(request.body) : std::nullopt;

                const char* required[] = {
                    "batchId", "product", "origin", "stage"
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
                    const int block_id = static_cast<int>(canonical_records.size());
                    const std::string record = canonical_record(*fields, *user);
                    MerkleTree candidate_tree(1);
                    bool candidate_ready = true;
                    for(const std::string& stored_record : canonical_records)
                    {
                        if(!candidate_tree.Append(stored_record))
                        {
                            candidate_ready = false;
                            break;
                        }
                    }

                    if(candidate_ready) candidate_ready = candidate_tree.Append(record);
                    if(!candidate_ready)
                    {
                        status = "500 Internal Server Error";
                        body = json_error("Failed to append Merkle block");
                    }
                    else
                    {
                        const std::string proof = candidate_tree.ProverBlock(block_id);
                        const bool verified = candidate_tree.Verify(proof);

                        SupplyChainRecord database_record;
                        database_record.block_id = block_id;
                        database_record.batch_id = fields->at("batchId");
                        database_record.product = fields->at("product");
                        database_record.origin = fields->at("origin");
                        database_record.stage = fields->at("stage");
                        database_record.confirmed_by = user->username;
                        database_record.uid = user->uid;
                        database_record.role = user->role;
                        database_record.organization_id = user->organization_id;
                        database_record.canonical_record = record;
                        database_record.root_hash = candidate_tree.GetRootHash();
                        database_record.proof = proof;
                        database_record.verified = verified;

                        if(!insert_supply_chain_record(database_path.string(), database_record))
                        {
                            status = "500 Internal Server Error";
                            body = json_error("Failed to save supply-chain record");
                        }
                        else if(!merkle_tree.Append(record))
                        {
                            status = "500 Internal Server Error";
                            body = json_error(
                                "Record saved, but in-memory Merkle update failed");
                        }
                        else
                        {
                            canonical_records.push_back(record);
                            status = "201 Created";
                            body = "{\"blockID\":" + std::to_string(block_id) +
                                   ",\"verified\":" +
                                   (verified ? "true" : "false") + "}";
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
}
