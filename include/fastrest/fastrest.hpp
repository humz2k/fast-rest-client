#ifndef _FASTREST_FASTREST_HPP_
#define _FASTREST_FASTREST_HPP_

#include <smallstring/smallstring.hpp>

#include <boost/circular_buffer.hpp>
#include <boost/pool/pool_alloc.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace fastrest {

// messages are returned as a fastrest::string which uses a pool allocator
using string = std::basic_string<char, std::char_traits<char>,
                                 boost::fast_pool_allocator<char>>;

class SocketClientException : public std::runtime_error {
  public:
    explicit SocketClientException(const std::string& msg)
        : std::runtime_error(msg) {}
};

struct HttpResponse {
    int status;     // status code
    string content; // json content (as a fastrest::string)
};

template <class Handler> class HttpParser {
  private:
    smallstring::Buffer<std::vector<char>> m_buffer{4096};
    int m_parse_status = 0;

    int m_current_status_code = 0;
    int m_current_content_length = 0;
    bool m_connection_alive = true;

    Handler& m_handler;

    static constexpr std::size_t RESPONSE_BUFFER_CAPACITY = 1024;

    // Replace std::queue with boost::circular_buffer
    boost::circular_buffer<HttpResponse> m_responses{RESPONSE_BUFFER_CAPACITY};

    // std::queue<HttpResponse> m_responses;

    // hacky way to go from str->int
    int parse_int(const char* start, const char* end) const {
        int out = 0;
        for (auto it = start; it != end; ++it) {
            out *= 10;
            out += (*it) - '0';
        }
        return out;
    }

    void check_status_code() {
        if (m_parse_status != 0)
            return;
        const size_t pos = m_buffer.find("HTTP/1.1 ");
        if (pos == std::string::npos)
            return;
        auto status_start = m_buffer.begin() + pos + 9;
        auto status_end = std::find(status_start, m_buffer.end(), ' ');
        if (status_end == m_buffer.end())
            return;

        m_current_status_code = parse_int(status_start, status_end);
        m_parse_status++;
    }

    void check_connection() {
        if (m_parse_status != 1)
            return;
        const size_t pos = m_buffer.find("Connection: ");
        if (pos == std::string::npos)
            return;
        auto connection_start = m_buffer.begin() + pos + 12;
        auto connection_end = std::find(connection_start, m_buffer.end(), '\r');
        if (connection_end == m_buffer.end())
            return;
        if (std::string_view(connection_start,
                             (connection_end - connection_start)) !=
            "keep-alive") {
            m_connection_alive = false;
        }
        m_parse_status++;
    }

    void check_content_length() {
        if (m_parse_status != 2)
            return;
        const size_t pos = m_buffer.find("Content-Length: ");
        if (pos == std::string::npos)
            return;
        auto content_length_start = m_buffer.begin() + pos + 16;
        auto content_length_end =
            std::find(content_length_start, m_buffer.end(), '\r');
        if (content_length_end == m_buffer.end())
            return;
        m_current_content_length =
            parse_int(content_length_start, content_length_end);
        m_parse_status++;
    }

    void check_content() {
        if (m_parse_status != 3)
            return;
        const size_t start = m_buffer.find("\r\n\r\n");
        if (start == std::string::npos)
            return;
        const size_t length_now = m_buffer.length() - (start + 4);
        if (length_now >= m_current_content_length) {
            auto content_start = m_buffer.begin() + start + 4;
            auto content_end =
                m_buffer.begin() + start + 4 + m_current_content_length;
            m_responses.push_back(
                {.status = m_current_status_code,
                 .content = string(content_start, content_end)});
            m_buffer.pop(start + 4 + m_current_content_length);
            m_parse_status = 0;
        }
    }

  public:
    explicit HttpParser(Handler& handler) : m_handler(handler) {}

    void update(const std::string_view& buf) {
        if (buf.length() == 0)
            return;
        m_buffer.push(buf);
        check_status_code();
        check_connection();
        check_content_length();
        check_content();
    }

    void poll() {
        if (m_responses.size() > 0) {
            m_handler(m_responses.front());
            m_responses.pop_front();
        }
    }

    bool connection_alive() const { return m_connection_alive; }

    void set_connected() { m_connection_alive = true; }
};

template <class Handler, bool verbose = false> class SocketClient {
  private:
    // the url of the host we are making requests to
    std::string m_host;
    HttpParser<Handler> m_parser;
    long m_port;

    // for caching requests
    smallstring::Buffer<std::vector<char>> m_buff{4096};

    // socket
    int m_sockfd = -1;

    // ssl socket, the thing we actually use
    int m_sslsock = -1;

    // ssl shit
    SSL_CTX* m_ctx = nullptr;
    SSL* m_ssl = nullptr;

    // we keep this guy around so we dont do extra heap allocations
    string m_out;

    // dumb way to print ssl errors
    std::string get_ssl_error() {
        std::string out = "";
        int err;
        while ((err = ERR_get_error())) {
            char* str = ERR_error_string(err, 0);
            if (str)
                out += std::string(str);
        }
        return out;
    }

    template <std::size_t N>
    std::string_view
    construct_http_request(const char (&method)[N], const std::string& path,
                           const std::string& host,
                           const std::string& content = "",
                           const std::string& extra_headers = "") {
        m_buff.clear();
        m_buff.push(method);
        m_buff.push(" ");
        m_buff.push(path);
        m_buff.push(" HTTP/1.1\r\nHost: ");
        m_buff.push(host);
        m_buff.push("\r\nAccept: */*\r\nConnection: keep-alive\r\n");
        m_buff.push(extra_headers);
        if (content.size() == 0) {
            m_buff.push("\r\n");
            return m_buff.view();
        }
        m_buff.push("Content-Length: ");
        m_buff.push(std::to_string(content.size()));
        m_buff.push("\r\n\r\n");
        m_buff.push(content);
        return m_buff.view();
    }

    void connect() {
        // reserve 1000 bytes for the out thingy
        m_out.reserve(1000);

        // lots of boring socket config stuff, so much boilerplate
        struct addrinfo hints = {}, *addrs;

        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        if (int rc = getaddrinfo(m_host.c_str(), std::to_string(m_port).c_str(),
                                 &hints, &addrs);
            rc != 0) {
            throw SocketClientException(std::string(gai_strerror(rc)));
        }

        for (addrinfo* addr = addrs; addr != NULL; addr = addr->ai_next) {
            m_sockfd =
                socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

            if (m_sockfd == -1)
                break;

            // set nonblocking socket flag
            int flag = 1;
            if (setsockopt(m_sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag,
                           sizeof(int)) < 0) {
                std::cerr << "Error setting TCP_NODELAY" << std::endl;
            } else {
                if constexpr (verbose) {
                    std::cout << "Set TCP_NODELAY" << std::endl;
                }
            }

            // try and connect
            if (::connect(m_sockfd, addr->ai_addr, addr->ai_addrlen) == 0)
                break;

            close(m_sockfd);
            m_sockfd = -1;
        }

        if (m_sockfd == -1)
            throw SocketClientException("Failed to connect to server.");

        // ssl boilerplate
        const SSL_METHOD* meth = TLS_client_method();
        m_ctx = SSL_CTX_new(meth);
        m_ssl = SSL_new(m_ctx);

        if (!m_ssl)
            throw SocketClientException("Failed to create SSL.");

        SSL_set_tlsext_host_name(m_ssl, m_host.c_str());

        m_sslsock = SSL_get_fd(m_ssl);
        SSL_set_fd(m_ssl, m_sockfd);

        if (SSL_connect(m_ssl) <= 0) {
            throw SocketClientException(get_ssl_error());
        }

        if constexpr (verbose) {
            std::cout << "SSL connection using " << SSL_get_cipher(m_ssl)
                      << std::endl;
        }

        freeaddrinfo(addrs);

        fcntl(m_sockfd, F_SETFL, O_NONBLOCK);
    }

    void disconnect() {
        if (!(m_sockfd < 0))
            close(m_sockfd);
        if (m_ctx)
            SSL_CTX_free(m_ctx);
        if (m_ssl) {
            SSL_shutdown(m_ssl);
            SSL_free(m_ssl);
        }
    }

  public:
    SocketClient(Handler& handler, const std::string host,
                 const long port = 443)
        : m_host(host), m_port(port), m_parser(handler) {
        connect();
    }

    SocketClient(const SocketClient&) = delete;
    SocketClient& operator=(const SocketClient&) = delete;

    SocketClient(SocketClient&& other)
        : m_host(std::move(other.m_host)), m_sockfd(other.m_sockfd),
          m_sslsock(other.m_sslsock), m_ctx(other.m_ctx), m_ssl(other.m_ssl),
          m_out(std::move(other.m_out)) {
        other.m_sockfd = -1;
        other.m_sslsock = -1;
        other.m_ctx = nullptr;
        other.m_ssl = nullptr;
    }

    SocketClient& operator=(SocketClient&& other) {
        disconnect();

        m_host = std::move(other.m_host);
        m_sockfd = other.m_sockfd;
        m_sslsock = other.m_sslsock;
        m_ctx = other.m_ctx;
        m_ssl = other.m_ssl;
        m_out = std::move(other.m_out);

        other.m_sockfd = -1;
        other.m_sslsock = -1;
        other.m_ctx = nullptr;
        other.m_ssl = nullptr;
    }

    // sends a request - forces the socket to fully send everything
    int send_request(const std::string_view& req) {
        const char* buf = req.data();
        int to_send = req.length();
        int sent = 0;
        while (to_send > 0) {
            const int len = SSL_write(m_ssl, buf + sent, to_send);
            if (len < 0) {
                int err = SSL_get_error(m_ssl, len);
                switch (err) {
                case SSL_ERROR_WANT_WRITE:
                    throw SocketClientException("SSL_ERROR_WANT_WRITE");
                case SSL_ERROR_WANT_READ:
                    throw SocketClientException("SSL_ERROR_WANT_READ");
                case SSL_ERROR_ZERO_RETURN:
                    throw SocketClientException("SSL_ERROR_ZERO_RETURN");
                case SSL_ERROR_SYSCALL:
                    throw SocketClientException("SSL_ERROR_SYSCALL");
                case SSL_ERROR_SSL:
                    throw SocketClientException("SSL_ERROR_SSL");
                default:
                    throw SocketClientException("UNKNOWN SSL ERROR");
                }
            }
            to_send -= len;
            sent += len;
        }
        return sent;
    }

    int get(const std::string& path, const std::string& extra_headers = "") {
        return send_request(
            construct_http_request("GET", path, m_host, "", extra_headers));
    }

    int post(const std::string& path, const std::string& content_type,
             const std::string& content,
             const std::string& extra_headers = "") {
        return send_request(construct_http_request(
            "POST", path, m_host, content,
            extra_headers + "Content-Type: " + content_type + "\r\n"));
    }

    int put(const std::string& path, const std::string& content_type,
            const std::string& content, const std::string& extra_headers = "") {
        return send_request(construct_http_request(
            "PUT", path, m_host, content,
            extra_headers + "Content-Type: " + content_type + "\r\n"));
    }

    int patch(const std::string& path, const std::string& content_type,
              const std::string& content,
              const std::string& extra_headers = "") {
        return send_request(construct_http_request(
            "PATCH", path, m_host, content,
            extra_headers + "Content-Type: " + content_type + "\r\n"));
    }

    int del(const std::string& path, const std::string& extra_headers = "") {
        return send_request(
            construct_http_request("DEL", path, m_host, "", extra_headers));
    }

    int head(const std::string& path, const std::string& extra_headers = "") {
        return send_request(
            construct_http_request("HEAD", path, m_host, "", extra_headers));
    }

    int options(const std::string& path,
                const std::string& extra_headers = "") {
        return send_request(
            construct_http_request("OPTIONS", path, m_host, "", extra_headers));
    }

    // this doesn't update the parser when you call it!
    std::string_view read_buffer(const size_t read_size = 100) {
        size_t read = 0;
        m_out.clear();
        while (true) {
            const size_t original_size = m_out.size();
            m_out.resize(original_size + read_size);
            char* buf = &(m_out.data()[original_size]);
            int rc = SSL_read_ex(m_ssl, buf, read_size, &read);
            m_out.resize(original_size + read);
            if ((read < read_size) || (rc == 0)) {
                break;
            }
        }

        return m_out;
    }

    void poll() {
        m_parser.update(read_buffer());
        m_parser.poll();
        if (!m_parser.connection_alive()) {
            std::cout << "disconnected" << std::endl;
            disconnect();
            connect();
            m_parser.set_connected();
        }
    }

    HttpParser<Handler>& parser() { return m_parser; }

    const HttpParser<Handler>& parser() const { return m_parser; }

    ~SocketClient() { disconnect(); }
};

} // namespace fastrest

#endif // _FASTREST_FASTREST_HPP_