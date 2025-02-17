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
    client.get("/trade-api/v2/exchange/schedule");

    auto start = std::chrono::high_resolution_clock::now();

    while (true) {
        client.poll();

        auto end = std::chrono::high_resolution_clock::now();

        auto total_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
                .count();
        if (total_time > 300)
            break;
    }
    return 0;
}