#pragma once
#include <string>
#include <filesystem>

inline const std::string USERS_FILE = "users.json";
inline const std::string USERDATA_DIR = "user_data";
inline const std::string PUBLIC_DIR = "_public";


inline std::filesystem::path ROOT_DIR_PATH;
inline std::filesystem::path USERS_FILE_PATH;
inline std::filesystem::path PUBLIC_DIR_PATH;
inline std::filesystem::path USERDATA_DIR_PATH;
