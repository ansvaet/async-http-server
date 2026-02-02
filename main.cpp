#include "server.hpp"
#include "connection.hpp"
#include <sys/epoll.h>
#include <iostream>
#include <cstring>
#include <csignal>
#include <memory>
#include <ctime>

const int MAX_EVENTS = 1024;
const int PORT = 8080;
time_t last_check = 0;
const int CHECK_INTERVAL = 1;

volatile bool running = true;



int main() {
 

    int server_fd = -1;
    int epoll_fd = -1;

    try {
        setup_server_socket(server_fd, PORT);

        epoll_fd = epoll_create1(0);
        if (epoll_fd == -1) {
            throw std::runtime_error("epoll_create1 failed: " + std::string(strerror(errno)));
        }

        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = server_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
            throw std::runtime_error("epoll_ctl failed: " + std::string(strerror(errno)));
        }

        int notify_fd = reactor.get_notify_fd();
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = notify_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, notify_fd, &event) == -1) {
            throw std::runtime_error("epoll_ctl notify failed: " + std::string(strerror(errno)));
        }

        std::cout << "[INFO] Сервер готов на порту " << PORT << std::endl;
        std::cout << "[INFO] Рабочих потоков: " << std::thread::hardware_concurrency() << std::endl;

        struct epoll_event events[MAX_EVENTS];

        while (running) {

            time_t now = time(nullptr);
            if (now - last_check >= CHECK_INTERVAL) {
                check_connections(epoll_fd);
                last_check = now;
            }
            int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 10);

            if (n == -1) {
                
                std::cerr << "[ERROR] epoll_wait failed: " << strerror(errno) << std::endl;
                break;
            }

            for (int i = 0; i < n; i++) {
                // Обработка нового подключения
                if (events[i].data.fd == notify_fd) {
                    ReactorNotification notification;
                    while (reactor.read_notification(notification)) {
                        Connection* conn = get_connection(notification.fd);
                        if (conn) {
                            // epoll на запись
                            struct epoll_event ev;
                            ev.events = EPOLLOUT | EPOLLRDHUP ;
                            ev.data.ptr = conn;
                            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, notification.fd, &ev);
                        }
                    }
                }
                else if (events[i].data.fd == server_fd) {
                    handle_new_connection(server_fd, epoll_fd);
                }
                // Обработка клиентских событий
                else {
                    Connection* conn = static_cast<Connection*>(events[i].data.ptr);
                    if (!conn) {
                        continue;
                    }
                 
                    // Ошибка или разрыв соединения
                    if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                        handle_connection_error(conn, epoll_fd);
                        continue;
                    }

                    // Можно читать
                    if (events[i].events & EPOLLIN) {
                        handle_read(conn, epoll_fd);
                    }

                    // Можно писать
                    if (events[i].events & EPOLLOUT) {
                        handle_write(conn, epoll_fd);
                    }
                }
            }
        }

    }
    catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }

   
    std::cout << "[INFO] Очистка ресурсов..." << std::endl;

    if (epoll_fd != -1) {
        close(epoll_fd);
    }

    if (server_fd != -1) {
        close(server_fd);
    }

    active_connections.clear();
    worker_pool.stop();

    std::cout << "[INFO] Сервер остановлен." << std::endl;
    return 0;
}