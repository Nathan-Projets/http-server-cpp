#include <iostream>
#include <cstdlib>
#include <string>
#include <algorithm>
#include <sstream>
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
  to_lower(search_term);
  char *p = strtok(message.data(), delimiter.c_str());
  while (p != nullptr)
  {
    std::string word(p);
    std::string word_search(p);
    to_lower(word_search);
    if (word_search.find(search_term) != std::string::npos)
    {
      return word.substr(search_term.size(), word.size() - search_term.size());
    }
    message.erase(0, word.size() + delimiter.size());
    p = strtok(message.data(), delimiter.c_str());
  }
  return "";
}

std::string writeHeaders(int size_payload = 0)
{
  std::string headers;
  headers += "Content-Type: text/plain\r\nContent-Length: ";
  headers += std::to_string(size_payload);
  headers += "\r\n\r\n";
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
  std::string search_term_user = "User-Agent: ";
  std::string payload = searchLine(message, search_term_user);
  std::string response = response_NOT_OK + response_HEADER_END;
  if (not payload.empty())
  {
    response = response_OK + writeHeaders(payload.size()) + payload;
  }
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

  while (1)
  {
    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
    if (client_fd == -1)
    {
      continue;
    }

    handle_connection(client_fd);
  }

  // int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
  // std::cout << "Client connected\n";

  // char msg_client[128] = "";
  // int msg_client_length = sizeof(msg_client);
  // bzero(&msg_client, msg_client_length);
  // if (recv(client_fd, &msg_client, msg_client_length, 0) == -1)
  // {
  //   std::cout << "Couldn't receive response\n";
  //   close(server_fd);
  //   return 0;
  // }

  // std::string msg_manipulator = msg_client;

  // std::string request_line = readLine(msg_manipulator); // GET /abcdefg HTTP/1.1
  // if (request_line.empty())
  // {
  //   std::cout << "Couldn't read a line\n";
  // }

  // std::string response_ok = "HTTP/1.1 200 OK\r\n";
  // std::string response_not_ok = "HTTP/1.1 404 Not Found\r\n";
  // std::string end_response = "\r\n";

  // URL_ENDPOINT url_found = URL_ENDPOINT::NONE;

  // bool get_found = false;
  // std::string temp = request_line;
  // std::size_t pos = temp.find(" ");
  // while (pos != std::string::npos)
  // {
  //   std::string word = temp.substr(0, pos);
  //   if (word == "GET")
  //   {
  //     get_found = true;
  //   }
  //   else if (get_found)
  //   {
  //     if (word == "/")
  //     {
  //       std::string response = response_ok + end_response;
  //       sendResponse(client_fd, response);
  //     }
  //     else
  //     {
  //       std::string path_echo = "/echo/";
  //       std::string path_user_agent = "/user-agent";
  //       if (std::size_t pos2 = word.find(path_echo) != std::string::npos)
  //       {
  //         std::string answer = word.substr(path_echo.size(), word.size() - path_echo.size());
  //         std::string response = response_ok + writeHeaders(answer.size()) + answer;
  //         sendResponse(client_fd, response);
  //       }
  //       else if (std::size_t pos2 = word.find(path_user_agent) != std::string::npos)
  //       {
  //         url_found = URL_ENDPOINT::USER_AGENT;
  //       }
  //       else
  //       {
  //         std::string response = response_not_ok + end_response;
  //         sendResponse(client_fd, response);
  //       }
  //     }
  //     break;
  //   }

  //   temp = temp.substr(pos + 1, temp.size() - pos);
  //   pos = temp.find(" ");
  // }

  // std::string search_term_user = "User-Agent: ";
  // switch (url_found)
  // {
  // case URL_ENDPOINT::NONE:
  //   std::cout << "No URL handler found\n";
  //   break;
  // case URL_ENDPOINT::USER_AGENT:
  //   std::cout << "URL handler found: User-Agent\n";
  //   std::string payload = searchLine(msg_manipulator, search_term_user);
  //   std::string response = response_not_ok + end_response;
  //   if (not payload.empty())
  //   {
  //     response = response_ok + writeHeaders(payload.size()) + payload;
  //   }
  //   sendResponse(client_fd, response);
  //   break;
  // }

  close(server_fd);

  return 0;
}
