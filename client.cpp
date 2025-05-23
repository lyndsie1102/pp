#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <filesystem>
#include <random>
#include "json.hpp"

#define PORT 8080
#define BUFFER_SIZE 1024

using json = nlohmann::json;
namespace fs = std::filesystem;

int main()
{
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Socket creation error");
        return 1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
    {
        std::cerr << "Invalid address/Address not supported" << std::endl;
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Connection Failed");
        return 1;
    }

    // Step 1: Collect user inputs
    std::string group_by;
    std::cout << "Enter grouping criteria (user/ip/level): ";
    std::cin >> group_by;

    char date_response;
    std::string start_date, end_date;
    std::cout << "Would you like to specify a date range? (y/n): ";
    std::cin >> date_response;

    if (date_response == 'y' || date_response == 'Y')
    {
        std::cin.ignore(); // flush newline
        std::cout << "Enter start date (YYYY-MM-DD HH:MM:SS): ";
        std::getline(std::cin, start_date);
        std::cout << "Enter end date (YYYY-MM-DD HH:MM:SS): ";
        std::getline(std::cin, end_date);
    }

    // Step 2: Select a random subfolder in test_clients
    std::string base_dir = "test_clients";
    std::vector<fs::path> subdirs;

    for (const auto &entry : fs::directory_iterator(base_dir))
    {
        if (fs::is_directory(entry.status()))
        {
            subdirs.push_back(entry.path());
        }
    }

    if (subdirs.empty())
    {
        std::cerr << "No subdirectories found in test_clients.\n";
        return 1;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, subdirs.size() - 1);
    fs::path chosen_dir = subdirs[dis(gen)];

    std::cout << "Randomly selected folder: " << chosen_dir << std::endl;

    // Step 3: Load all three log files from chosen folder
    fs::path json_path = chosen_dir / "log_file.json";
    fs::path txt_path = chosen_dir / "log_file.txt";
    fs::path xml_path = chosen_dir / "log_file.xml";

    std::ifstream json_file(json_path);
    std::ifstream txt_file(txt_path);
    std::ifstream xml_file(xml_path);

    if (!json_file || !txt_file || !xml_file)
    {
        std::cerr << "Failed to open one or more log files in " << chosen_dir << "\n";
        return 1;
    }

    std::ostringstream json_log_stream, txt_log_stream, xml_log_stream;
    json_log_stream << json_file.rdbuf();
    txt_log_stream << txt_file.rdbuf();
    xml_log_stream << xml_file.rdbuf();

    std::string json_log = json_log_stream.str();
    std::string txt_log = txt_log_stream.str();
    std::string xml_log = xml_log_stream.str();

    json_file.close();
    txt_file.close();
    xml_file.close();

    // Step 4: Construct JSON envelope
    json envelope;
    envelope["group_by"] = group_by;
    if (!start_date.empty() && !end_date.empty())
    {
        envelope["start_date"] = start_date;
        envelope["end_date"] = end_date;
    }

    envelope["log_json"] = json_log;
    envelope["log_txt"] = txt_log;
    envelope["log_xml"] = xml_log;

    std::string payload = envelope.dump();

    // Step 5: Send payload to server
    send(sock, payload.c_str(), payload.size(), 0);
    shutdown(sock, SHUT_WR);
    std::cout << "Log files and metadata sent. Waiting for server response...\n";

    // Step 6: Receive response
    std::ostringstream response_stream;
    int bytesRead;
    while ((bytesRead = read(sock, buffer, BUFFER_SIZE)) > 0)
    {
        response_stream.write(buffer, bytesRead);
    }

    std::string server_response = response_stream.str();

    // Step 7: Parse and save server response
    std::ofstream json_result_file("json_result.txt");
    std::ofstream txt_result_file("txt_result.txt");
    std::ofstream xml_result_file("xml_result.txt");

    if (!json_result_file || !txt_result_file || !xml_result_file)
    {
        std::cerr << "Failed to create result files.\n";
        return 1;
    }

    json response_json;
    try
    {
        response_json = json::parse(server_response);
    }
    catch (...)
    {
        std::cerr << "Failed to parse server response.\n";
        return 1;
    }

    json_result_file << response_json["json_result"].get<std::string>();
    txt_result_file << response_json["txt_result"].get<std::string>();
    xml_result_file << response_json["xml_result"].get<std::string>();

    json_result_file.close();
    txt_result_file.close();
    xml_result_file.close();

    std::cout << "Server response saved to json_result.txt, txt_result.txt, and xml_result.txt\n";

    close(sock);
    return 0;
}
