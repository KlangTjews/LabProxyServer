#pragma once
#include <string>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <cctype>

// 解析状态机状态
enum class ParseState { REQUEST_LINE, HEADERS, BODY, DONE, ERROR };

class HTTPRequest {
public:
    HTTPRequest();

    /**
     * 解析 data[0..len) 中的一条 HTTP 请求。
     * @param data         输入数据指针。
     * @param len          输入数据长度。
     * @param out_consumed 成功解析并完整时，输出本条请求消费的字节数。
     * @return 完整解析一条请求时返回 true，数据不够或出错返回 false。
     */
    bool parse(const char* data, size_t len, size_t& out_consumed);

    bool is_complete() const;
    bool keep_alive()  const;
    const std::string& method()  const;
    const std::string& path()    const;
    const std::string& version() const;
    const std::string& body()    const;
    const std::unordered_map<std::string, std::string>& headers() const;

private:
    bool parse_request_line(const char* data, size_t len, size_t& used);
    bool parse_headers     (const char* data, size_t len, size_t& used);
    bool parse_body        (const char* data, size_t len, size_t& used);
    bool parse_chunked_body(const char* data, size_t len, size_t& used);
    void finalize();

    ParseState _state;
    bool       _chunked;
    bool       _keep_alive;
    size_t     _content_length;

    // 解析结果
    std::string _method;
    std::string _path;
    std::string _version;
    std::unordered_map<std::string, std::string> _headers;
    std::string _body;
};
