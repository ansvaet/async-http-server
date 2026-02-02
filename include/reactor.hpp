#pragma once

#include <cstdint>

struct ReactorNotification {
	int fd;
	uint32_t events;
};

class Reactor {
public:
	Reactor();
	~Reactor();

	void notify(int fd, uint32_t events);
	int get_notify_fd() const { return pipe_[0]; }
	bool read_notification(ReactorNotification& notification);
private:
	int pipe_[2];
	//int pipe_read_;   // pipe[0] - чтение
	//int pipe_write_;  // pipe[1] - запись
};