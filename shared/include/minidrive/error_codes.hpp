#pragma once
#include <string>
#include <cstdint>

namespace minidrive {

class error_code {
public:
    error_code(uint32_t code, const std::string &what);
    bool operator==(const error_code &rhs) const;
    operator bool() const;
    const std::string& what() const; 
    uint32_t code() const;
    
private:
    uint32_t _code;
    std::string _what;
};

namespace error {
    extern const error_code UNKNOWN;
    extern const error_code SUCCESS;
    extern const error_code USER_NOT_FOUND;
    extern const error_code INCORRECT_PASSWORD;
    extern const error_code USER_ALREADY_EXISTS;
    extern const error_code USER_REGISTER;
    extern const error_code ALREADY_AUTHENTICATED;

    extern const error_code JSON_TYPE_ERROR;
    extern const error_code UNKNOWN_COMMAND;
    extern const error_code MISSING_ARGUMENT;
    extern const error_code JSON_PARSE_ERROR;
    
    extern const error_code ACCESS_DENIED;
    extern const error_code TARGET_NOT_FOUND;
    extern const error_code FS_ERROR;
    extern const error_code TARGET_ALREADY_EXISTS;
}

const error_code* getErrorByCode(uint32_t code);

} // namespace minidrive