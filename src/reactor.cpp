#include "reactor.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <iostream>

Reactor::Reactor() {

    if (pipe(pipe_) == -1) {
        throw std::runtime_error("pipe failed: " + std::string(strerror(errno)));
    }

    for (int i = 0; i < 2; i++) {
        int flags = fcntl(pipe_[i], F_GETFL, 0);
        fcntl(pipe_[i], F_SETFL, flags | O_NONBLOCK);
    }

    //std::cout << "[DEBUG] Reactor pipe created: read="
    //    << pipe_[0] << ", write=" << pipe_[1] << std::endl;
}

Reactor::~Reactor() {
    close(pipe_[0]);
    close(pipe_[1]);
}

void Reactor::notify(int fd, uint32_t events) {
    ReactorNotification notification{ fd, events };


    ssize_t written = write(pipe_[1], &notification, sizeof(notification));

    if (written == -1) {
 
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cerr << "[ERROR] Failed to write to pipe: "
                << strerror(errno) << std::endl;
        }
    }
    else if (written != sizeof(notification)) {
        std::cerr << "[ERROR] Partial write to pipe: "
            << written << " bytes" << std::endl;
    }
}

bool Reactor::read_notification(ReactorNotification& notification) {
    ssize_t read_bytes = read(pipe_[0], &notification, sizeof(notification));

    if (read_bytes == sizeof(notification)) {
        return true; 
    }

    if (read_bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return false; 
    }

    if (read_bytes != -1) {
        std::cerr << "[ERROR] Partial read from pipe: "
            << read_bytes << " bytes" << std::endl;
    }
    return false;
}