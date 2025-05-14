#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 1024

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


    std::string file_format;
    std::cout << "Enter log file format (txt/json): ";
    std::cin >> file_format;
    send(sock, file_format.c_str(), file_format.size(), 0);

    // Send the group-by criteria (user, ip, or level)
    std::string group_by;
    std::cout << "Enter grouping criteria (user/ip/level): ";
    std::cin >> group_by;
    send(sock, group_by.c_str(), group_by.size(), 0);

    // Ask for date range
    std::string start_date, end_date;
    char date_response;
    std::cout << "Would you like to specify a date range? (y/n): ";
    std::cin >> date_response;

    // ✅ Send date_response to server
    send(sock, &date_response, 1, 0);

    if (date_response == 'y' || date_response == 'Y')
    {
        std::cin.ignore(); // Clean up newline after previous input
        std::cout << "Enter start date (YYYY-MM-DD HH:MM:SS): ";
        std::getline(std::cin, start_date);
        std::cout << "Enter end date (YYYY-MM-DD HH:MM:SS): ";
        std::getline(std::cin, end_date);

        // Send date range strings
        send(sock, start_date.c_str(), start_date.size(), 0);
        send(sock, end_date.c_str(), end_date.size(), 0);
    }

    // Send the log file contents to the server
    std::ifstream infile("test_clients/log_file.json", std::ios::binary);
    if (!infile)
    {
        std::cerr << "Cannot open logs.txt" << std::endl;
        return 1;
    }

    char file_buffer[BUFFER_SIZE];
    while (!infile.eof())
    {
        infile.read(file_buffer, BUFFER_SIZE);
        send(sock, file_buffer, infile.gcount(), 0);
    }
    infile.close();

    // Signal end of transmission
    shutdown(sock, SHUT_WR);
    std::cout << "Log file sent. Waiting for server response...\n";

    // Receive processed response from server
    std::ostringstream oss;
    int bytesRead;
    while ((bytesRead = read(sock, buffer, BUFFER_SIZE)) > 0)
    {
        oss.write(buffer, bytesRead);
    }

    // Receive processed response from server
    while ((bytesRead = read(sock, buffer, BUFFER_SIZE)) > 0)
    {
        oss.write(buffer, bytesRead);
    }

    std::string server_response = oss.str();

    std::ofstream outfile("results.txt");
    if (!outfile)
    {
        std::cerr << "Failed to create results.txt\n";
        return 1;
    }
    outfile << server_response;
    outfile.close();

    std::cout << "Server response saved to results.txt\n";

    close(sock);
    return 0;
}
