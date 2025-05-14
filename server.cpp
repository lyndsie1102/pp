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
#include "json.hpp"  // Include the JSON library
#include "tinyxml2.h"  // Include TinyXML2 for XML processing

using json = nlohmann::json;
using namespace tinyxml2;

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
    else if (format == "xml")
    {
        XMLDocument doc;
        doc.Parse(log_data.c_str());

        XMLElement *root = doc.FirstChildElement("logs");
        if (root == nullptr)
            return "Invalid XML format.\n";

        for (XMLElement *log_element = root->FirstChildElement("log"); log_element != nullptr; log_element = log_element->NextSiblingElement("log"))
        {
            std::string timestamp = log_element->FirstChildElement("timestamp")->GetText();
            std::string log_level = log_element->FirstChildElement("log_level")->GetText();
            std::string user_id = log_element->FirstChildElement("user_id")->GetText();
            std::string ip_address = log_element->FirstChildElement("ip_address")->GetText();

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
    else  // txt
    {
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

        // 1. Receive the entire payload (JSON with all log files)
        std::ostringstream payload_stream;
        int bytesRead;
        while ((bytesRead = read(new_socket, buffer, BUFFER_SIZE)) > 0)
        {
            payload_stream.write(buffer, bytesRead);
        }
        std::string payload = payload_stream.str();

        // 2. Parse the received JSON
        json envelope;
        try
        {
            envelope = json::parse(payload);
        }
        catch (...)
        {
            std::cerr << "Invalid JSON received.\n";
            continue;
        }

        // Extract metadata
        std::string group_by = envelope["group_by"];
        std::string start_date = envelope.value("start_date", "");
        std::string end_date = envelope.value("end_date", "");

        // Process each log file (json, txt, xml)
        std::string json_result = process_log_data(envelope["log_json"], group_by, start_date, end_date, "json");
        std::string txt_result = process_log_data(envelope["log_txt"], group_by, start_date, end_date, "txt");
        std::string xml_result = process_log_data(envelope["log_xml"], group_by, start_date, end_date, "xml");

        // Combine the results
        json result_json;
        result_json["json_result"] = json_result;
        result_json["txt_result"] = txt_result;
        result_json["xml_result"] = xml_result;

        // Send the results back to the client
        std::string result_payload = result_json.dump();
        send(new_socket, result_payload.c_str(), result_payload.size(), 0);

        std::cout << "Processed data sent back to client.\n";
        close(new_socket);
    }

    return 0;
}
