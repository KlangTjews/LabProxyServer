#include "Buffer.h"

void Buffer::append(const char* data, size_t len){
    _buffer.insert(_buffer.end(), data, data + len);
}

std::string Buffer::read_until(const std::string& delimiter){
    if (delimiter.empty()) return read_all();

    // 在内部缓冲区中查找 delimiter
    auto it = std::search(_buffer.begin(), _buffer.end(), delimiter.begin(), delimiter.end());

    if (it == _buffer.end()) {
        return "";  // 没找到，返回空字符串，表示“数据不够”
    }

    // 计算读取长度
    size_t len = std::distance(_buffer.begin(), it) + delimiter.length();

    // 拷贝数据并移除
    std::string result(_buffer.begin(), _buffer.begin() + len);
    _buffer.erase(_buffer.begin(), _buffer.begin() + len);
    return result;
}

std::string Buffer::read_all(){
    std::string result(_buffer.begin(), _buffer.end());
    _buffer.clear();
    return result;
}

void Buffer::consume(size_t n) {
    if (!_buffer.empty()) _buffer.erase(_buffer.begin(), _buffer.begin() + n);
}

bool Buffer::empty() const {
    return _buffer.empty();
}

size_t Buffer::size() const {
    return _buffer.size();
}

const char* Buffer::data() const {
    return _buffer.data();
}

std::string_view Buffer::peek() const{
    return std::string_view(_buffer.data(), _buffer.size());
}