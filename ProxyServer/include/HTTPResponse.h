#pragma once

#include <string>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <cctype>

// 解析状态机状态
enum class ResponseParseState { STATUS_LINE, HEADERS, BODY, DONE, ERROR };

class HTTPResponse {
public:
    HTTPResponse();

    /**
     * 解析 data[0..len) 中的一条 HTTP 响应。
     * @param data         输入数据指针。
     * @param len          输入数据长度。
     * @param out_consumed 成功解析并完整时，输出本条响应消费的字节数。
     * @return 完整解析一条响应时返回 true，数据不够或出错返回 false。
     */
    bool parse(const char* data, size_t len, size_t& out_consumed);

    bool is_complete() const;
    const std::string& version() const;
    int status_code() const;
    const std::string& reason_phrase() const;
    const std::unordered_map<std::string, std::string>& headers() const;
    const std::string& body() const;

private:
    bool parse_status_line(const char* data, size_t len, size_t& used);
    bool parse_headers(const char* data, size_t len, size_t& used);
    bool parse_body(const char* data, size_t len, size_t& used);
    bool parse_chunked_body(const char* data, size_t len, size_t& used);
    void finalize();

    ResponseParseState _state;
    bool _chunked;
    size_t _content_length;

    std::string _version;
    int         _status_code;
    std::string _reason_phrase;
    std::unordered_map<std::string, std::string> _headers;
    std::string _body;
};
