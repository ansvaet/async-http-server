#pragma once

#include <string>
#include <map>
#include <sys/socket.h>
#include <ctime>

enum class ConnectionState {
    READING_REQUEST,   
    PROCESSING,        
    WRITING_RESPONSE,  
    CLOSING,
	KEEP_ALIVE_WAITING
};


struct Connection {
	int fd;
	ConnectionState state;
	std::string read_buffer;
	std::string write_buffer;
	size_t offset;

	std::string method;
	std::string http_version;
	std::string path;
	std::map<std::string, std::string> headers;
	std::string body;

	time_t last_activity;
	bool keep_alive;
	int keep_alive_timeout;
	int max_requests;
	int handled_request;

	Connection(int socket_fd);

	void add_to_read(const char* data, size_t length);
	void set_response(const std::string& response);
	ssize_t send_data();
	bool parse_headers();
	bool response_complete() const;
	void parse_connection_params();
	void handle_keep_alive();
	bool should_close() const;
	void update_activity();
	bool is_timed_out() const;
	bool is_max_requests() const;
	bool is_valid_state() const;
	bool headers_receive() const;
	

};


