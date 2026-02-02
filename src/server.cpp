#include "server.hpp"
#include "connection.hpp"
#include "connection_map.hpp"
#include "reactor.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <cerrno>
#include <chrono>
#include <netinet/in.h>

const int MAX_CONNECTIONS = 100;

ConnectionMap active_connections;
ThreadPool worker_pool(std::thread::hardware_concurrency());
Reactor reactor;

Connection* create_connection(int fd, int epoll_fd) {
    if (active_connections.size() >= MAX_CONNECTIONS) {
        std::cerr << "[WARN] Достигнут лимит соединений fd=" << fd << std::endl;
        close(fd);
        return nullptr;
    }

    auto conn = std::make_unique<Connection>(fd);
    Connection* raw_ptr = conn.get();
    if (!conn->is_valid_state()) {
        std::cerr << "[ERROR] Создано невалидное соединение" << std::endl;
        close(fd);
        return nullptr;
    }
    active_connections.insert(fd, std::move(conn));

    return raw_ptr;
}

Connection* get_connection(int fd) {
    return active_connections.get(fd);
}

void delete_connection(int fd, int epoll_fd) {
    Connection* conn = get_connection(fd);
    if (!conn) {
        close(fd);
        return;
    }
    
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);

    close(fd);

    active_connections.erase(fd);

    //std::cout << "[INFO] Закрыто соединение fd=" << fd << std::endl;
}

void check_connections(int epoll_fd) {
    active_connections.for_each([epoll_fd](int fd, Connection* conn) {
        if (conn && conn->should_close()) {
           /* std::cout << "[INFO] Закрытие по таймауту/лимиту fd=" << conn->fd
                << " запросов=" << conn->handled_request
                << " таймаут=" << conn->is_timed_out() << std::endl;*/
            delete_connection(fd, epoll_fd);
        }
        });
}
void handle_new_connection(int server_fd, int epoll_fd) {
    
    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Нет больше ожидающих подключений
                break;
            }
            std::cerr << "[ERROR] accept failed: " << strerror(errno) << std::endl;
            break;
        }

        //std::cout << "[INFO] Новое подключение fd=" << client_fd << std::endl;

        set_nonblocking(client_fd);

        Connection* conn = create_connection(client_fd, epoll_fd);

        struct epoll_event event {};
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
        event.data.ptr = conn;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
    }
}

void handle_read(Connection* conn, int epoll_fd) {
    if (!conn || conn->state != ConnectionState::READING_REQUEST) {
        std::cerr << "[ERROR] Неожиданное состояние в handle_readable" << std::endl;
        return;
    }

    char buffer[4096];
    while (true) {
        ssize_t bytes_read = recv(conn->fd, buffer, sizeof(buffer), 0);

        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            std::cerr << "[ERROR] recv failed: " << strerror(errno) << std::endl;
            delete_connection(conn->fd, epoll_fd);
            return;
        }
        else if (bytes_read == 0) {
            delete_connection(conn->fd, epoll_fd);
            return;
        }

        conn->add_to_read(buffer, bytes_read);

        if (conn->headers_receive()) {

            struct epoll_event event {};
            event.events = EPOLLRDHUP | EPOLLET; 
            event.data.ptr = conn;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &event);

 
            worker_pool.enqueue([conn, epoll_fd]() {
                process_request(conn, epoll_fd);
                });
            break; 
        }
        }
    //std::cout << "Данные прочитаны для fd=" << conn->fd << std::endl;

    }

void process_request(Connection* conn, int epoll_fd) {
    auto start = std::chrono::steady_clock::now();
    bool parse_success = conn->parse_headers();

    std::string response;
    if (!parse_success) {
        response = "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "Bad Request";
    }
    else {
      
        std::string body = "Processed in thread pool. Path: " + conn->path;
        response = "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: " + std::to_string(body.length()) + "\r\n"
            "\r\n" + body;
    }
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "[PERF] process_request fd=" << conn->fd
        << " took " << duration.count() << " μs" << std::endl;

    conn->set_response(response);

    reactor.notify(conn->fd, EPOLLOUT);
}



void handle_write(Connection* conn, int epoll_fd) {
    if (!conn || conn->state != ConnectionState::WRITING_RESPONSE) {
        return;
    }
    while (true) {
        ssize_t sent = conn->send_data();

        if (sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {  
                return;
            }
            std::cerr << "[ERROR] send failed: " << strerror(errno) << std::endl;
            delete_connection(conn->fd, epoll_fd);
            return;
        }
        if (sent == 0) { break; }
        if (conn->response_complete()) {
            /*std::cout << "[DEBUG] Ответ отправлен полностью для fd=" << conn->fd
                << ", keep-alive=" << conn->keep_alive
                << ", requests=" << conn->handled_request
                << "/" << conn->max_requests << std::endl;*/

            if (conn->keep_alive && !conn->should_close()) {
                conn->handle_keep_alive();

                struct epoll_event event {};
                event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
                event.data.ptr = conn;

                if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &event) == -1) {
                    delete_connection(conn->fd, epoll_fd);
                }
                /*else {
                    std::cout << "[DEBUG] Keep-alive: fd=" << conn->fd
                        << " переключен на чтение" << std::endl;
                }*/
            }
            else {
               /* std::cout << "[DEBUG] Закрываем соединение fd=" << conn->fd
                    << ", keep-alive=" << conn->keep_alive
                    << ", should_close=" << conn->should_close() << std::endl;*/
                delete_connection(conn->fd, epoll_fd);
            }
                break;
            }
        }
    //std::cout << "Данные отправлены для fd=" << conn->fd << std::endl;    
    }
    
    


void handle_connection_error(Connection* conn, int epoll_fd) {
    delete_connection(conn->fd, epoll_fd);
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void setup_server_socket(int& server_fd, int port) {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        throw std::runtime_error("socket failed: " + std::string(strerror(errno)));
    }

    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    set_nonblocking(server_fd);

    struct sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
        throw std::runtime_error("bind failed: " + std::string(strerror(errno)));
    }

    if (listen(server_fd, 128) == -1) {
        throw std::runtime_error("listen failed: " + std::string(strerror(errno)));
    }

    std::cout << "[INFO] Сервер запущен на порту " << port << std::endl;
}