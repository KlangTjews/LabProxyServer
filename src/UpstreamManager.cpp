#include "UpstreamManager.h"

UpstreamManager::~UpstreamManager() {
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto& [fd, _] : _active_connections) {
        close(fd); // 关闭所有活跃连接
    }
}

// 解析 URL 提取 host 和 port
bool UpstreamManager::parse_url(const std::string& url, std::string& host, int& port) {
    size_t protocol_pos = url.find("://");
    if (protocol_pos == std::string::npos) {
        std::cerr << "[ERROR] Invalid URL: missing protocol" << std::endl;
        return false;
    }

    size_t host_start = protocol_pos + 3;
    size_t host_end = url.find('/', host_start);
    std::string host_port = (host_end == std::string::npos) ? url.substr(host_start) : url.substr(host_start, host_end - host_start);

    size_t colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        host = host_port.substr(0, colon_pos);
        port = std::stoi(host_port.substr(colon_pos + 1));
    }
    else {
        host = host_port;
        port = 80; // 默认 HTTP 端口
    }
    return true;
}

// 创建并连接到上游服务器的非阻塞套接字
int UpstreamManager::connect_to_upstream(const std::string& host, int port) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (status != 0) {
        std::cerr << "getaddrinfo error: " << gai_strerror(status) << std::endl;
        return -1;
    }

    int sockfd = -1;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;

        // 设置为非阻塞模式
        int flags = fcntl(sockfd, F_GETFL, 0);
        if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
            close(sockfd);
            continue;
        }

        // 发起非阻塞连接
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            if (errno == EINPROGRESS) {
                // 连接进行中，需后续通过 epoll 检查
                break;
            } else {
                close(sockfd);
                sockfd = -1;
                continue;
            }
        }
        break; // 连接立即成功
    }

    freeaddrinfo(res);

    if (sockfd != -1) {
        std::lock_guard<std::mutex> lock(_mutex);
        _active_connections[sockfd] = host + ":" + std::to_string(port);
    }
    return sockfd;
}

// 外部接口：根据 URL 获取上游连接 fd
int UpstreamManager::get_upstream_fd(const std::string& url) {
    std::string host;
    int port;
    if (!parse_url(url, host, port)) return -1;

    return connect_to_upstream(host, port);
}
