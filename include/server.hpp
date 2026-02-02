#pragma once

#include "connection.hpp"
#include "connection_map.hpp"
#include "reactor.hpp"
#include "thread_pool.hpp"
#include <sys/epoll.h>

extern ConnectionMap active_connections;
extern ThreadPool worker_pool;
extern Reactor reactor;


Connection* create_connection(int fd, int epoll_fd);
Connection* get_connection(int fd);
void delete_connection(int fd, int epoll_fd);
void check_connections(int epoll_fd);
void process_request(Connection* conn, int epoll_fd);

void handle_new_connection(int server_fd, int epoll_fd);
void handle_read(Connection* conn, int epoll_fd);
void handle_write(Connection* conn, int epoll_fd);
void handle_connection_error(Connection* conn, int epoll_fd);


void set_nonblocking(int fd);
void setup_server_socket(int& server_fd, int port);
