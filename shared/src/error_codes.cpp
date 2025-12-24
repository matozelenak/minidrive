#include "minidrive/error_codes.hpp"
#include <map>

namespace minidrive {

error_code::error_code(uint32_t code, const std::string &what)
    :_code(code), _what(what) {}

bool error_code::operator==(const error_code &rhs) const {
    return _code == rhs._code;
}

error_code::operator bool() const {
    return _code != 0;
}

const std::string& error_code::what() const {
    return _what;
}

uint32_t error_code::code() const {
    return _code;
}

} // namespace minidrive