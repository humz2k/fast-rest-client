# fast-rest-client

Single-header C++17 library for single-threaded socket based REST API communication over HTTPS. This is pretty much just a wrapper over sockets/ssl, but has been useful to me when writing clients for apis like Coinbase and Kalshi. The intended use case is when you are going to be sending lots of non-blocking requests over a connection you want to keep alive.

## Features
* Header-only
* Non-blocking
* SSL/TLS using OpenSSL

## Requirements
* OpenSSL

## Limitations
* **not** thread-safe.
* Does **not** handle chunked encoding, or anything else that isn't completely basic http.
* I can't imagine there is any use case for this apart from trading APIs like Kalshi and Coinbase. For anything else go use [restclient-cpp](https://github.com/mrtazz/restclient-cpp).

## Usage

### Basic Example

Sends a request and then polls for a response for 1 second.

```c++
#include <chrono>
#include <fastrest/fastrest.hpp>
#include <iostream>
#include <string>

struct Handler {
    void operator()(fastrest::HttpResponse resp) {
        std::cout << "Received response from the server:" << std::endl;
        std::cout << " - status = " << resp.status << std::endl;
        std::cout << " - content = " << resp.content << std::endl;
    }
};

int main() {
    const std::string host = "api.elections.kalshi.com";
    Handler handler;
    fastrest::SocketClient<Handler> client(handler, host);

    client.get("/trade-api/v2/exchange/status");

    auto start = std::chrono::high_resolution_clock::now();

    while (true) {
        client.poll();

        auto end = std::chrono::high_resolution_clock::now();

        auto total_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
                .count();
        if (total_time > 1000)
            break;
    }
    return 0;
}
```

## Documentation

### Basic request types
```c++
client.get("/path/to/resource");
client.post("/path/to/resource", "application/json", "{\"key\":\"value\"}");
client.put("/path/to/resource", "application/json", "{\"key\":\"updated_value\"}");
client.patch("/path/to/resource", "application/json", "{\"key\":\"patched_value\"}");
client.del("/path/to/resource");
client.head("/path/to/resource");
client.options("/path/to/resource");
```

### Polling for responses
We create a functor that will handle responses from the server.
```c++
struct HttpResponseHandler {
    void operator()(fastrest::HttpResponse response){
        // handle response
    }
};
```
Then we provide this to the SocketClient's constructor.
```c++
HttpResponseHandler handler;
fastrest::SocketClient<HttpResponseHandler> client(handler, host);
```
Then poll for responses.
```c++
// send requests...
while (true) {
    client.poll();
}
```
The client will call `HttpResponseHandler::operator()` if there is a packet to be handled when you call `client.poll()`.
