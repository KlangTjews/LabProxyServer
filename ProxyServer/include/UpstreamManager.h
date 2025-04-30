#pragma once

#include <string>
#include <string.h>
#include <unordered_map>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <mutex>
#include <iostream>
#include "Singleton.h"

class UpstreamManager : public Singleton<UpstreamManager> {
    friend class Singleton<UpstreamManager>;
public:
    ~UpstreamManager();
    // 根据 URL 创建到上游服务器的连接，返回 upstream_fd
    int get_upstream_fd(const std::string& url);

private:
    bool parse_url(const std::string& url, std::string& host, int& port);
    int connect_to_upstream(const std::string& host, int port);

    // 维护所有活跃的上游连接（可选：用于资源回收）
    std::unordered_map<int, std::string> _active_connections; // fd -> "host:port"
    std::mutex _mutex;
};
