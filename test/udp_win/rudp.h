#ifndef RUDP_H_
#define RUDP_H_

extern "C"
{
typedef int (*rudp_accept_fn)(int fd, int err);
typedef void (*rudp_recv_fn)(int fd, const void* buf, size_t len, int err);
typedef int (*rudp_connected_fn)(int fd, int err);

// 初始化
int rudp_init();

// 定时调用接口
int rudp_update();

// socket
int rudp_socket();

// 关闭socket
void rudp_close(int fd);

// 释放内存 
void rudp_free(int fd);

// 绑定
int rudp_bind(int fd, const char *ip_addr, unsigned short port);

// 监听
int rudp_listen(int fd, rudp_accept_fn accept, rudp_recv_fn recv);

// 连接
int rudp_connect(int fd, const char *ip_addr, unsigned short port, rudp_connected_fn connected, rudp_recv_fn recv);

// 发送
int rudp_send(int fd, const void *buf, size_t len);

}

#endif