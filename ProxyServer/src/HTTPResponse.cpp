#include "HTTPResponse.h"

static int find_crlf(const char* data, size_t len) {
    for (size_t i = 0; i + 1 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') return static_cast<int>(i);
    }
    return -1;
}

HTTPResponse::HTTPResponse()
    : _state(ResponseParseState::STATUS_LINE),
      _chunked(false),
      _content_length(0),
      _status_code(0) {}

bool HTTPResponse::is_complete() const { return _state == ResponseParseState::DONE; }
const std::string& HTTPResponse::version() const { return _version; }
int HTTPResponse::status_code() const { return _status_code; }
const std::string& HTTPResponse::reason_phrase() const { return _reason_phrase; }
const std::unordered_map<std::string, std::string>& HTTPResponse::headers() const { return _headers; }
const std::string& HTTPResponse::body() const { return _body; }

bool HTTPResponse::parse(const char* data, size_t len, size_t& out_consumed) {
    size_t pos = 0, used = 0;

    while (_state != ResponseParseState::DONE && _state != ResponseParseState::ERROR) {
        bool ok = false;
        switch (_state) {
            case ResponseParseState::STATUS_LINE:
                ok = parse_status_line(data + pos, len - pos, used);
                break;
            case ResponseParseState::HEADERS:
                ok = parse_headers(data + pos, len - pos, used);
                break;
            case ResponseParseState::BODY:
                ok = parse_body(data + pos, len - pos, used);
                break;
            default:
                return false;
        }
        if (!ok) return false;
        pos += used;
    }

    if (_state == ResponseParseState::DONE) {
        out_consumed = pos;
        return true;
    }
    return false;
}

bool HTTPResponse::parse_status_line(const char* data, size_t len, size_t& used) {
    int idx = find_crlf(data, len);
    if (idx < 0) return false;

    std::string line(data, idx);
    std::istringstream iss(line);
    if (!(iss >> _version >> _status_code)) {
        _state = ResponseParseState::ERROR;
        return false;
    }
    std::getline(iss, _reason_phrase);
    if (!_reason_phrase.empty() && _reason_phrase[0] == ' ')
        _reason_phrase.erase(0, 1);

    _state = ResponseParseState::HEADERS;
    used = idx + 2;
    return true;
}

bool HTTPResponse::parse_headers(const char* data, size_t len, size_t& used) {
    size_t pos = 0;
    while (true) {
        int idx = find_crlf(data + pos, len - pos);
        if (idx < 0) return false;

        if (idx == 0) {
            pos += 2;
            auto it = _headers.find("content-length");
            if (it != _headers.end()) {
                _content_length = std::stoul(it->second);
                _state = ResponseParseState::BODY;
            } else if (_headers.find("transfer-encoding") != _headers.end() &&
                       _headers["transfer-encoding"] == "chunked") {
                _chunked = true;
                _state = ResponseParseState::BODY;
            } else {
                finalize();
            }
            used = pos;
            return true;
        }

        std::string line(data + pos, idx);
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            _state = ResponseParseState::ERROR;
            return false;
        }
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        auto l = val.find_first_not_of(" \t");
        auto r = val.find_last_not_of(" \t");
        val = (l == std::string::npos ? std::string() : val.substr(l, r - l + 1));
        _headers[key] = val;
        pos += idx + 2;
    }
}

bool HTTPResponse::parse_body(const char* data, size_t len, size_t& used) {
    if (_chunked) return parse_chunked_body(data, len, used);

    if (len < _content_length) return false;
    _body.assign(data, data + _content_length);
    used = _content_length;
    finalize();
    return true;
}

bool HTTPResponse::parse_chunked_body(const char* data, size_t len, size_t& used) {
    size_t pos = 0;
    while (true) {
        int idx = find_crlf(data + pos, len - pos);
        if (idx < 0) return false;

        std::string line(data + pos, idx);
        size_t chunk_size;
        try {
            chunk_size = std::stoul(line, nullptr, 16);
        } catch (...) {
            _state = ResponseParseState::ERROR;
            return false;
        }
        pos += idx + 2;

        if (chunk_size == 0) {
            if (len - pos < 2) return false;
            pos += 2;
            finalize();
            used = pos;
            return true;
        }
        if (len - pos < chunk_size + 2) return false;

        _body.append(data + pos, chunk_size);
        pos += chunk_size;
        if (data[pos] != '\r' || data[pos + 1] != '\n') {
            _state = ResponseParseState::ERROR;
            return false;
        }
        pos += 2;
    }
}

void HTTPResponse::finalize() {
    _state = ResponseParseState::DONE;
}
