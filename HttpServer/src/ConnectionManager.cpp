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

    // 读事件
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

        // 解析 HTTP 请求
        while (true) {
            auto view = ctx->in_buf.peek();
            if (view.empty()) break;

            HTTPRequest req;
            size_t consumed = 0;
            if (!req.parse(view.data(), view.size(), consumed)) break;

            ctx->in_buf.consume(consumed);
            ctx->pipeline.push(req);
        }

        // 每个解析成功的 HTTPRequest，生成对应响应
        while (!ctx->pipeline.empty()) {
            HTTPRequest& req = ctx->pipeline.front();
            handle_request(ctx, req);  // 👈【重点!!!】本地处理，生成 out_buf
            ctx->pipeline.pop();
        }

        // 注册可写
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.fd = fd;
        epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
    }

    // 写事件
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

        if (ctx->out_buf.empty()) {
            epoll_event ev{};
            ev.events = EPOLLIN | EPOLLET;
            ev.data.fd = fd;
            epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);

            if (!ctx->keep_alive) {
                close(fd);
                remove_conn(fd);
            }
        }
    }

    if (events & (EPOLLERR | EPOLLHUP)) {
        close(fd);
        remove_conn(fd);
    }
}

void ConnectionManager::handle_request(ConnCtx* ctx, HTTPRequest& req) {
    std::string method = req.method();
    std::string path = req.path();
    std::unordered_map<std::string, std::string> head = req.headers();
    std::string content_type = head["Content-Type"];

    std::string body;
    int status_code = 200;
    std::string response_content_type = "application/json"; // 默认是 JSON

    if (method != "GET" && method != "POST") {
        body = load_file("static/501.html");
        status_code = 501;
        response_content_type = "text/html";
    }
    else if (method == "POST" && path == "/api/upload") {
        if (content_type == "application/json" || content_type == "application/x-www-form-urlencoded") {
            if (is_valid_body(req.body(), content_type)) {
                body = req.body(); // 原样返回
            } else {
                body = load_file("data/error.json");
                status_code = 404;
            }
        }
        else {
            body = load_file("data/error.json");
            status_code = 404;
        }
    }
    else if (method == "GET") {
        // 处理静态文件 GET
        if (path == "/" || path == "/index.html") {
            path = "/index.html";
        }

        std::string file_path = "static" + path;
        body = load_file(file_path);

        if (body == "<h1>File Not Found</h1>") {
            body = load_file("static/404.html");
            status_code = 404;
            response_content_type = "text/html";
        } else {
            status_code = 200;
            response_content_type = get_mime_type(file_path);
            
            if (response_content_type == "application/json") {
                body = minify_json(body);
            }
        }
    }
    else {
        body = load_file("static/404.html");
        status_code = 404;
        response_content_type = "text/html";
    }

    std::string response = build_http_response(status_code, response_content_type, body);
    ctx->out_buf.append(response.data(), response.size());
}

std::string ConnectionManager::load_file(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return "<h1>File Not Found</h1>";
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

bool ConnectionManager::is_valid_body(const std::string& body, const std::string& content_type) {
    if (content_type == "application/json") {
        // 简单校验 JSON 格式：必须是以 { 开头，以 } 结尾
        return !body.empty() && body.front() == '{' && body.back() == '}';
    } else if (content_type == "application/x-www-form-urlencoded") {
        // 简单校验 key=value&key2=value2 格式
        return std::regex_match(body, std::regex("([a-zA-Z0-9_]+=[^&]*&?)+"));
    }
    return false;
}

std::string ConnectionManager::build_http_response(int status_code, const std::string& content_type, const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code << " " << get_status_text(status_code) << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::string ConnectionManager::get_status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 404: return "Not Found";
        case 501: return "Not Implemented";
        default: return "Unknown";
    }
}

std::string ConnectionManager::get_mime_type(const std::string& path) {
    if (ends_with(path, ".html")) return "text/html";
    if (ends_with(path, ".css")) return "text/css";
    if (ends_with(path, ".js")) return "text/javascript";
    if (ends_with(path, ".json")) return "application/json";
    return "text/html"; // 默认
}

std::string ConnectionManager::minify_json(const std::string& json) {
    std::string result;
    for (char c : json) {
        if (c != '\n' && c != '\r' && c != '\t') {
            result += c;
        }
    }
    return result;
}
