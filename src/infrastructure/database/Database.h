#pragma once

#include <filesystem>
#include <string>

struct sqlite3;

namespace modra {

class Database {
public:
    explicit Database(const std::filesystem::path& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) = delete;
    Database& operator=(Database&&) = delete;

    void apply_migrations();
    int migration_count() const;
    std::string sqlite_version() const;
    sqlite3* handle() const;

private:
    void execute(const char* sql) const;
    int scalar_int(const char* sql) const;

    sqlite3* connection_ = nullptr;
};

}  // namespace modra
