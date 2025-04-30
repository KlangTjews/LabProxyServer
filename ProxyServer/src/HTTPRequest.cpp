#include "HTTPRequest.h"

// 在 data[0..len) 中查找 "\r\n"，返回位置或 -1
static int find_crlf(const char* data, size_t len) {
    for (size_t i = 0; i + 1 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') return static_cast<int>(i);
    }
    return -1;
}

HTTPRequest::HTTPRequest()
    : _state(ParseState::REQUEST_LINE),
      _chunked(false),
      _keep_alive(false),
      _content_length(0) {}

bool HTTPRequest::is_complete() const { return _state == ParseState::DONE; }
bool HTTPRequest::keep_alive()  const { return _keep_alive; }
const std::string& HTTPRequest::method()  const { return _method; }
const std::string& HTTPRequest::path()    const { return _path; }
const std::string& HTTPRequest::version() const { return _version; }
const std::string& HTTPRequest::body()    const { return _body; }
const std::unordered_map<std::string, std::string>& HTTPRequest::headers() const { return _headers; }
ParseState HTTPRequest::state() const { return _state; }

std::string HTTPRequest::raw(){
    std::string result;

    result += _method + " " + _path + " " + _version + "\r\n";
    // 请求头
    for (const auto& [key, value] : _headers) {
        result += key + ": " + value + "\r\n";
    }
    // 空行
    result += "\r\n";
    // 请求体（如果有）
    result += _body;

    return result;
}

bool HTTPRequest::parse(const char* data, size_t len, size_t& out_consumed) {
    size_t pos = 0, used = 0;

    while (_state != ParseState::DONE && _state != ParseState::ERROR) {
        bool ok = false;
        switch (_state) {
            case ParseState::REQUEST_LINE:
                ok = parse_request_line(data + pos, len - pos, used);
                break;
            case ParseState::HEADERS:
                ok = parse_headers(data + pos, len - pos, used);
                break;
            case ParseState::BODY:
                ok = parse_body(data + pos, len - pos, used);
                break;
            default:
                return false;
        }
        if (!ok) return false;
        pos += used;
    }

    if (_state == ParseState::DONE) {
        out_consumed = pos;
        return true;
    }
    return false;
}

bool HTTPRequest::parse_request_line(const char* data, size_t len, size_t& used) {
    int idx = find_crlf(data, len);
    if (idx < 0) return false;

    std::string line(data, idx);
    std::istringstream iss(line);
    if (!(iss >> _method >> _path >> _version)) {
        _state = ParseState::ERROR;
        return false;
    }

    _state = ParseState::HEADERS;
    used = idx + 2;  // 包含 "\r\n"
    return true;
}

bool HTTPRequest::parse_headers(const char* data, size_t len, size_t& used) {
    size_t pos = 0;
    while (true) {
        int idx = find_crlf(data + pos, len - pos);
        if (idx < 0) return false;

        // 空行 => headers 结束
        if (idx == 0) {
            pos += 2;
            auto it = _headers.find("content-length");
            if (it != _headers.end()) {
                _content_length = std::stoul(it->second);
                _state = ParseState::BODY;
            } else if (_headers.find("transfer-encoding") != _headers.end() &&
                       _headers["transfer-encoding"] == "chunked") {
                _chunked = true;
                _state = ParseState::BODY;
            } else {
                finalize();
            }
            used = pos;
            return true;
        }

        std::string line(data + pos, idx);
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            _state = ParseState::ERROR;
            return false;
        }
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        // 标准化 key
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        // 去除 val 前后空白
        auto l = val.find_first_not_of(" \t");
        auto r = val.find_last_not_of(" \t");
        val = (l == std::string::npos ? std::string() : val.substr(l, r - l + 1));
        _headers[key] = val;
        pos += idx + 2;
    }
}

bool HTTPRequest::parse_body(const char* data, size_t len, size_t& used) {
    if (_chunked) return parse_chunked_body(data, len, used);

    if (len < _content_length) return false;
    _body.assign(data, data + _content_length);
    used = _content_length;
    finalize();
    return true;
}

bool HTTPRequest::parse_chunked_body(const char* data, size_t len, size_t& used) {
    size_t pos = 0;
    while (true) {
        int idx = find_crlf(data + pos, len - pos);
        if (idx < 0) return false;

        std::string line(data + pos, idx);
        size_t chunk_size;
        try {
            chunk_size = std::stoul(line, nullptr, 16);
        } catch (...) {
            _state = ParseState::ERROR;
            return false;
        }
        pos += idx + 2;

        if (chunk_size == 0) {
            if (len - pos < 2) return false;  // 末尾 CRLF
            pos += 2;
            finalize();
            used = pos;
            return true;
        }
        if (len - pos < chunk_size + 2) return false;

        _body.append(data + pos, chunk_size);
        pos += chunk_size;
        // 跳过 chunk 末尾 CRLF
        if (data[pos] != '\r' || data[pos+1] != '\n') {
            _state = ParseState::ERROR;
            return false;
        }
        pos += 2;
    }
}

void HTTPRequest::finalize() {
    if (_version == "HTTP/1.1") {
        auto it = _headers.find("connection");
        _keep_alive = (it == _headers.end() || it->second != "close");
    } else {
        auto it = _headers.find("connection");
        _keep_alive = (it != _headers.end() && it->second == "keep-alive");
    }
    _state = ParseState::DONE;
}

void HTTPRequest::reset() {
    _state = ParseState::REQUEST_LINE;
    _chunked = false;
    _keep_alive = false;
    _content_length = 0;
    _method.clear();
    _path.clear();
    _version.clear();
    _headers.clear();
    _body.clear();
}