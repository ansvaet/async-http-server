#include "connection.hpp"
#include <cstring>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <ctime>

Connection::Connection(int socket_fd) :
	fd(socket_fd),
	state(ConnectionState::READING_REQUEST),
	offset(0),
	method("GET"),
	keep_alive(true), // http 1.1
	keep_alive_timeout(-1),
	max_requests(10),
	handled_request(0)
{
	update_activity();
};

void Connection::add_to_read(const char* data, size_t length) {
	read_buffer.append(data, length);
	update_activity();
}

void Connection::set_response(const std::string& response) {
	write_buffer = response;
	state = ConnectionState::WRITING_RESPONSE;
	offset = 0;
	update_activity();
}

ssize_t Connection::send_data() {
	size_t remaining = write_buffer.size() - offset;;
	if (remaining == 0) {
		return 0;
	}
	ssize_t sent = send(fd, write_buffer.data() + offset, remaining, 0);
	if (sent > 0) {
		offset += sent;
		update_activity();
	}
	return sent;
}


void Connection::handle_keep_alive() {
	/*std::cout << "[DEBUG] handle_keep_alive: fd=" << fd
		<< ", handled=" << handled_request
		<< ", max=" << max_requests << std::endl;*/
	read_buffer.clear();
	write_buffer.clear();
	offset = 0;
	method.clear();
	path.clear();
	headers.clear();
	body.clear();

	handled_request++;
	if (handled_request >= max_requests) {
		/*std::cout << "[DEBUG] Достигнут лимит запросов для fd=" << fd
			<< " (" << handled_request << "/" << max_requests << ")" << std::endl;*/
		keep_alive = false;
		state = ConnectionState::WRITING_RESPONSE; 
		return;
	}
	state = ConnectionState::READING_REQUEST;
	update_activity();
}
bool Connection::headers_receive() const {
	return read_buffer.find("\r\n\r\n") != std::string::npos;
}
bool Connection::response_complete() const {
	return offset >= write_buffer.size();
}

bool Connection::is_timed_out() const {
	if (keep_alive_timeout <= 0) return false;
	bool is_timed_out = (time(nullptr) - last_activity) > keep_alive_timeout;
	return !keep_alive ||
		handled_request >= max_requests ||is_timed_out;
}
bool Connection::is_max_requests() const {
	return handled_request >= max_requests;
}

bool Connection::is_valid_state() const {
	
	if (fd <= 0) return false;
	if (state == ConnectionState::CLOSING) return false;
	return true;
}

bool Connection::should_close() const {

	if (!keep_alive) {
		/*std::cout << "[DEBUG] should_close: keep_alive=false" << std::endl;*/
		return true;
	}

	if (handled_request >= max_requests) {
		/*std::cout << "[DEBUG] should_close: max_requests reached" << std::endl*/;
		return true;
	}

	if (is_timed_out()) {
		/*std::cout << "[DEBUG] should_close: timeout" << std::endl*/;
		return true;
	}

	// Проверяем валидность состояния
	if (!is_valid_state()) {
		//std::cout << "[DEBUG] should_close: invalid state" << std::endl;
		return true;
	}

	return false;
}

void Connection::update_activity() {
	last_activity = time(nullptr);
}

void Connection::parse_connection_params() {

	auto conn_it = headers.find("connection");
	if (conn_it != headers.end()) {
		std::string conn_val = conn_it->second;
		std::transform(conn_val.begin(), conn_val.end(), conn_val.begin(), ::tolower);

		if (http_version == "HTTP/1.1") {
			// HTTP/1.1: keep-alive по умолчанию
			keep_alive = (conn_val.find("close") == std::string::npos);
		}
		else if (http_version == "HTTP/1.0") {
			// HTTP/1.0: close по умолчанию
			keep_alive = (conn_val.find("keep-alive") != std::string::npos);
		}
	}

	auto ka_it = headers.find("keep-alive");
	if (ka_it != headers.end()) {
		std::string ka_val = ka_it->second;

		size_t timeout_pos = ka_val.find("timeout=");
		if (timeout_pos != std::string::npos) {
			std::string timeout_str = ka_val.substr(timeout_pos + 8);
			size_t end_pos = timeout_str.find_first_of(", ");
			if (end_pos != std::string::npos) {
				timeout_str = timeout_str.substr(0, end_pos);
			}
			try {
				keep_alive_timeout = std::max(1, std::stoi(timeout_str));
			}
			catch (...) {
				// Оставляем значение по умолчанию
			}
		}

		size_t max_pos = ka_val.find("max=");
		if (max_pos != std::string::npos) {
			std::string max_str = ka_val.substr(max_pos + 4);
			size_t end_pos = max_str.find_first_of(", ");
			if (end_pos != std::string::npos) {
				max_str = max_str.substr(0, end_pos);
			}
			try {
				max_requests = std::max(1, std::stoi(max_str));
			}
			catch (...) {
				// Оставляем значение по умолчанию
			}
		}
	}
	/*std::cout << "[DEBUG] Final keep-alive: " << keep_alive
		<< ", timeout: " << keep_alive_timeout
		<< ", max: " << max_requests << std::endl;*/
}

bool Connection::parse_headers() {
	if (!headers_receive()) {
		return false; 
	}
	
	size_t first_line_end = read_buffer.find("\r\n");
	if (first_line_end == std::string::npos) return false;

	std::string first_line = read_buffer.substr(0, first_line_end);
	std::istringstream iss(first_line);
	if (!(iss >> method >> path >> http_version)) {
		return false;
	}

	size_t headers_start = first_line_end + 2;
	size_t headers_end = read_buffer.find("\r\n\r\n");
	if (headers_end == std::string::npos) return false;

	std::string headers_text = read_buffer.substr(headers_start, headers_end - headers_start);
	std::istringstream hss(headers_text);
	std::string line;

	while (std::getline(hss, line)) {
		if (line.empty() || line == "\r") continue;
		if (line.back() == '\r') line.pop_back();

		size_t colon = line.find(':');
		if (colon != std::string::npos) {
			std::string key = line.substr(0, colon);
			std::string value = line.substr(colon + 1);

			value.erase(0, value.find_first_not_of(" \t"));
			std::transform(key.begin(), key.end(), key.begin(), ::tolower);
			headers[key] = value;
		}
	}

	parse_connection_params();

	if (method != "GET" && method != "POST") {
		return false;
		std::cout << "Not supported method"<< std::endl;
	}

	if (path.empty()) path = "/";



	return true;
}
