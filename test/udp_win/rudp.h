#ifndef RUDP_H_
#define RUDP_H_

extern "C"
{
typedef int (*rudp_accept_fn)(int fd, int err);
typedef void (*rudp_recv_fn)(int fd, const void* buf, size_t len, int err);
typedef int (*rudp_connected_fn)(int fd, int err);

// ��ʼ��
int rudp_init();

// ��ʱ���ýӿ�
int rudp_update();

// socket
int rudp_socket();

// �ر�socket
void rudp_close(int fd);

// �ͷ��ڴ� 
void rudp_free(int fd);

// ��
int rudp_bind(int fd, const char *ip_addr, unsigned short port);

// ����
int rudp_listen(int fd, rudp_accept_fn accept, rudp_recv_fn recv);

// ����
int rudp_connect(int fd, const char *ip_addr, unsigned short port, rudp_connected_fn connected, rudp_recv_fn recv);

// ����
int rudp_send(int fd, const void *buf, size_t len);

}

#endif