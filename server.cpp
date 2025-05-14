#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <map>
#include <cstdint>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <nlohmann/json.hpp>
#include <tinyxml2.h>
#include <regex>
#include <sstream>

#if defined(__APPLE__) || defined(__linux__)
static inline uint64_t htobe64(uint64_t x)
{
    uint32_t hi = htonl((uint32_t)(x >> 32));
    uint32_t lo = htonl((uint32_t)(x & 0xFFFFFFFFULL));
    return ((uint64_t)lo << 32) | hi;
}
#endif

#define SERVER_PORT 12345
#define BUFFER_SIZE 4096

using json = nlohmann::json;

// === Helper Functions ===

ssize_t send_all(int sockfd, const void *buf, size_t len, int flags = 0)
{
    size_t total_sent = 0;
    while (total_sent < len)
    {
        ssize_t sent = send(sockfd, (const char *)buf + total_sent, len - total_sent, flags | MSG_NOSIGNAL);
        if (sent == -1)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (sent == 0)
            return 0;
        total_sent += sent;
    }
    return total_sent;
}

ssize_t recv_all(int sockfd, void *buf, size_t len, int flags = 0)
{
    size_t total_received = 0;
    while (total_received < len)
    {
        ssize_t received = recv(sockfd, (char *)buf + total_received, len - total_received, flags);
        if (received == -1)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (received == 0)
            return 0;
        total_received += received;
    }
    return total_received;
}

uint64_t be64toh(uint64_t x)
{
    uint32_t hi = ntohl((uint32_t)(x >> 32));
    uint32_t lo = ntohl((uint32_t)(x & 0xFFFFFFFFULL));
    return ((uint64_t)lo << 32) | hi;
}

void create_directory_if_not_exists(const std::string &dir)
{
    struct stat info;
    if (stat(dir.c_str(), &info) != 0 || !(info.st_mode & S_IFDIR))
    {
        if (mkdir(dir.c_str(), 0777) != 0)
            std::cerr << "❌ Failed to create directory: " << dir << std::endl;
        else
            std::cout << "📂 Directory created: " << dir << std::endl;
    }
}

std::string get_basename(const std::string &filepath)
{
    size_t pos = filepath.find_last_of("/\\");
    return (pos == std::string::npos) ? filepath : filepath.substr(pos + 1);
}

// === Core File Processing Logic ===

bool ends_with(const std::string &str, const std::string &suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}


void process_json_file(const std::string &file_content, const std::string &count_type, const std::string &output_filename)
{
    json j = json::parse(file_content);
    std::map<std::string, int> counter;

    for (const auto &entry : j)
    {
        std::string key = (count_type == "user") ? std::to_string(entry["user_id"].get<int>()) : entry["ip_address"].get<std::string>();
        counter[key]++;
    }

    json result;
    result["type"] = count_type;
    result[(count_type == "user") ? "users" : "ips"] = counter;

    std::ofstream out(output_filename);
    out << result.dump(4);
}

void process_xml_file(const std::string &file_content, const std::string &count_type, const std::string &output_filename)
{
    tinyxml2::XMLDocument doc;
    doc.Parse(file_content.c_str());

    std::map<std::string, int> counter;
    tinyxml2::XMLElement *root = doc.RootElement();
    for (tinyxml2::XMLElement *log = root->FirstChildElement("log"); log; log = log->NextSiblingElement("log"))
    {
        const char *key = nullptr;
        if (count_type == "user")
            key = log->FirstChildElement("user_id") ? log->FirstChildElement("user_id")->GetText() : nullptr;
        else
            key = log->FirstChildElement("ip_address") ? log->FirstChildElement("ip_address")->GetText() : nullptr;

        if (key)
            counter[key]++;
    }

    std::ofstream out(output_filename);
    out << "<result>\n  <type>" << count_type << "</type>\n";
    for (const auto &[key, value] : counter)
        out << "  <entry><id>" << key << "</id><count>" << value << "</count></entry>\n";
    out << "</result>";
}

void process_txt_file(const std::string &file_content, const std::string &count_type, const std::string &output_filename)
{
    std::map<std::string, int> counter;
    std::istringstream iss(file_content);
    std::string line;
    std::regex user_id_regex(R"(UserID:\s*(\d+))");
    std::regex ip_regex(R"(IP:\s*([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+))");

    while (std::getline(iss, line))
    {
        std::smatch match;
        if (count_type == "user" && std::regex_search(line, match, user_id_regex))
            counter[match[1].str()]++;
        else if (count_type == "ip" && std::regex_search(line, match, ip_regex))
            counter[match[1].str()]++;
    }

    std::ofstream out(output_filename);
    out << "Result (type: " << count_type << "):\n";
    for (const auto &[key, value] : counter)
        out << key << ": " << value << "\n";
}

// === Client Handler ===

void handle_client(int client_socket)
{
    try
    {
        while (true)
        {
            uint32_t count_type_len_net;
            if (recv_all(client_socket, &count_type_len_net, sizeof(count_type_len_net)) <= 0) break;
            uint32_t count_type_len = ntohl(count_type_len_net);

            std::vector<char> type_buf(count_type_len + 1);
            if (recv_all(client_socket, type_buf.data(), count_type_len) <= 0) break;
            type_buf[count_type_len] = '\0';
            std::string count_type(type_buf.data());

            uint32_t file_count_net;
            if (recv(client_socket, &file_count_net, sizeof(file_count_net), 0) <= 0) break;
            uint32_t file_count = ntohl(file_count_net);

            std::vector<std::string> result_files;
            create_directory_if_not_exists("result_test_clients");

            for (uint32_t i = 0; i < file_count; ++i)
            {
                uint32_t name_size_net;
                if (recv(client_socket, &name_size_net, sizeof(name_size_net), 0) <= 0) break;
                uint32_t name_size = ntohl(name_size_net);

                std::vector<char> name_buf(name_size + 1);
                if (recv(client_socket, name_buf.data(), name_size, 0) <= 0) break;
                name_buf[name_size] = '\0';
                std::string clean_name = get_basename(std::string(name_buf.data()));

                uint64_t size_net;
                if (recv(client_socket, &size_net, sizeof(size_net), 0) <= 0) break;
                uint64_t file_size = be64toh(size_net);

                std::vector<char> data(file_size);
                if (recv_all(client_socket, data.data(), file_size) <= 0) break;

                std::string content(data.begin(), data.end());
                std::string result_file = "result_test_clients/" + clean_name;

                if (ends_with(clean_name, ".json"))
                    process_json_file(content, count_type, result_file);
                else if (ends_with(clean_name, ".xml"))
                    process_xml_file(content, count_type, result_file);
                else if (ends_with(clean_name, ".txt"))
                    process_txt_file(content, count_type, result_file);

                result_files.push_back(result_file);
                std::cout << "✅ Processed file: " << clean_name << std::endl;
            }

            // Send results
            uint32_t result_count_net = htonl(result_files.size());
            send(client_socket, &result_count_net, sizeof(result_count_net), 0);

            for (const auto &path : result_files)
            {
                uint32_t fname_len = path.length();
                uint32_t fname_len_net = htonl(fname_len);
                send(client_socket, &fname_len_net, sizeof(fname_len_net), 0);
                send_all(client_socket, path.c_str(), fname_len);

                std::ifstream infile(path, std::ios::binary);
                infile.seekg(0, std::ios::end);
                uint64_t fsize = infile.tellg();
                infile.seekg(0);
                uint64_t fsize_net = htobe64(fsize);
                send(client_socket, &fsize_net, sizeof(fsize_net), 0);

                char buffer[BUFFER_SIZE];
                while (infile.read(buffer, sizeof(buffer)))
                    send_all(client_socket, buffer, infile.gcount());
                if (infile.gcount() > 0)
                    send_all(client_socket, buffer, infile.gcount());
                infile.close();
            }
        }
    }
    catch (...)
    {
        std::cerr << "❌ Error in client thread\n";
    }

    close(client_socket);
    std::cout << "🔌 Client disconnected.\n";
}

// === Main Server Loop ===

int main()
{
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        std::cerr << "❌ Socket creation failed\n";
        return 1;
    }

    int optval = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#ifdef SO_NOSIGPIPE
    setsockopt(server_socket, SOL_SOCKET, SO_NOSIGPIPE, &optval, sizeof(optval));
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "❌ Bind failed\n";
        return 1;
    }

    if (listen(server_socket, 10) < 0)
    {
        std::cerr << "❌ Listen failed\n";
        return 1;
    }

    std::cout << "🚀 Server is listening on port " << SERVER_PORT << "\n";

    while (true)
    {
        int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket == -1)
        {
            std::cerr << "❌ Accept failed\n";
            continue;
        }
        std::cout << "✔️ Client connected!\n";
        std::thread(handle_client, client_socket).detach();
    }

    close(server_socket);
    return 0;
}
