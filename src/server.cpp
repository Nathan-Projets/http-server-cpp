#include <iostream>
#include <cstdlib>
#include <string>
#include <sstream>
#include <variant>
#include <algorithm>
#include <functional>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <cctype>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

namespace fs = std::filesystem;

const std::string response_OK = "HTTP/1.1 200 OK\r\n";
const std::string response_NOT_OK = "HTTP/1.1 404 Not Found\r\n";
const std::string response_HEADER_END = "\r\n";

const std::string endpoint_ROOT = "/";
const std::string endpoint_ECHO = "/echo/";
const std::string endpoint_USER_AGENT = "/user-agent";
const std::string endpoint_FILES = "/files/";

static std::string directory;

enum REQUEST_TYPE
{
  GET = 0,
  POST
};

enum URL_ENDPOINT
{
  NONE = 0,
  ROOT,
  ECHO,
  USER_AGENT,
  FILES
};

struct Request
{
  REQUEST_TYPE type;
  URL_ENDPOINT url_type;
  std::string url_data;
};

enum ContentType
{
  TEXT_PLAIN = 0,
  OCTET_STREAM
};

/**
 * example of using variant:
 * if (std::holds_alternative<std::string>(body.data)) {
        std::cout << "String: " << std::get<std::string>(body.data) << std::endl;
    }
 */
struct Body
{
  std::variant<std::string, std::unordered_map<std::string, std::string>> Data;
};

enum class QueryMethod
{
  UNKNOWN = 0,
  GET,
  POST,
  PATCH,
  PUT
};

struct QueryLine
{
  QueryMethod Method;
  std::string Path;
  std::string Version;
};

struct Query
{
  QueryLine Queryline;
  std::unordered_map<std::string, std::string> Headers;
  Body Payload;
};

struct Header
{
  std::string Key;
  std::string Value;
};

const std::unordered_map<ContentType, std::string> mapping_content_type{
    {ContentType::TEXT_PLAIN, "text/plain"},
    {ContentType::OCTET_STREAM, "application/octet-stream"}};

const std::unordered_set<std::string> mapping_encoding_supported{
    "gzip"};

const std::unordered_map<std::string, QueryMethod> mapping_method{
    {"GET", QueryMethod::GET},
    {"POST", QueryMethod::POST},
    {"PATCH", QueryMethod::PATCH},
    {"PUT", QueryMethod::PUT}};

std::string write_headers(const Query &query, int size_payload = 0, ContentType content_type = ContentType::TEXT_PLAIN)
{
  std::string headers;
  if (size_payload > 0)
  {
    auto search = mapping_content_type.find(content_type);
    headers += "Content-Type: " + search->second + "\r\n";
    headers += "Content-Length: " + std::to_string(size_payload) + "\r\n";
    auto encoding = query.Headers.find("Accept-Encoding");
    if (encoding != query.Headers.end() and mapping_encoding_supported.find(encoding->second) != mapping_encoding_supported.end())
    {
      headers += "Content-Encoding: " + encoding->second + "\r\n";
    }
  }
  headers += response_HEADER_END;
  return headers;
}

void send_response(int client_fd, std::string response)
{
  if (send(client_fd, response.c_str(), response.size(), 0) == -1)
  {
    std::cout << "Couldn't send response\n";
  }
}

std::string read_message(int client_fd)
{
  char msg_client[256] = "";
  int msg_client_length = sizeof(msg_client);
  bzero(&msg_client, msg_client_length);
  if (recv(client_fd, &msg_client, msg_client_length, 0) == -1)
  {
    std::cout << "Couldn't receive response\n";
    return "";
  }

  return msg_client;
}

// No control on the input, the predicate is that we try to parse a string following the right HTTP format "Method Path Protocol_Version"
void parse_query_line(std::string message, QueryLine &query_line)
{
  if (not message.empty())
  {
    std::istringstream iss(message);
    std::string method;
    iss >> method >> query_line.Path >> query_line.Version;
    auto method_iterator = mapping_method.find(method);
    if (method_iterator != mapping_method.end())
    {
      query_line.Method = method_iterator->second;
    }
  }
}

void parse_body(std::string message, const Query &query, Body &body)
{
  auto content_type = query.Headers.find("Content-Type");
  if (content_type != query.Headers.end())
  {
    if (content_type->second == mapping_content_type.at(ContentType::TEXT_PLAIN) || content_type->second == mapping_content_type.at(ContentType::OCTET_STREAM))
    {
      body.Data = message;
    }
    else
    {
      std::cout << "Content-Type " << content_type->second << " is not supported yet.\n";
    }
  }
}

void parse_message(std::string message, Query &query_output, std::string delimiter = "\r\n")
{
  if (not message.empty())
  {
    std::string headers, body;
    std::string delimiter_header_body = "\r\n\r\n";
    std::size_t search = message.find(delimiter_header_body);
    if (search != std::string::npos)
    {
      headers = message.substr(0, search);
      body = message.substr(search + delimiter_header_body.size());
    }

    // parse request line
    search = headers.find(delimiter);
    if (search != std::string::npos)
    {
      parse_query_line(headers.substr(0, search), query_output.Queryline);
      headers.erase(0, search + delimiter.size());
    }

    // parse headers
    search = headers.find(delimiter);
    while (search != std::string::npos and headers.size() > 0)
    {
      Header header;
      std::string header_string = headers.substr(0, search);
      size_t colon_pos = header_string.find(":");
      if (colon_pos != std::string::npos)
      {
        // Determine if there is a space after the colon
        size_t value_start_pos = colon_pos + 1;
        if (header_string[colon_pos + 1] == ' ')
        {
          value_start_pos++;
        }

        header.Key = header_string.substr(0, colon_pos);
        header.Value = header_string.substr(value_start_pos);
        query_output.Headers[header.Key] = header.Value;
      }

      headers.erase(0, search + delimiter.size());
      search = headers.find(delimiter);
    }

    // Handle the last header if there's no trailing delimiter
    if (!headers.empty())
    {
      Header header;
      size_t colon_pos = headers.find(':');
      if (colon_pos != std::string::npos)
      {
        // Determine if there is a space after the colon
        size_t value_start_pos = colon_pos + 1;
        if (headers[colon_pos + 1] == ' ')
        {
          value_start_pos++;
        }

        header.Key = headers.substr(0, colon_pos);
        header.Value = headers.substr(value_start_pos);
        query_output.Headers[header.Key] = header.Value;
      }
    }

    // parse body
    parse_body(body, query_output, query_output.Payload);
  }
}

void handle_endpoint_error(int client_fd, std::string &message, Query &request)
{
  std::string response = response_NOT_OK + write_headers(request);
  send_response(client_fd, response);
}

void handle_endpoint_root(int client_fd, std::string &message, Query &request)
{
  std::string response = response_OK + write_headers(request);
  send_response(client_fd, response);
}

void handle_endpoint_echo(int client_fd, std::string &message, Query &request)
{
  std::string answer = request.Queryline.Path.substr(endpoint_ECHO.size(), request.Queryline.Path.size() - endpoint_ECHO.size());
  std::string response = response_OK + write_headers(request, answer.size()) + answer;
  send_response(client_fd, response);
}

void handle_endpoint_user_agent(int client_fd, std::string &message, Query &request)
{
  auto user_agent = request.Headers.find("User-Agent");
  if (user_agent != request.Headers.end())
  {
    std::string payload = "User-Agent not found.";
    std::string response = response_NOT_OK + write_headers(request, payload.size()) + payload;
    if (not user_agent->second.empty())
    {
      response = response_OK + write_headers(request, user_agent->second.size()) + user_agent->second;
    }
    send_response(client_fd, response);
  }
  else
  {
    handle_endpoint_error(client_fd, message, request);
  }
}

void read_file_response(int client_fd, const Query &request, std::string &file_name)
{
  fs::path absolute_path(directory);
  absolute_path.append(file_name);
  std::ifstream from(absolute_path);
  std::string content;

  if (from.is_open())
  {
    std::string line;
    while (getline(from, line))
    {
      content += line;
    }

    std::string response = response_OK + write_headers(request, content.size(), ContentType::OCTET_STREAM) + content;
    send_response(client_fd, response);
  }
  else
  {
    std::string response = response_NOT_OK + write_headers(request);
    send_response(client_fd, response);
  }
}

void write_file_response(int client_fd, std::string &file_name, std::string &content)
{
  fs::path absolute_path(directory);
  absolute_path.append(file_name);
  std::ofstream to(absolute_path);

  if (to.is_open())
  {
    to << content;
  }

  std::string response = "HTTP/1.1 201 Created\r\n\r\n";
  send_response(client_fd, response);
}

void handle_endpoint_files(int client_fd, std::string &message, Query &request)
{
  std::string file_name = request.Queryline.Path.substr(endpoint_FILES.size(), request.Queryline.Path.size() - endpoint_FILES.size());

  try
  {
    if (request.Queryline.Method == QueryMethod::GET)
    {
      read_file_response(client_fd, request, file_name);
    }
    else if (request.Queryline.Method == QueryMethod::POST)
    {
      if (std::holds_alternative<std::string>(request.Payload.Data))
      {
        write_file_response(client_fd, file_name, std::get<std::string>(request.Payload.Data));
      }
      else
      {
        handle_endpoint_error(client_fd, message, request);
      }
    }
  }
  catch (std::exception e)
  {
    std::cerr << "Exception opening/reading/closing file: " << e.what() << std::endl;
    handle_endpoint_error(client_fd, message, request);
  }
}

const std::unordered_map<std::string, std::function<void(int, std::string &, Query &)>> mapping_handling_endpoint{
    {endpoint_ROOT, handle_endpoint_root},
    {endpoint_ECHO, handle_endpoint_echo},
    {endpoint_USER_AGENT, handle_endpoint_user_agent},
    {endpoint_FILES, handle_endpoint_files},
};

void handle_connection(int client_fd)
{
  std::cout << "Handle connection client " << client_fd << "\n";
  std::string msg_manipulator = read_message(client_fd);
  if (not msg_manipulator.empty())
  {
    Query query;
    parse_message(msg_manipulator, query);

    bool endpoint_found = false;
    for (auto [path, handler] : mapping_handling_endpoint)
    {
      std::size_t searchh = query.Queryline.Path.find(path);
      if (searchh != std::string::npos and searchh == 0)
      {
        if (path == "/" and query.Queryline.Path != path)
        {
          continue;
        }
        handler(client_fd, msg_manipulator, query);
        endpoint_found = true;
        break;
      }
    }

    if (not endpoint_found)
    {
      handle_endpoint_error(client_fd, msg_manipulator, query);
    }
  }
}

int main(int argc, char **argv)
{
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  if (argc == 3 && strcmp(argv[1], "--directory") == 0)
  {
    directory = argv[2];
    directory.erase(std::remove_if(directory.begin(), directory.end(),
                                   [](auto const &c) -> bool
                                   { return !std::isalnum(c) && c != '/' && c != '-' && c != '.' && c != '_'; }),
                    directory.end());
    std::cout << "Current directory has been set to " << directory << "\n";
  }

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0)
  {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
  {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(4221);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
  {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0)
  {
    std::cerr << "listen failed\n";
    return 1;
  }

  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);

  std::cout << "Waiting for a client to connect...\n";

  while (true)
  {
    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
    if (client_fd == -1)
    {
      continue;
    }

    std::thread client(handle_connection, client_fd);
    client.detach();
  }

  close(server_fd);

  return 0;
}
