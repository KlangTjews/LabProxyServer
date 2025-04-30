#include "HttpServer.h"

int main(int argc, char* argv[]){
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
    addr.sin_port = htons(c_port);
    inet_pton(AF_INET, c_ip.c_str(), &addr.sin_addr);

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
        std::cerr << "[ERROR] Failed to create thread pool" << std::endl;
        return EXIT_FAILURE;
    }

    std::shared_ptr<ConnectionManager> ConnMgr = ConnectionManager::getInstance();
    if (!ConnMgr){
        std::cerr << "[ERROR] Failed to create ConnectionManager" << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "[INIT] ProxyServer has started, ip: " << c_ip << ", port: " << c_port << ", thread nums: " << c_threads << std::endl;

    // 5.转起来了
    std::vector<epoll_event> events(MAX_EVENTS);
    while (true) {
        int n = epoll_wait(epfd, events.data(), MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        
        std::cout << "[STATE] epoll wait: got " << n << " events" << std::endl;

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