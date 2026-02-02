#include "connection_map.hpp"

Connection* ConnectionMap::get(int fd) {
    std::shared_lock lock(mutex);
    auto current = map.find(fd);
    return current != map.end() ? current->second.get() : nullptr;
}

void ConnectionMap::insert(int fd, std::unique_ptr<Connection> conn) {
    std::unique_lock lock(mutex);
    map[fd] = std::move(conn);
}

void ConnectionMap::erase(int fd) {
    std::unique_lock lock(mutex);
    map.erase(fd);
}

size_t ConnectionMap::size() const {
    std::shared_lock lock(mutex);
    return map.size();
}

void ConnectionMap::clear() {
    std::unique_lock lock(mutex);
    map.clear();
}