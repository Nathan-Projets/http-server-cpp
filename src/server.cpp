#include <iostream>
#include <cstdlib>
#include <string>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

/**
 * Returns the line defined by the delimiter
 */
std::string readLine(std::string &message, const std::string &delimiter = "\r\n")
{
  char *p = strtok(message.data(), delimiter.c_str());
  return (p != nullptr) ? p : message.data();
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

  int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
  std::cout << "Client connected\n";

  char msg_client[128] = "";
  int msg_client_length = sizeof(msg_client);
  bzero(&msg_client, msg_client_length);
  if (recv(client_fd, &msg_client, msg_client_length, 0) == -1)
  {
    std::cout << "Couldn't receive response\n";
    close(server_fd);
    return 0;
  }

  std::string msg_manipulator = msg_client;

  std::string request_line = readLine(msg_manipulator); // GET /abcdefg HTTP/1.1
  if (request_line.empty())
  {
    std::cout << "Couldn't read a line\n";
  }
  else
  {
    std::cout << "Here is the line: " << request_line << "\n";
  }

  std::string response_ok = "HTTP/1.1 200 OK\r\n\r\n";
  std::string response_not_ok = "HTTP/1.1 404 Not Found\r\n\r\n";

  bool get_found = false;
  std::string temp = request_line;
  std::size_t pos = temp.find(" ");
  while (pos != std::string::npos)
  {
    std::string word = temp.substr(0, pos);
    if (word == "GET")
    {
      get_found = true;
    }
    else if (get_found)
    {
      if (word == "/")
      {
        if (send(client_fd, response_ok.c_str(), response_ok.size(), 0) == -1)
        {
          std::cout << "Couldn't send response\n";
        }
      }
      else
      {
        if (send(client_fd, response_not_ok.c_str(), response_not_ok.size(), 0) == -1)
        {
          std::cout << "Couldn't send response\n";
        }
      }
      break;
    }

    temp = temp.substr(pos + 1, temp.size() - pos);
    pos = temp.find(" ");
  }

  close(server_fd);

  return 0;
}
