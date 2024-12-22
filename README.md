# fast-rest-client

Single-header C++17 library for single-threaded socket based REST API communication over HTTPS. This is pretty much just a wrapper over sockets/ssl, but has been useful to me when writing clients for apis like Coinbase and Kalshi.

## Features
* Header-only
* Non-blocking
* SSL/TLS using OpenSSL
* **not** thread-safe.

## Requirements
* OpenSSL

## Usage

### Basic Example

Sends a request and then polls for a response for 1 second.

```c++
#include <chrono>
#include <fastrest/fastrest.hpp>
#include <iostream>
#include <string>

int main() {
    fastrest::SocketClient client("api.example.com");

    std::string request = "GET /path HTTP/1.1\r\n"
                          "Host: api.example.com\r\n"
                          "Accept: */*\r\n"
                          "Connection: keep-alive\r\n\r\n";

    client.send_request(request);

    auto start = std::chrono::system_clock::now();

    while (true) {
        auto out = client.read_buffer();
        if (out.length() > 0) {
            std::cout << out << std::endl;
        }

        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now() - start)
                .count() > 1000)
            break;
    }
    return 0;
}
```

## Documentation

### SocketClient

#### Constructor
```c++
fastrest::SocketClient(const std::string host, const long port = 443);
```
* *host*: The server hostname (e.g., `api.example.com`).
* *port*: The port to connect to (default: 443).

#### Methods

**send_request**: Sends a packet to the server.
```c++
int fastrest::SocketClient::send_request(const std::string& req);
```
* *req* The raw packet string.
* *Returns*: The number of bytes sent.

**read_buffer**: Reads bytes from the receive buffer.
```c++
std::string fastrest::SocketClient::read_buffer(const size_t read_size = 100);
```
* *read_size*: Size to read from the buffer at once (i.e., if there is 250 bytes in the buffer, with a read_size of 100, there will be 3 read operations).
* *Returns*: Whatever was in the buffer as a string.

#### Error Handling
Kind of adhoc, will throw a `fastrest::SocketClientException` if there is an error but see `include/fastrest/fastrest.hpp` for specifics.

#### Logging
Again, adhoc, but set the template parameter `verbose` of `fastrest::SocketClient` to `true` for some basic logging to `stdout`.

## Performance
On a M1 Mac, I get call site latencies of <100 microseconds for `send_packet`, ~300 nanoseconds for `read_buffer` when the buffer is empty, and <100 microseconds for `read_buffer` when the buffer is full. YMMV.