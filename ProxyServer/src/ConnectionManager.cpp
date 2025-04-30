#include "ConnectionManager.h"

ConnectionManager::~ConnectionManager(){
    clear_all();
}

void ConnectionManager::register_conn(int fd, ConnCtx* ctx){
    std::lock_guard<std::mutex> lock(_mutex); // 保证线程安全
    _connections[fd] = ctx;
    std::cout << "Registered client_fd " << fd << " to epoll" << std::endl;
}

ConnCtx* ConnectionManager::get_conn(int fd){
    std::lock_guard<std::mutex> lock(_mutex); // 保证线程安全
    auto it = _connections.find(fd);
    return it != _connections.end() ? it->second : nullptr;
}

void ConnectionManager::remove_conn(int fd){
    std::lock_guard<std::mutex> lock(_mutex); // 保证线程安全
    auto it = _connections.find(fd);
    if (it != _connections.end()) {
        delete it->second;
        _connections.erase(it);
    }
}

void ConnectionManager::clear_all(){
    std::lock_guard<std::mutex> lock(_mutex); // 保证线程安全
    for (auto& [fd, ctx] : _connections) {
        delete ctx;
    }
    _connections.clear();
}

void ConnectionManager::accept_new_conn(int listen_fd, int epfd){
    while(true){
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        int client_fd = accept4(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &addrlen, SOCK_NONBLOCK);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 所有连接已处理完
                break;
            }
            else {
                perror("accept");
                break;
            }
        }

        std::cout << "accept new conn, client_fd = " << client_fd << std::endl;

        // 创建连接上下文 ConnCtx
        ConnCtx* ctx = new ConnCtx();
        ctx->client_fd = client_fd;
        ctx->upstream_fd = -1; // 默认不启用上游
        ctx->keep_alive = true; // 默认启用 keep-alive，可根据 header 再决定
        // 注册到全局管理表（例如 map<int, ConnCtx*>）
        register_conn(client_fd, ctx);

        // 注册进 epoll 监听
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client_fd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
            perror("epoll_ctl (add client)");
            close(client_fd);
            remove_conn(client_fd);
            delete ctx;
            continue;
        }

        // 可以打印客户端信息（可选）
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        std::cout << "[STATE] New connection from ip: " << ip << ", port: " << ntohs(client_addr.sin_port) << ", fd: " << client_fd << std::endl;
    }
}

void ConnectionManager::handle_io_event(int fd, uint32_t events, int epfd) {
    ConnCtx* ctx = get_conn(fd);
    if (!ctx){
        std::cerr << "[ERROR] no context for fd" << std::endl;
        return;
    };

    bool is_client = (fd == ctx->client_fd);
    bool is_upstream = (fd == ctx->upstream_fd);
    if (!is_client && !is_upstream) {
        std::cerr << "[ERROR] cant distinguish is_client or is_upstream" << std::endl;
        return;
    }

    // ---------- 可读事件 ----------
    if (events & EPOLLIN) {
        char buf[4096];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) {
            if (n < 0) perror("read");
            close(fd);
            remove_conn(fd);
            return;
        }

        if (is_client) {
            ctx->in_buf.append(buf, n);
            // 尝试解析请求
            while (true) {
                auto view = ctx->in_buf.peek();
                if (view.empty()) break;

                HTTPRequest req;
                size_t consumed = 0;
                if (!req.parse(view.data(), view.size(), consumed)) break;

                ctx->in_buf.consume(consumed);
                ctx->pipeline.push(req);

                if (ctx->upstream_fd == -1) {
                    ctx->upstream_fd = connect_to_upstream("127.0.0.1", 8888);
                    if (ctx->upstream_fd < 0) {
                        std::cerr << "[ERROR] connect upstream failed" << std::endl;
                        close(fd);
                        remove_conn(fd);
                        return;
                    }

                    register_conn(ctx->upstream_fd, ctx);
                    epoll_event ev{};
                    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
                    ev.data.fd = ctx->upstream_fd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, ctx->upstream_fd, &ev);
                }

                std::string raw_req = req.raw();
                ctx->upstream_out_buf.append(raw_req.data(), raw_req.size());
            }

            // 注册上游为可写
            epoll_event ev{};
            ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
            ev.data.fd = ctx->upstream_fd;
            epoll_ctl(epfd, EPOLL_CTL_MOD, ctx->upstream_fd, &ev);
        }
        else {
            // 是 upstream 返回的响应
            ctx->upstream_in_buf.append(buf, n);
            // 将 upstream 的响应全部塞进 client 的 out_buf
            ctx->out_buf.append(ctx->upstream_in_buf.data(), ctx->upstream_in_buf.size());
            ctx->upstream_in_buf.read_all();

            epoll_event ev{};
            ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
            ev.data.fd = ctx->client_fd;
            epoll_ctl(epfd, EPOLL_CTL_MOD, ctx->client_fd, &ev);
        }
    }

    // ---------- 可写事件 ----------
    if (events & EPOLLOUT) {
        Buffer& buf = is_client ? ctx->out_buf : ctx->upstream_out_buf;
        while (!buf.empty()) {
            ssize_t n = write(fd, buf.data(), buf.size());
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                perror("write");
                close(fd);
                remove_conn(fd);
                return;
            }
            
            buf.consume(n);
        }

        if (buf.empty()) {
            epoll_event ev{};
            ev.events = EPOLLIN | EPOLLET;
            ev.data.fd = fd;
            epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
        }
    }

    // ---------- 错误事件 ----------
    if (events & (EPOLLERR | EPOLLHUP)) {
        std::cerr << "epoll error/hup on fd " << fd << std::endl;
        close(fd);
        remove_conn(fd);
    }
}

int ConnectionManager::connect_to_upstream(const std::string& ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock < 0) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            close(sock);
            return -1;
        }
    }
    return sock;
}