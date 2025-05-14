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

// Safe send/receive helpers
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
// Function to convert bytes from network byte order to host byte order (Big Endian to Host Endian)
uint64_t be64toh(uint64_t x)
{
    uint32_t hi = ntohl((uint32_t)(x >> 32));
    uint32_t lo = ntohl((uint32_t)(x & 0xFFFFFFFFULL));
    return ((uint64_t)lo << 32) | hi;
}

// Function to create directory if it does not exist
void create_directory_if_not_exists(const std::string &dir)
{
    struct stat info;
    if (stat(dir.c_str(), &info) != 0 || !(info.st_mode & S_IFDIR))
    {
        if (mkdir(dir.c_str(), 0777) != 0)
        {
            std::cerr << "❌ Failed to create directory: " << dir << std::endl;
        }
        else
        {
            std::cout << "📂 Directory created: " << dir << std::endl;
        }
    }
}

// Helper function to create full directory structure
void create_directory_structure(const std::string &filepath)
{
    size_t pos = 0;
    std::string dir;
    while ((pos = filepath.find('/', pos + 1)) != std::string::npos)
    {
        dir = filepath.substr(0, pos);
        create_directory_if_not_exists(dir);
    }
}

// Add a helper function to get the basename of a file
std::string get_basename(const std::string &filepath)
{
    size_t pos = filepath.find_last_of("/\\");
    if (pos == std::string::npos)
        return filepath;
    return filepath.substr(pos + 1);
}

// Function to count unique users in a JSON file
void count_unique_users_json(const std::string &filename, const std::string &count_type)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "❌ Failed to open JSON file: " << filename << std::endl;
        return;
    }

    json j;
    file >> j;

    std::map<std::string, int> counter;
    for (const auto &entry : j)
    {
        if (count_type == "user")
        {
            std::string user_id = std::to_string(entry["user_id"].get<int>());
            counter[user_id]++;
        }
        else if (count_type == "ip")
        {
            std::string ip = entry["ip_address"].get<std::string>();
            counter[ip]++;
        }
    }

    std::string result_filename = "result_test_clients/" + get_basename(filename);
    create_directory_structure(result_filename);

    json result;
    result["type"] = count_type;
    result[count_type == "user" ? "users" : "ips"] = counter;

    std::ofstream result_file(result_filename);
    if (!result_file.is_open())
    {
        std::cerr << "❌ Failed to open result file: " << result_filename << std::endl;
        return;
    }

    result_file << result.dump(4);
    result_file.close();
    std::cout << "✅ Result saved to " << result_filename << std::endl;
}

// Function to count unique users in an XML file
void count_unique_users_xml(const std::string &filename, const std::string &count_type)
{
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(filename.c_str()) != tinyxml2::XML_SUCCESS)
    {
        std::cerr << "❌ Failed to open XML file: " << filename << std::endl;
        return;
    }

    tinyxml2::XMLElement *root = doc.RootElement();
    std::map<std::string, int> counter;

    for (tinyxml2::XMLElement *logElement = root->FirstChildElement("log"); logElement != nullptr; logElement = logElement->NextSiblingElement("log"))
    {
        if (count_type == "user")
        {
            auto *userElement = logElement->FirstChildElement("user_id");
            if (userElement && userElement->GetText())
            {
                std::string user_id = userElement->GetText();
                counter[user_id]++;
            }
        }
        else if (count_type == "ip")
        {
            auto *ipElement = logElement->FirstChildElement("ip_address");
            if (ipElement && ipElement->GetText())
            {
                std::string ip = ipElement->GetText();
                counter[ip]++;
            }
        }
    }

    std::string result_filename = "result_test_clients/" + get_basename(filename);

    create_directory_structure(result_filename);

    std::ofstream result_file(result_filename);
    if (!result_file.is_open())
    {
        std::cerr << "❌ Failed to open result file: " << result_filename << std::endl;
        return;
    }

    result_file << "<result>\n  <type>" << count_type << "</type>\n";
    for (const auto &[key, value] : counter)
    {
        result_file << "  <entry><id>" << key << "</id><count>" << value << "</count></entry>\n";
    }
    result_file << "</result>";

    result_file.close();
    std::cout << "✅ Result saved to " << result_filename << std::endl;
}

// Function to count unique users in a TXT file
void count_unique_users_txt(const std::string &filename, const std::string &count_type)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "❌ Failed to open TXT file: " << filename << std::endl;
        return;
    }

    std::map<std::string, int> counter;
    std::string line;
    std::regex user_id_regex(R"(UserID:\s*(\d+))");
    std::regex ip_regex(R"(IP:\s*([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+))");

    while (std::getline(file, line))
    {
        std::smatch match;
        if (count_type == "user" && std::regex_search(line, match, user_id_regex))
        {
            counter[match[1].str()]++;
        }
        else if (count_type == "ip" && std::regex_search(line, match, ip_regex))
        {
            counter[match[1].str()]++;
        }
    }

    std::string result_filename = "result_test_clients/" + get_basename(filename);

    create_directory_structure(result_filename);

    std::ofstream result_file(result_filename);
    if (!result_file.is_open())
    {
        std::cerr << "❌ Failed to open result file: " << result_filename << std::endl;
        return;
    }

    result_file << "Result (type: " << count_type << "):\n";
    for (const auto &[key, value] : counter)
    {
        result_file << key << ": " << value << "\n";
    }

    result_file.close();
    std::cout << "✅ Result saved to " << result_filename << std::endl;
}

// Function to handle each client connection
void handle_client(int client_socket)
{
    try
    {
        while (true)
        {
            // Receive count type
            uint32_t count_type_len_net;
            if (recv_all(client_socket, &count_type_len_net, sizeof(count_type_len_net)) <= 0)
            {
                std::cout << "🔌 Client disconnected during count type\n";
                break;
            }

            uint32_t count_type_len = ntohl(count_type_len_net);
            std::vector<char> count_type_buf(count_type_len + 1);
            if (recv_all(client_socket, count_type_buf.data(), count_type_len) <= 0)
            {
                std::cerr << "❌ Failed to receive count type\n";
                break;
            }
            count_type_buf[count_type_len] = '\0';
            std::string count_type(count_type_buf.data());

            std::cout << "📌 Count type: " << count_type << std::endl;

            // Receive file count
            uint32_t file_count_net;
            if (recv(client_socket, &file_count_net, sizeof(file_count_net), 0) <= 0)
            {
                std::cerr << "❌ Failed to receive file count.\n";
                break;
            }

            uint32_t file_count = ntohl(file_count_net);
            std::cout << "📥 Receiving " << file_count << " files...\n";

            std::vector<std::string> result_files;
            create_directory_if_not_exists("result_test_clients");

            for (uint32_t i = 0; i < file_count; ++i)
            {
                // Receive filename
                uint32_t filename_size_net;
                if (recv(client_socket, &filename_size_net, sizeof(filename_size_net), 0) <= 0)
                {
                    std::cerr << "❌ Failed to receive filename size.\n";
                    break;
                }
                uint32_t filename_size = ntohl(filename_size_net);

                std::vector<char> filename_buf(filename_size + 1);
                if (recv(client_socket, filename_buf.data(), filename_size, 0) <= 0)
                {
                    std::cerr << "❌ Failed to receive filename.\n";
                    break;
                }
                filename_buf[filename_size] = '\0';
                std::string filename(filename_buf.data());
                // ✅ Clean it here
                std::string clean_filename = get_basename(filename);

                // Receive file data
                uint64_t file_size_net;
                if (recv(client_socket, &file_size_net, sizeof(file_size_net), 0) <= 0)
                {
                    std::cerr << "❌ Failed to receive file size.\n";
                    break;
                }
                uint64_t file_size = be64toh(file_size_net);

                std::vector<char> file_data(file_size);
                size_t received = 0;
                while (received < file_size)
                {
                    ssize_t bytes = recv(client_socket, file_data.data() + received, file_size - received, 0);
                    if (bytes <= 0)
                    {
                        std::cerr << "❌ Failed to receive file content.\n";
                        break;
                    }
                    received += bytes;
                }
                // Check if we received the expected number of bytes

                std::string file_content(file_data.begin(), file_data.end());
                std::string result_filename = "result_test_clients/" + clean_filename;

                // Create directory structure for the result file
                if (clean_filename.find(".json") != std::string::npos)
                {
                    std::istringstream iss(file_content);
                    json j;
                    iss >> j;
                    std::map<std::string, int> counter;
                    for (const auto &entry : j)
                    {
                        std::string key = (count_type == "user") ? std::to_string(entry["user_id"].get<int>()) : entry["ip_address"].get<std::string>();
                        counter[key]++;
                    }
                    json result;
                    result["type"] = count_type;
                    result[count_type == "user" ? "users" : "ips"] = counter;
                    std::ofstream result_file(result_filename);
                    result_file << result.dump(4);
                    result_file.close();
                }
                else if (clean_filename.find(".xml") != std::string::npos)
                {
                    tinyxml2::XMLDocument doc;
                    doc.Parse(file_content.c_str());
                    tinyxml2::XMLElement *root = doc.RootElement();
                    std::map<std::string, int> counter;
                    for (tinyxml2::XMLElement *logElement = root->FirstChildElement("log"); logElement != nullptr; logElement = logElement->NextSiblingElement("log"))
                    {
                        std::string key;
                        if (count_type == "user")
                        {
                            auto *userElement = logElement->FirstChildElement("user_id");
                            if (userElement && userElement->GetText())
                                key = userElement->GetText();
                        }
                        else if (count_type == "ip")
                        {
                            auto *ipElement = logElement->FirstChildElement("ip_address");
                            if (ipElement && ipElement->GetText())
                                key = ipElement->GetText();
                        }
                        if (!key.empty())
                            counter[key]++;
                    }
                    std::ofstream result_file(result_filename);
                    result_file << "<result>\n  <type>" << count_type << "</type>\n";
                    for (const auto &[key, value] : counter)
                    {
                        result_file << "  <entry><id>" << key << "</id><count>" << value << "</count></entry>\n";
                    }
                    result_file << "</result>";
                    result_file.close();
                }
                else if (clean_filename.find(".txt") != std::string::npos)
                {
                    std::istringstream iss(file_content);
                    std::string line;
                    std::map<std::string, int> counter;
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
                    std::ofstream result_file(result_filename);
                    result_file << "Result (type: " << count_type << "):\n";
                    for (const auto &[key, value] : counter)
                    {
                        result_file << key << ": " << value << "\n";
                    }
                    result_file.close();
                }

                result_files.push_back(result_filename);
                std::cout << "✅ Processed and saved result file: " << result_filename << std::endl;
            }

            // Send number of result files
            uint32_t result_file_count_net = htonl(result_files.size());
            send(client_socket, &result_file_count_net, sizeof(result_file_count_net), 0);

            // Send each result file
            for (const auto &result_file : result_files)
            {
                std::cout << "📤 Sending result file: " << result_file << " to client\n";

                uint32_t filename_len = result_file.length();
                uint32_t filename_len_net = htonl(filename_len);
                send(client_socket, &filename_len_net, sizeof(filename_len_net), 0);
                send(client_socket, result_file.c_str(), filename_len, 0);

                std::ifstream infile(result_file, std::ios::binary);
                if (!infile)
                {
                    std::cerr << "❌ Failed to open result file: " << result_file << std::endl;
                    continue;
                }

                infile.seekg(0, std::ios::end);
                uint64_t result_file_size = infile.tellg();
                infile.seekg(0, std::ios::beg);
                uint64_t result_file_size_net = htobe64(result_file_size);
                send(client_socket, &result_file_size_net, sizeof(result_file_size_net), 0);

                char file_buffer[BUFFER_SIZE];
                while (infile.read(file_buffer, sizeof(file_buffer)))
                {
                    ssize_t bytes_to_send = infile.gcount();
                    ssize_t sent = send(client_socket, file_buffer, bytes_to_send, MSG_NOSIGNAL);
                    if (sent == -1)
                    {
                        perror("❌ Failed to send file data");
                        break;
                    }
                }

                if (infile.gcount() > 0)
                {
                    ssize_t bytes_to_send = infile.gcount();
                    ssize_t sent = send(client_socket, file_buffer, bytes_to_send, MSG_NOSIGNAL);
                    if (sent == -1)
                    {
                        perror("❌ Failed to send remaining data");
                    }
                }

                infile.close();
                std::cout << "✅ Sent result file: " << result_file << " to client\n";

                infile.close();
                std::cout << "✅ Sent result file: " << result_file << " to client\n";
            }
        }
    }
    catch (const std::exception &ex)
    {
        std::cerr << "❌ Exception in client handler: " << ex.what() << "\n";
    }
    catch (...)
    {
        std::cerr << "❌ Unknown exception in client handler.\n";
    }

    close(client_socket);
    std::cout << "🔌 Client connection closed.\n";
}

int main()
{
    // Create socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        std::cerr << "❌ Failed to create socket: " << strerror(errno) << std::endl;
        return 1;
    }

    // Set socket options
    int optval = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#ifdef SO_NOSIGPIPE
    setsockopt(server_socket, SOL_SOCKET, SO_NOSIGPIPE, &optval, sizeof(optval));
#endif
    // Step 1: Create a socket
    
    if (server_socket == -1)
    {
        std::cerr << "❌ Failed to create socket" << std::endl;
        return 1;
    }

    // Step 2: Set up the server address
    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

    // Set socket options to reuse address
    int reuse = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
    {
        std::cerr << "❌ Failed to set socket options" << std::endl;
        close(server_socket);
        return 1;
    }

    // Step 3: Bind the socket to the address
    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) == -1)
    {
        std::cerr << "❌ Failed to bind socket to address" << std::endl;
        close(server_socket);
        return 1;
    }

    // Step 4: Start listening for incoming connections
    if (listen(server_socket, 10) == -1)
    {
        std::cerr << "❌ Failed to listen on socket" << std::endl;
        close(server_socket);
        return 1;
    }

    std::cout << "🚀 Server is listening on port " << SERVER_PORT << "..." << std::endl;

    // Step 5: Main loop to accept and handle client connections
    while (true)
    {
        // Accept a new client connection
        int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket == -1)
        {
            std::cerr << "❌ Failed to accept client connection" << std::endl;
            continue;
        }

        std::cout << "✔️ Client connected!" << std::endl;

        // Handle the client in a separate thread
        std::thread client_thread(handle_client, client_socket);
        client_thread.detach();
    }

    // Cleanup (though this won't be reached in this infinite loop)
    close(server_socket);
    return 0;
}
