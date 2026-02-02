#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace bdsmysql {

struct DatabaseConfig {
    std::string host;
    int         port;
    std::string database;
    std::string username;
    std::string password;
    std::string charset;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(DatabaseConfig, host, port, database, username, password, charset)
};

class Config {
public:
    static Config& getInstance();

    bool load();
    bool save();
    void createDefaultConfig();

    const DatabaseConfig& getDatabaseConfig() const { return mDatabaseConfig; }

private:
    Config()  = default;
    ~Config() = default;

    Config(const Config&)            = delete;
    Config& operator=(const Config&) = delete;

    DatabaseConfig mDatabaseConfig;
    std::string    mConfigPath;
};

} // namespace bdsmysql