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
        std::cout << "New connection from ip: " << ip << ", port: " << ntohs(client_addr.sin_port) << ", fd: " << client_fd << std::endl;
    }
}

void ConnectionManager::handle_io_event(int fd, uint32_t events, int epfd) {
    std::cout << "handle_io_event, fd = " << fd << ", events = " << events << std::endl;

    ConnCtx* ctx = get_conn(fd);
    if (!ctx) {
        std::cerr << "No context for fd " << fd << std::endl;
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
        ctx->in_buf.append(buf, n);
        
        std::cout << "buffer context: " << std::endl << ctx->in_buf.data() << std::endl;

        // 尝试解析请求
        while (true) {
            auto buf_view = ctx->in_buf.peek(); // 不动缓冲区
            if (buf_view.size() == 0) break;

            HTTPRequest req;
            size_t consumed = 0;
            if (!req.parse(buf_view.data(), buf_view.size(), consumed)) break; // 数据不足
            
            ctx->in_buf.consume(consumed);
            // 请求成功，放入 pipeline
            ctx->pipeline.push(req);
            // 构造响应（此处简化处理）
            std::string body = "Hello, HTTP!\n";
            std::string response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: " + std::to_string(body.size()) + "\r\n"
                "Connection: close\r\n"
                "\r\n" + body;

            ctx->out_buf.append(response.c_str(), response.size());
        }

        // 修改 epoll，添加可写事件
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.fd = fd;
        epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
    }

    // ---------- 可写事件 ----------
    if (events & EPOLLOUT) {
        while (!ctx->out_buf.empty()) {
            ssize_t n = write(fd, ctx->out_buf.data(), ctx->out_buf.size());
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                perror("write");
                close(fd);
                remove_conn(fd);
                return;
            }
            ctx->out_buf.consume(n);
        }

        // 如果写完了，只监听读事件
        if (ctx->out_buf.empty()) {
            epoll_event ev{};
            ev.events = EPOLLIN | EPOLLET;
            ev.data.fd = fd;
            epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
        }
    }

    // ---------- 错误/挂起事件 ----------
    if (events & (EPOLLERR | EPOLLHUP)) {
        std::cerr << "epoll error/hup on fd " << fd << std::endl;
        close(fd);
        remove_conn(fd);
    }
}