#include "minidrive/error_codes.hpp"
#include <map>

namespace minidrive {

static std::map<uint32_t, error_code*> codeMap;


error_code::error_code(uint32_t code, const std::string &what)
    :_code(code), _what(what) {
        codeMap[code] = this;
    }

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



const error_code* getErrorByCode(uint32_t code) {
    if (codeMap.find(code) == codeMap.end()) return &error::UNKNOWN;
    return codeMap[code];
}

} // namespace minidrive




const minidrive::error_code minidrive::error::UNKNOWN(1, "unknown");
const minidrive::error_code minidrive::error::SUCCESS(0, "success");
const minidrive::error_code minidrive::error::USER_NOT_FOUND(1000, "username not found");
const minidrive::error_code minidrive::error::INCORRECT_PASSWORD(1001, "incorrect password");
const minidrive::error_code minidrive::error::USER_ALREADY_EXISTS(1002, "username already exists");
const minidrive::error_code minidrive::error::USER_REGISTER(1003, "could not register user");
const minidrive::error_code minidrive::error::ALREADY_AUTHENTICATED(1004, "already authenticated");

const minidrive::error_code minidrive::error::JSON_TYPE_ERROR(1100, "json type error");
const minidrive::error_code minidrive::error::UNKNOWN_COMMAND(1101, "uknown command");
const minidrive::error_code minidrive::error::MISSING_ARGUMENT(1102, "missing argument");
const minidrive::error_code minidrive::error::JSON_PARSE_ERROR(1103, "json parse error");
    
const minidrive::error_code minidrive::error::ACCESS_DENIED(1200, "access denied");
const minidrive::error_code minidrive::error::TARGET_NOT_FOUND(1201, "target does not exist");
const minidrive::error_code minidrive::error::FS_ERROR(1202, "filesystem error");
const minidrive::error_code minidrive::error::TARGET_ALREADY_EXISTS(1203, "target already exists");