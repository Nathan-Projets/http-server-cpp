This is a http server done in the context of ["Build Your Own HTTP server" Challenge](https://app.codecrafters.io/courses/http-server/overview).

In this challenge, I have built a HTTP/1.1 server that is capable of serving multiple clients.

**Note**: If you want to try it also, head over to [codecrafters.io](https://codecrafters.io) to try the challenge.

# Build

1. Ensure you have `cmake` installed locally
2. Run `./your_server.sh` to run your program, which is implemented in
   `src/server.cpp`. You may need to change the header depending on your environment (this line `#!/bin/bash`)
3. Test output will be streamed to your terminal.
4. You can test by sending request with curl or another tool.

# Endpoints

1. `/`: GET, return a 200 response.
2. `/echo/{your/content/or/whatever}`: GET, return the text content specified in the parameter, (accept encoding `gzip`)
3. `/user-agent`:  GET, return the header `User-Agent` specified in the request
4. `/files/{your/file/path}`:
   1. GET, return the content of the file in the relative path specified. You need to pass a directory when running the server script (e.g `./your_server.sh --directory /tmp`)
   2. POST, writes the content passed in the body of the request to the relative path specified as parameter.    

# Examples

1. `curl -v http://localhost:4221/`
2. `curl -v http://localhost:4221/echo/pineapple`
3. `curl -v http://localhost:4221/echo/blueberry -H "Accept-Encoding: gzip"`
4. `curl -v http://localhost:4221/user-agent -H "User-Agent: strawberry/raspberry-pear"`
5. `curl -v http://localhost:4221/files/file_12345 -H "Content-Type: application/octet-stream" --data "12345"`
6. `curl -v http://localhost:4221/files/file_12345`
