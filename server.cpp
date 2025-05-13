#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <set>
#include <map>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <tinyxml2.h>

#ifdef __linux__
#include <endian.h>
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define be64toh(x) OSSwapBigToHostInt64(x)
#endif

#include <nlohmann/json.hpp>
using json = nlohmann::json;
using namespace tinyxml2;

#define PORT 12345
#define BUFFER_SIZE 4096
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define be64toh(x) OSSwapBigToHostInt64(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#elif defined(__linux__)
#include <endian.h>
#else
// Fallback if not defined
#include <cstdint>
static inline uint64_t htobe64(uint64_t x)
{
    uint32_t hi = htonl((uint32_t)(x >> 32));
    uint32_t lo = htonl((uint32_t)(x & 0xFFFFFFFFULL));
    return ((uint64_t)hi << 32) | lo;
}
#endif

// --- Function Declarations ---
void count_unique_users_json(const std::string &filename);
void count_unique_users_txt(const std::string &filename);
void count_unique_users_xml(const std::string &filename);

// --- Function Definitions ---
void count_unique_users_json(const std::string &filename)
{
    std::ifstream infile(filename);
    if (!infile.is_open())
    {
        std::cerr << "❌ Failed to open JSON file: " << filename << "\n";
        return;
    }

    try
    {
        json logs;
        infile >> logs;
        std::map<std::string, std::set<int>> ip_to_users;

        for (const auto &entry : logs)
        {
            if (entry.contains("ip_address") && entry.contains("user_id") &&
                entry["ip_address"].is_string() && entry["user_id"].is_number_integer())
            {
                ip_to_users[entry["ip_address"]].insert(entry["user_id"].get<int>());
            }
        }

        std::string base_filename = filename.substr(filename.find_last_of("/\\") + 1);
        std::string result_filename = "result_" + base_filename + ".json";
        std::ofstream result_out(result_filename);
        if (result_out.is_open())
        {
            json result;
            for (const auto &[ip, users] : ip_to_users)
            {
                result[ip] = users.size();
            }
            result_out << result.dump(4);
            result_out.close();
            std::cout << "📝 JSON result written to " << result_filename << "\n";
        }
        else
        {
            std::cerr << "❌ Failed to write result file for: " << filename << "\n";
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "❌ JSON parse error: " << e.what() << "\n";
    }
}

void count_unique_users_txt(const std::string &filename)
{
    std::ifstream infile(filename);
    if (!infile.is_open())
    {
        std::cerr << "❌ Failed to open TXT file: " << filename << "\n";
        return;
    }

    std::map<std::string, std::set<int>> ip_to_users;
    std::string line;

    while (std::getline(infile, line))
    {
        size_t user_pos = line.find("UserID:");
        size_t ip_pos = line.find("IP:");
        if (user_pos != std::string::npos && ip_pos != std::string::npos)
        {
            try
            {
                int user_id = std::stoi(line.substr(user_pos + 7, ip_pos - user_pos - 7));
                std::string ip = line.substr(ip_pos + 3);
                ip.erase(0, ip.find_first_not_of(" \r\n"));
                ip.erase(ip.find_last_not_of(" \r\n") + 1);
                ip_to_users[ip].insert(user_id);
            }
            catch (const std::exception &e)
            {
                std::cerr << "❌ Parse error: " << e.what() << "\n";
            }
        }
    }

    std::string base_filename = filename.substr(filename.find_last_of("/\\") + 1);
    std::string result_filename = "result_" + base_filename + ".json";
    std::ofstream out(result_filename);
    if (out.is_open())
    {
        json result;
        for (const auto &[ip, users] : ip_to_users)
        {
            result[ip] = users.size();
        }
        out << result.dump(4);
        out.close();
        std::cout << "📝 TXT result written to " << result_filename << "\n";
    }
    else
    {
        std::cerr << "❌ Failed to write result file for: " << filename << "\n";
    }
}

void count_unique_users_xml(const std::string &filename)
{
    XMLDocument doc;
    if (doc.LoadFile(filename.c_str()) != XML_SUCCESS)
    {
        std::cerr << "❌ Failed to open XML file: " << filename << "\n";
        return;
    }

    std::map<std::string, std::set<int>> ip_to_users;
    XMLElement *root = doc.RootElement();
    if (!root)
    {
        std::cerr << "❌ Invalid XML: Missing <logs> root\n";
        return;
    }

    for (XMLElement *log = root->FirstChildElement("log"); log; log = log->NextSiblingElement("log"))
    {
        const char *ip = log->FirstChildElement("ip_address") ? log->FirstChildElement("ip_address")->GetText() : nullptr;
        const char *uid = log->FirstChildElement("user_id") ? log->FirstChildElement("user_id")->GetText() : nullptr;
        if (ip && uid)
        {
            try
            {
                ip_to_users[ip].insert(std::stoi(uid));
            }
            catch (const std::exception &e)
            {
                std::cerr << "❌ Invalid user_id: " << e.what() << "\n";
            }
        }
    }

    std::string base_filename = filename.substr(filename.find_last_of("/\\") + 1);
    std::string result_filename = "result_" + base_filename + ".json";
    std::ofstream out(result_filename);
    if (out.is_open())
    {
        json result;
        for (const auto &[ip, users] : ip_to_users)
        {
            result[ip] = users.size();
        }
        out << result.dump(4);
        out.close();
        std::cout << "📝 XML result written to " << result_filename << "\n";
    }
    else
    {
        std::cerr << "❌ Failed to write result file for: " << filename << "\n";
    }
}

// --- Client Handler ---
void handle_client(int client_socket)
{
    char buffer[BUFFER_SIZE];

    // Step 1: Receive number of files
    uint32_t file_count_net;
    if (recv(client_socket, &file_count_net, sizeof(file_count_net), 0) != sizeof(file_count_net))
    {
        std::cerr << "❌ Failed to receive file count.\n";
        close(client_socket);
        return;
    }

    uint32_t file_count = ntohl(file_count_net);
    std::cout << "📥 Receiving " << file_count << " files...\n";
    std::vector<std::string> result_files;

    std::string overall_response;

    for (uint32_t i = 0; i < file_count; ++i)
    {
        // Step 2: Receive filename
        uint32_t filename_size_net;
        recv(client_socket, &filename_size_net, sizeof(filename_size_net), 0);
        uint32_t filename_size = ntohl(filename_size_net);

        std::vector<char> filename(filename_size + 1);
        recv(client_socket, filename.data(), filename_size, 0);
        filename[filename_size] = '\0';

        // Step 3: Receive file size and contents
        uint64_t file_size_net;
        recv(client_socket, &file_size_net, sizeof(file_size_net), 0);
        uint64_t file_size = be64toh(file_size_net);

        std::vector<char> file_data(file_size);
        uint64_t received = 0;
        while (received < file_size)
        {
            ssize_t bytes = recv(client_socket, buffer, BUFFER_SIZE, 0);
            if (bytes <= 0)
                break;
            memcpy(file_data.data() + received, buffer, bytes); // Don't use std::memcpy
            received += bytes;
        }

        std::string file_str(filename.data());

        // Step 4: Save file
        std::ofstream file_out(file_str, std::ios::binary);
        if (!file_out.is_open())
        {
            overall_response += "❌ Failed to save: " + file_str + "\n";
            continue;
        }
        file_out.write(file_data.data(), file_data.size());
        file_out.close();
        std::cout << "📦 Received file: " << file_str << "\n";

        // Step 5: Process based on file extension
        std::string ext = file_str.substr(file_str.find_last_of('.') + 1);
        if (ext == "json")
        {
            try
            {
                count_unique_users_json(file_str);
                std::string base_filename = file_str.substr(file_str.find_last_of("/\\") + 1);
                result_files.push_back("result_" + base_filename + ".json");
                overall_response += "✅ Processed JSON: " + file_str + "\n";
            }
            catch (...)
            {
                overall_response += "❌ Failed to process JSON: " + file_str + "\n";
            }
        }
        else if (ext == "xml")
        {
            try
            {
                count_unique_users_xml(file_str);
                std::string base_filename = file_str.substr(file_str.find_last_of("/\\") + 1);
                result_files.push_back("result_" + base_filename + ".json");
                overall_response += "✅ Processed XML: " + file_str + "\n";
            }
            catch (...)
            {
                overall_response += "❌ Failed to process XML: " + file_str + "\n";
            }
        }
        else if (ext == "txt")
        {
            try
            {
                count_unique_users_txt(file_str);
                std::string base_filename = file_str.substr(file_str.find_last_of("/\\") + 1);
                result_files.push_back("result_" + base_filename + ".json");
                overall_response += "✅ Processed TXT: " + file_str + "\n";
            }
            catch (...)
            {
                overall_response += "❌ Failed to process TXT: " + file_str + "\n";
            }
        }
        else
        {
            overall_response += "❌ Unsupported file type: " + file_str + "\n";
        }
    }

    // Step 6: Send final summary back to client
    uint32_t response_size = htonl(overall_response.size());
    send(client_socket, &response_size, sizeof(response_size), 0);
    send(client_socket, overall_response.c_str(), overall_response.size(), 0);

    

    // Send how many result files are coming
    uint32_t result_count = result_files.size();
    uint32_t result_count_net = htonl(result_count);
    send(client_socket, &result_count_net, sizeof(result_count_net), 0);

    for (const std::string &res_file : result_files)
    {
        std::ifstream in(res_file, std::ios::binary);
        if (!in.is_open())
            continue;

        in.seekg(0, std::ios::end);
        uint64_t size = in.tellg();
        in.seekg(0);

        // Send filename
        uint32_t fname_len = htonl(res_file.length());
        send(client_socket, &fname_len, sizeof(fname_len), 0);
        send(client_socket, res_file.c_str(), res_file.length(), 0);

        // Send file size
        uint64_t size_net = htobe64(size);
        send(client_socket, &size_net, sizeof(size_net), 0);

        // Send file content
        char buffer[BUFFER_SIZE];
        while (!in.eof())
        {
            in.read(buffer, BUFFER_SIZE);
            std::streamsize bytes = in.gcount();
            send(client_socket, buffer, bytes, 0);
        }

        in.close();
    }

    close(client_socket);
}

// --- Server Entry Point ---
int main()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        perror("❌ socket creation failed");
        return 1;
    }

    sockaddr_in address{};
    socklen_t addrlen = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("❌ bind failed");
        return 1;
    }

    if (listen(server_fd, 10) < 0)
    {
        perror("❌ listen failed");
        return 1;
    }

    std::cout << "🚀 Server listening on port " << PORT << "\n";

    while (true)
    {
        int client_socket = accept(server_fd, (sockaddr *)&address, &addrlen);
        if (client_socket < 0)
        {
            perror("❌ accept failed");
            continue;
        }

        std::thread([client_socket]()
                    { handle_client(client_socket); })
            .detach();
    }

    close(server_fd);
    return 0;
}

