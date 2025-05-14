#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <cstring>
#include <ctime>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "json.hpp"
using json = nlohmann::json;

#define PORT 8080
#define BUFFER_SIZE 1024

std::string process_log_data(const std::string &log_data, const std::string &group_by,
                             const std::string &start_date = "", const std::string &end_date = "",
                             const std::string &format = "txt")
{
    std::unordered_map<std::string, int> result_counts;

    // Parse date range
    struct tm start_tm = {}, end_tm = {};
    bool has_date_range = false;
    if (!start_date.empty() && !end_date.empty())
    {
        strptime(start_date.c_str(), "%Y-%m-%d %H:%M:%S", &start_tm);
        strptime(end_date.c_str(), "%Y-%m-%d %H:%M:%S", &end_tm);
        has_date_range = true;
    }

    if (format == "json")
    {
        try
        {
            auto logs = json::parse(log_data);

            for (const auto &entry : logs)
            {
                std::string timestamp = entry["timestamp"];
                std::string log_level = entry["log_level"];
                std::string user_id = std::to_string(entry["user_id"].get<int>());
                std::string ip_address = entry["ip_address"];

                // Date range check
                if (has_date_range)
                {
                    struct tm log_tm = {};
                    if (strptime(timestamp.c_str(), "%Y-%m-%d %H:%M:%S", &log_tm) == nullptr)
                    {
                        continue;
                    }
                    time_t log_time = mktime(&log_tm);
                    if (log_time < mktime(&start_tm) || log_time > mktime(&end_tm))
                        continue;
                }

                std::string key;
                if (group_by == "user")
                    key = user_id;
                else if (group_by == "ip")
                    key = ip_address;
                else if (group_by == "level")
                    key = log_level;

                result_counts[key]++;
            }
        }
        catch (...)
        {
            return "Invalid JSON format.\n";
        }
    }
    else
    { // txt
        std::istringstream iss(log_data);
        std::string line;

        while (std::getline(iss, line))
        {
            std::string date = line.substr(0, 19);
            std::string level, userid, ip;

            if (line.find("INFO") != std::string::npos)
                level = "INFO";
            else if (line.find("WARN") != std::string::npos)
                level = "WARN";
            else if (line.find("ERROR") != std::string::npos)
                level = "ERROR";
            else if (line.find("CRITICAL") != std::string::npos)
                level = "CRITICAL";

            std::size_t user_pos = line.find("UserID:");
            if (user_pos != std::string::npos)
            {
                std::istringstream user_stream(line.substr(user_pos));
                std::string label;
                user_stream >> label >> userid;
            }

            std::size_t ip_pos = line.find("IP:");
            if (ip_pos != std::string::npos)
            {
                std::istringstream ip_stream(line.substr(ip_pos));
                std::string label;
                ip_stream >> label >> ip;
            }

            if ((group_by == "user" && userid.empty()) ||
                (group_by == "ip" && ip.empty()) ||
                (group_by == "level" && level.empty()))
                continue;

            if (has_date_range)
            {
                struct tm log_tm = {};
                strptime(date.c_str(), "%Y-%m-%d %H:%M:%S", &log_tm);
                time_t log_time = mktime(&log_tm);
                if (log_time < mktime(&start_tm) || log_time > mktime(&end_tm))
                    continue;
            }

            std::string key;
            if (group_by == "user")
                key = userid;
            else if (group_by == "ip")
                key = ip;
            else if (group_by == "level")
                key = level;

            result_counts[key]++;
        }
    }

    // Format output
    std::ostringstream result;
    for (const auto &pair : result_counts)
    {
        result << group_by << " " << pair.first << ": " << pair.second << " entries\n";
    }

    return result.str();
}

int main()
{
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0)
    {
        perror("Socket failed");
        return 1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Bind failed");
        return 1;
    }

    if (listen(server_fd, 3) < 0)
    {
        perror("Listen failed");
        return 1;
    }

    std::cout << "Server listening on port " << PORT << "...\n";

    while (true)
    {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0)
        {
            perror("Accept failed");
            continue;
        }

        std::cout << "Client connected.\n";

        // 1. Receive format (txt/json)
        char format_buffer[BUFFER_SIZE] = {0};
        int bytesRead = read(new_socket, format_buffer, BUFFER_SIZE - 1);
        if (bytesRead <= 0)
        {
            std::cerr << "Failed to read format\n";
            close(new_socket);
            continue;
        }
        std::string file_format(format_buffer, bytesRead);

        // 2. Receive grouping criteria
        char group_by_buffer[BUFFER_SIZE] = {0};
        bytesRead = read(new_socket, group_by_buffer, BUFFER_SIZE - 1);
        if (bytesRead <= 0)
        {
            std::cerr << "Failed to read group_by\n";
            close(new_socket);
            continue;
        }
        std::string group_by(group_by_buffer, bytesRead);

        // 3. Receive optional date range decision (y/n)
        char date_choice;
        bytesRead = read(new_socket, &date_choice, 1);
        if (bytesRead <= 0)
        {
            std::cerr << "Failed to read date range choice\n";
            close(new_socket);
            continue;
        }

        // 4. Receive start and end dates if applicable
        std::string start_date = "", end_date = "";
        if (date_choice == 'y' || date_choice == 'Y')
        {
            char start_date_buffer[BUFFER_SIZE] = {0};
            bytesRead = read(new_socket, start_date_buffer, BUFFER_SIZE - 1);
            if (bytesRead <= 0)
            {
                std::cerr << "Failed to read start date\n";
                close(new_socket);
                continue;
            }
            start_date = std::string(start_date_buffer, bytesRead);

            char end_date_buffer[BUFFER_SIZE] = {0};
            bytesRead = read(new_socket, end_date_buffer, BUFFER_SIZE - 1);
            if (bytesRead <= 0)
            {
                std::cerr << "Failed to read end date\n";
                close(new_socket);
                continue;
            }
            end_date = std::string(end_date_buffer, bytesRead);
        }

        // 5. Read log data
        std::ostringstream log_stream;
        while ((bytesRead = read(new_socket, buffer, BUFFER_SIZE)) > 0)
        {
            log_stream.write(buffer, bytesRead);
        }
        std::string log_data = log_stream.str();

        // 6. Process and respond
        std::string result = process_log_data(log_data, group_by, start_date, end_date, file_format);
        send(new_socket, result.c_str(), result.size(), 0);
        std::cout << "Processed data sent to client.\n";

        close(new_socket);
        std::cout << "Client disconnected.\n";
    }
    close(server_fd);
    return 0;
}
