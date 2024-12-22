#include <chrono>
#include <fastrest/fastrest.hpp>
#include <iostream>
#include <string>

int main() {
    const std::string host = "api.elections.kalshi.com";
    fastrest::SocketClient client(host);

    std::string request = "GET /trade-api/v2/exchange/schedule HTTP/1.1\r\n"
                          "Host: api.elections.kalshi.com\r\n"
                          "Accept: */*\r\n"
                          "Connection: keep-alive\r\n"
                          "Content-Type: application/json\r\n\r\n";

    int n_requests = 0;
    double n_empty_requests = 0;
    double n_full_requests = 0;
    double total_send_time = 0;
    double total_empty_recv_time = 0;
    double total_actual_recv_time = 0;

    for (int i = 0; i < 20; i++) {
        std::cout << "Ping " << i << std::endl;
        n_requests++;

        auto start_send = std::chrono::high_resolution_clock::now();
        client.send_request(request);
        auto end_send = std::chrono::high_resolution_clock::now();

        total_send_time +=
            std::chrono::duration_cast<std::chrono::microseconds>(end_send -
                                                                  start_send)
                .count();

        while (true) {
            auto start_read = std::chrono::high_resolution_clock::now();
            auto out = client.read_buffer();
            auto end_read = std::chrono::high_resolution_clock::now();
            if (out.length() > 0) {
                std::cout << out << std::endl;
                n_full_requests += 1;
                total_actual_recv_time +=
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        end_read - start_read)
                        .count();
            } else {
                n_empty_requests += 1;
                total_empty_recv_time +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        end_read - start_read)
                        .count();
            }

            auto total_time =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_read - start_send)
                    .count();
            if (total_time > 100)
                break;
        }
    }

    std::cout << "\n\n"
              << "Average send latency: "
              << total_send_time / ((double)n_requests) << "us" << std::endl;
    std::cout << "Average recv empty latency: "
              << total_empty_recv_time / n_empty_requests << "ns" << std::endl;
    std::cout << "Average recv actual latency: "
              << total_actual_recv_time / n_full_requests << "us" << std::endl;

    return 0;
}