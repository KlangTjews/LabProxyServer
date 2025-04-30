#pragma once

#include <string>
#include <vector>
#include <algorithm>

class Buffer {
public:
    void append(const char* data, size_t len);
    std::string read_until(const std::string& delimiter);
    std::string read_all();
    void consume(size_t n);
    bool empty() const;
    size_t size() const;
    const char* data() const;
    std::string_view peek() const;
    
private:
    std::vector<char> _buffer;
};