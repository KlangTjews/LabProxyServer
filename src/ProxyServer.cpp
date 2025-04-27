#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <getopt.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include "ThreadPool.h"
#include "ConnectionManager.h"

constexpr int MAX_EVENTS = 65535;

// 全局配置
std::string g_ip;
int g_port = 0;
int g_thread_count = 0;
std::string g_proxy_url;

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 参数解析
void parse_args(int argc, char* argv[]) {
    static struct option long_opts[] = {
        {"ip",      required_argument, nullptr, 'i'},
        {"port",    required_argument, nullptr, 'p'},
        {"threads", required_argument, nullptr, 't'},
        {"proxy",   required_argument, nullptr,  0 },
        {0, 0, nullptr, 0}
    };

    int opt, idx;
    while ((opt = getopt_long(argc, argv, "i:p:t:", long_opts, &idx)) != -1) {
        switch (opt) {
            case 'i': g_ip = optarg; break;
            case 'p': g_port = std::atoi(optarg); break;
            case 't': g_thread_count = std::atoi(optarg); break;
            case 0:
                if (std::string(long_opts[idx].name) == "proxy") {
                    g_proxy_url = optarg;
                }
                break;
            default:
                std::cerr << "Usage: " << argv[0] << " --ip <IP> --port <PORT> --threads <N> [--proxy <URL>]" << std::endl;
                std::exit(EXIT_FAILURE);
        }
    }

    if (g_ip.empty() || g_port <= 0 || g_thread_count <= 0) {
        std::cerr << "Missing required parameters" << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

int main(int argc, char* argv[]) {
    // 1.解析参数
    parse_args(argc, argv);

    // 2.创建监听socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)); // 设置复用socket

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_port);
    inet_pton(AF_INET, g_ip.c_str(), &addr.sin_addr);

    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 || listen(listen_fd, SOMAXCONN) < 0) {
        perror("bind/listen");
        close(listen_fd);
        return EXIT_FAILURE;
    }
    set_nonblocking(listen_fd); //非阻塞

    // 3.初始化epoll
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        return EXIT_FAILURE;
    }

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    // 4.懒汉模式初始化
    std::shared_ptr<ThreadPool> pool = ThreadPool::getInstance();
    if (!pool) {
        std::cerr << "Failed to create thread pool" << std::endl;
        return EXIT_FAILURE;
    }

    std::shared_ptr<ConnectionManager> ConnMgr = ConnectionManager::getInstance();
    if (!ConnMgr){
        std::cerr << "Failed to create ConnectionManager" << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "ProxyServer has started, ip: " << g_ip << ", port: " << g_port << ", thread nums: " << g_thread_count << ", upstream server: " << g_proxy_url << std::endl;

    // 5.转起来了
    std::vector<epoll_event> events(MAX_EVENTS);
    while (true) {
        int n = epoll_wait(epfd, events.data(), MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        
        std::cout << "epoll wait: got " << n << " events" << std::endl;

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t evs = events[i].events;

            if (fd == listen_fd) { //新连接
                pool->commit([ConnMgr, listen_fd, epfd]() {
                    ConnMgr->accept_new_conn(listen_fd, epfd);
                });
            }
            else { //已有连接
                pool->commit([ConnMgr, fd, evs, epfd]() {
                    ConnMgr->handle_io_event(fd, evs, epfd);
                });
            }
        }
    }

    // 6.清理
    close(listen_fd);
    close(epfd);
    return 0;
}
