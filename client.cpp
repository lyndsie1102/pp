#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htobe64(x) OSSwapHostToBigInt64(x)
#elif defined(__linux__)
#include <endian.h>
#else
// Fallback: define htobe64 manually if not available
#include <cstdint>
static inline uint64_t htobe64(uint64_t x)
{
    uint32_t hi = htonl((uint32_t)(x >> 32));
    uint32_t lo = htonl((uint32_t)(x & 0xFFFFFFFFULL));
    return ((uint64_t)lo << 32) | hi;
}
#endif
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htobe64(x) OSSwapHostToBigInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)  // ✅ Add this line
#elif defined(__linux__)
#include <endian.h>
#else
// Fallback for other platforms
#include <cstdint>
static inline uint64_t htobe64(uint64_t x)
{
    uint32_t hi = htonl((uint32_t)(x >> 32));
    uint32_t lo = htonl((uint32_t)(x & 0xFFFFFFFFULL));
    return ((uint64_t)lo << 32) | hi;
}

static inline uint64_t be64toh(uint64_t x)  // ✅ Add this too
{
    uint32_t hi = ntohl((uint32_t)(x >> 32));
    uint32_t lo = ntohl((uint32_t)(x & 0xFFFFFFFFULL));
    return ((uint64_t)lo << 32) | hi;
}
#endif

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define BUFFER_SIZE 4096

// Function to send a single file
void send_file(int sockfd, const std::string &filename)
{
    std::ifstream infile(filename, std::ios::binary);
    if (!infile)
    {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return;
    }

    // Get file size
    infile.seekg(0, std::ios::end);
    uint64_t file_size = infile.tellg();
    infile.seekg(0);

    // Send filename length and filename
    uint32_t filename_len = filename.length();
    uint32_t filename_len_net = htonl(filename_len);
    send(sockfd, &filename_len_net, sizeof(filename_len_net), 0);
    send(sockfd, filename.c_str(), filename_len, 0);

    // Send file size
    uint64_t file_size_net = htobe64(file_size);
    send(sockfd, &file_size_net, sizeof(file_size_net), 0);

    // Send file contents
    char buffer[BUFFER_SIZE];
    while (!infile.eof())
    {
        infile.read(buffer, BUFFER_SIZE);
        std::streamsize bytes = infile.gcount();
        send(sockfd, buffer, bytes, 0);
    }

    infile.close();
}

int main()
{
    // File names
    std::vector<std::string> filenames = {"test_clients/log_file.json", "test_clients/log_file.txt", "test_clients/log_file.xml"};

    // Create socket and connect to server
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sockfd, (sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("connect");
        return 1;
    }

    // Send number of files first
    uint32_t file_count = filenames.size();
    uint32_t file_count_net = htonl(file_count);
    send(sockfd, &file_count_net, sizeof(file_count_net), 0);

    // Send each file
    for (const auto &filename : filenames)
    {
        send_file(sockfd, filename);
    }

    // Immediately print "Files sent successfully"
    std::cout << "Files sent successfully" << std::endl;

    // Receive size of JSON response
    uint32_t response_size_net;
    recv(sockfd, &response_size_net, sizeof(response_size_net), 0);
    uint32_t response_size = ntohl(response_size_net);

    // Receive the JSON response
    std::vector<char> response_buffer(response_size + 1);
    size_t total_received = 0;
    while (total_received < response_size)
    {
        ssize_t bytes = recv(sockfd, response_buffer.data() + total_received, response_size - total_received, 0);
        if (bytes <= 0)
        {
            std::cerr << "Failed to receive full JSON response\n";
            break;
        }
        total_received += bytes;
    }
    response_buffer[response_size] = '\0';

    std::string json_str(response_buffer.data());
    std::cout << "✅ Server reports:\n"
              << json_str << std::endl;

              

    uint32_t result_file_count_net;
    recv(sockfd, &result_file_count_net, sizeof(result_file_count_net), 0);
    uint32_t result_file_count = ntohl(result_file_count_net);

    for (uint32_t i = 0; i < result_file_count; ++i)
    {
        // Receive filename size
        uint32_t filename_size_net;
        recv(sockfd, &filename_size_net, sizeof(filename_size_net), 0);
        uint32_t filename_size = ntohl(filename_size_net);

        // Receive filename
        std::vector<char> filename_buf(filename_size + 1);
        recv(sockfd, filename_buf.data(), filename_size, 0);
        filename_buf[filename_size] = '\0';
        std::string filename(filename_buf.data());

        // Receive file size
        uint64_t file_size_net;
        recv(sockfd, &file_size_net, sizeof(file_size_net), 0);
        uint64_t file_size = be64toh(file_size_net);

        // Receive file content
        std::vector<char> file_data(file_size);
        size_t received = 0;
        while (received < file_size)
        {
            ssize_t bytes = recv(sockfd, file_data.data() + received, file_size - received, 0);
            if (bytes <= 0)
                break;
            received += bytes;
        }

        // Save file to disk
        std::ofstream out(filename, std::ios::binary);
        if (out.is_open())
        {
            out.write(file_data.data(), file_size);
            out.close();

            // Determine file type for message
            std::string type = "unknown";
            if (filename.find(".json") != std::string::npos)
                type = "JSON";
            else if (filename.find(".xml") != std::string::npos)
                type = "XML";
            else if (filename.find(".txt") != std::string::npos)
                type = "TXT";

            std::cout << "📥 Received result file for: " << type << " (" << filename << ")\n";
        }
        else
        {
            std::cerr << "❌ Failed to save result file: " << filename << "\n";
        }
    }

    close(sockfd);

    return 0;
}