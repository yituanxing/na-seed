#ifndef NASEED_FS_EPOLL_H
#define NASEED_FS_EPOLL_H
#include <stdint.h>
#include <stdbool.h>
struct nas_task;
uint64_t nas_epoll_create1(uint64_t flags);
uint64_t nas_epoll_ctl(uint64_t epfd,uint64_t op,uint64_t fd,uint64_t event);
uint64_t nas_epoll_wait(uint64_t epfd,uint64_t events,uint64_t maxevents,uint64_t timeout);
uint16_t nas_epoll_fd_poll(uint64_t fd,uint16_t interests);
uint16_t nas_epoll_description_poll(unsigned slot,uint16_t interests);
void nas_epoll_arm_current_task(uint64_t epfd);
bool nas_epoll_task_ready(const struct nas_task *task);
void nas_epoll_target_final_close(unsigned slot);
void nas_epoll_release(unsigned slot);
#endif
