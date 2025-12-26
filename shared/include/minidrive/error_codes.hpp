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
    inline const error_code SUCCESS(0, "success");
    inline const error_code USER_NOT_FOUND(1000, "username not found");
    inline const error_code INCORRECT_PASSWORD(1001, "incorrect password");
    inline const error_code USER_ALREADY_EXISTS(1002, "username already exists");
    inline const error_code USER_REGISTER(1003, "could not register user");
    inline const error_code ALREADY_AUTHENTICATED(1004, "already authenticated");

    inline const error_code JSON_TYPE_ERROR(1100, "json type error");
    inline const error_code UNKNOWN_COMMAND(1101, "uknown command");
    inline const error_code MISSING_ARGUMENT(1102, "missing argument");
    inline const error_code JSON_PARSE_ERROR(1103, "json parse error");
    
    inline const error_code ACCESS_DENIED(1200, "access denied");
    inline const error_code TARGET_NOT_FOUND(1201, "target does not exist");
    inline const error_code FS_ERROR(1202, "filesystem error");
    inline const error_code TARGET_ALREADY_EXISTS(1203, "target already exists");
    
}

} // namespace minidrive