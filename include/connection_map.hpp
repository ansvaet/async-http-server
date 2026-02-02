#pragma once

#include <shared_mutex>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <iterator>
#include "connection.hpp"

class ConnectionMap {
public:
    Connection* get(int fd);
    void insert(int fd, std::unique_ptr<Connection> conn);
    void erase(int fd);
    size_t size() const;
    void clear();

    template<typename Func>
    void for_each(Func func) {
        std::shared_lock lock(mutex);
        for (auto& [fd, conn] : map) {
            func(fd, conn.get());
        }
    }
private:
    mutable std::shared_mutex mutex;
    std::unordered_map<int, std::unique_ptr<Connection>> map;
};

