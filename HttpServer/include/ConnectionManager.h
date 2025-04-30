#pragma once

#include <iostream>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <regex>

#include "Singleton.h"
#include "Buffer.h"
#include "HTTPRequest.h"

struct ConnCtx {
    int client_fd = -1;
    int upstream_fd = -1;
    Buffer in_buf;
    Buffer out_buf;
    std::queue<HTTPRequest> pipeline;
    bool keep_alive = true;
};

class ConnectionManager : public Singleton<ConnectionManager> {
    friend class Singleton<ConnectionManager>;

public:
    ~ConnectionManager();
    // 注册一个连接
    void register_conn(int listen_fd, ConnCtx* ctx);
    // 查找连接上下文
    ConnCtx* get_conn(int fd);
    // 移除并释放连接上下文
    void remove_conn(int fd);
    // 清空全部连接
    void clear_all();

    void accept_new_conn(int fd, int epfd);
    void handle_io_event(int fd, uint32_t events, int epfd);
    void handle_request(ConnCtx* ctx, HTTPRequest& req);
private:
    std::string load_file(const std::string& path);
    bool is_valid_body(const std::string& body, const std::string& content_type);
    std::string build_http_response(int status_code, const std::string& content_type, const std::string& body);
    std::string get_status_text(int code);
    std::string get_mime_type(const std::string& path);
    std::string minify_json(const std::string& json);
    
    bool ends_with(const std::string& str, const std::string& suffix) {
        if (str.size() < suffix.size()) return false;
        return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
    }
    
    std::unordered_map<int, ConnCtx*> _connections;
    std::mutex _mutex;  // 线程池场景下，必须加锁保护
};
