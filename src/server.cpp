#include <iostream>
#include <cstdlib>
#include <string>
#include <algorithm>
#include <sstream>
#include <thread>
#include <cctype>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

const std::string response_OK = "HTTP/1.1 200 OK\r\n";
const std::string response_NOT_OK = "HTTP/1.1 404 Not Found\r\n";
const std::string response_HEADER_END = "\r\n";

const std::string endpoint_ROOT = "/";
const std::string endpoint_ECHO = "/echo/";
const std::string endpoint_USER_AGENT = "/user-agent";

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
  USER_AGENT
};

struct Request
{
  REQUEST_TYPE type;
  URL_ENDPOINT url_type;
  std::string url_data;
};

void to_lower(std::string &str)
{
  std::transform(str.begin(), str.end(), str.begin(),
                 [](unsigned char c)
                 { return std::tolower(c); });
}

// This is needed because for god knows why codecrafters decided to include null characters in their test cases
std::string removeNullCharacters(const std::string &input)
{
  std::string result;
  result.reserve(input.size()); // Reserve memory to improve performance
  for (char c : input)
  {
    if (c != '\x00')
    {
      result += c;
    }
  }
  return result;
}

/**
 * Returns the line defined by the delimiter
 */
std::string readLine(std::string &message, const std::string &delimiter = "\r\n")
{
  char *p = strtok(message.data(), delimiter.c_str());
  return (p != nullptr) ? p : message.data();
}

std::string searchLine(std::string message, std::string search_term, const std::string &delimiter = "\r\n")
{
  std::string lower_case_message = message;
  to_lower(lower_case_message);
  to_lower(search_term);

  std::size_t pos_term = lower_case_message.find(search_term);
  if (pos_term != std::string::npos)
  {
    std::size_t pos_end_value = lower_case_message.find(delimiter, pos_term);
    if (pos_end_value != std::string::npos)
    {
      std::string result = message.substr(pos_term + search_term.size());
      std::size_t pos_specials = result.find(delimiter);
      while (pos_specials != std::string::npos)
      {
        result.erase(pos_specials, delimiter.size());
        pos_specials = result.find(delimiter);
      }
      result.erase(result.begin(), std::find_if(result.begin(), result.end(), [](unsigned char ch)
                                                { return !std::isspace(ch); }));
      return removeNullCharacters(result);
    }
  }

  return "";
}

std::string writeHeaders(int size_payload = 0)
{
  std::string headers;
  if (size_payload > 0)
  {
    headers += "Content-Type: text/plain\r\n";
    headers += "Content-Length: " + std::to_string(size_payload) + "\r\n";
  }
  headers += response_HEADER_END;
  return headers;
}

void sendResponse(int client_fd, std::string response)
{
  if (send(client_fd, response.c_str(), response.size(), 0) == -1)
  {
    std::cout << "Couldn't send response\n";
  }
}

std::string readMessage(int client_fd)
{
  char msg_client[128] = "";
  int msg_client_length = sizeof(msg_client);
  bzero(&msg_client, msg_client_length);
  if (recv(client_fd, &msg_client, msg_client_length, 0) == -1)
  {
    std::cout << "Couldn't receive response\n";
    return "";
  }

  return msg_client;
}

Request read_request(std::string &message)
{
  std::string request_line = readLine(message);
  Request request = {
      REQUEST_TYPE::GET,  // type
      URL_ENDPOINT::NONE, // url_type
      ""                  // url_data
  };

  if (not request_line.empty())
  {
    int nth_element = 0;
    char *p = strtok(request_line.data(), " ");
    while (p != nullptr)
    {
      if (nth_element > 3)
      {
        break;
      }
      std::string word(p);
      switch (nth_element)
      {
      case 0: // request type
        if (word.find("POST") != std::string::npos)
        {
          request.type = REQUEST_TYPE::POST;
        }
        break;
      case 1: // url
        if (word == endpoint_ROOT)
        {
          request.url_type = URL_ENDPOINT::ROOT;
        }
        if (word.find(endpoint_ECHO) != std::string::npos)
        {
          request.url_type = URL_ENDPOINT::ECHO;
        }
        else if (word.find(endpoint_USER_AGENT) != std::string::npos)
        {
          request.url_type = URL_ENDPOINT::USER_AGENT;
        }

        request.url_data = word;
        break;
      case 2: // protocol
              // pass
        break;
      }

      message.erase(0, word.size() + 1);
      p = strtok(message.data(), " ");
      nth_element++;
    }
  }
  return request;
}

void handle_endpoint_error(int client_fd, std::string &message, Request &request)
{
  std::string response = response_NOT_OK + writeHeaders();
  sendResponse(client_fd, response);
}

void handle_endpoint_root(int client_fd, std::string &message, Request &request)
{
  std::string response = response_OK + writeHeaders();
  sendResponse(client_fd, response);
}

void handle_endpoint_echo(int client_fd, std::string &message, Request &request)
{
  std::string answer = request.url_data.substr(endpoint_ECHO.size(), request.url_data.size() - endpoint_ECHO.size());
  std::string response = response_OK + writeHeaders(answer.size()) + answer;
  sendResponse(client_fd, response);
}

void handle_endpoint_user_agent(int client_fd, std::string &message, Request &request)
{
  std::string search_term_user = "User-Agent:";
  std::string payload = searchLine(message, search_term_user);
  std::string response = response_NOT_OK + response_HEADER_END;
  std::cout << "PAYLOAD: " << payload << "\n";
  if (not payload.empty())
  {
    response = response_OK + writeHeaders(payload.size()) + payload;
  }
  std::cout << "handle user agent sending: " << response << "\n";
  sendResponse(client_fd, response);
}

void handle_connection(int client_fd)
{
  std::cout << "Handle connection client " << client_fd << "\n";
  std::string msg_manipulator = readMessage(client_fd);
  if (not msg_manipulator.empty())
  {
    Request request = read_request(msg_manipulator);
    if (request.url_type != URL_ENDPOINT::NONE)
    {
      if (request.url_type == URL_ENDPOINT::ROOT)
      {
        handle_endpoint_root(client_fd, msg_manipulator, request);
      }
      else if (request.url_type == URL_ENDPOINT::ECHO)
      {
        handle_endpoint_echo(client_fd, msg_manipulator, request);
      }
      else if (request.url_type == URL_ENDPOINT::USER_AGENT)
      {
        handle_endpoint_user_agent(client_fd, msg_manipulator, request);
      }
    }
    else
    {
      handle_endpoint_error(client_fd, msg_manipulator, request);
    }
  }
}

int main(int argc, char **argv)
{
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

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
