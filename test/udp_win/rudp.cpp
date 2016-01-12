#include "rudp.h"

#include "lwip/tcp_impl.h"
#include "lwip/pbuf.h"
#include "lwip/memp.h"

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <unordered_map>

#pragma comment(lib,"ws2_32.lib")

struct rudp_state
{
	// for tcp_close may fail, rudp must retry, not app
	int fd;
	//  u8_t retries;
	tcp_pcb* pcb;

	rudp_recv_fn recv_cb;
	rudp_accept_fn accept_cb;
	rudp_connected_fn connected_cb;
};

SOCKET udp_fd = INVALID_SOCKET;

int global_fd = 0;
typedef std::tr1::unordered_map<int, struct rudp_state*> MAP_OF_TCP_PCB;
MAP_OF_TCP_PCB map_of_tcp_pcb;

//前导声明
void setup_pcb(rudp_state* rudp, tcp_pcb* pcb);

int ip_output_udp(char *data, int len, u32_t addr, u16_t remote_port)
{
	struct sockaddr_in remaddr;
	static socklen_t addrlen = sizeof(remaddr);
	remaddr.sin_port = htons(remote_port);
	remaddr.sin_family = AF_INET;
	remaddr.sin_addr.S_un.S_addr = addr;

	int ret = sendto(udp_fd, data, len, 0, (SOCKADDR*)&remaddr, addrlen);
	if (ret <= 0)
	{
		return -3;
	}

	return 0;
}

int rudp_init()
{
	tcp_init(ip_output_udp);
	pbuf_init();
	memp_init();

	WSADATA wsaData;
	int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (ret != NO_ERROR) {
		printf("WSAStartup failed: %d\n", ret);
		return -1;
	}

	udp_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udp_fd == INVALID_SOCKET)
	{
		perror("cannot create socket\n");
		return -2;
	}

	return 0;
}

void tcp_timer()
{
	printf("recvfrom timeout\n");

	if (tcp_active_pcbs || tcp_tw_pcbs)
		tcp_tmr();
}

int rudp_update()
{
	int udp_process_count = 0;
	const int BUFSIZE = 64 * 1024;
	char buf[BUFSIZE];

	DWORD last_tick_count = 0;
	static const DWORD TIME_INTERVAL = 100;
	static const int max_loop = 1000;

	struct sockaddr_in remaddr;     /* remote address */
	static socklen_t addrlen = sizeof(remaddr);

	do
	{
		int recvlen = recvfrom(udp_fd, buf, BUFSIZE, 0, (struct sockaddr *)&remaddr, &addrlen);
		if (recvlen < 0)
		{
			// timeout
			if (WSAGetLastError() == WSAETIMEDOUT)
				tcp_timer();
			else
				perror("recvfrom failed\n");
			return -1;
		}

		struct pbuf *mybuf = pbuf_alloc(PBUF_TRANSPORT, recvlen, PBUF_POOL);
		if (mybuf == NULL)
		{
			perror("pbuff alloc failed\n");
			return -2;
		}

		int copy_len = 0;
		struct pbuf *tmp_buf = mybuf;
		while (tmp_buf != NULL)
		{
			memcpy(tmp_buf->payload, buf + copy_len, mybuf->len);
			copy_len += tmp_buf->len;
			tmp_buf = tmp_buf->next;
		}

		struct ip_addr_t ipaddr;
		ipaddr.addr = remaddr.sin_addr.s_addr;

		tcp_input(ipaddr, ntohs(remaddr.sin_port), mybuf);

		udp_process_count++;
	} while (udp_process_count < max_loop);

	DWORD tick_count = GetTickCount();
	DWORD pass_usec = tick_count - last_tick_count;
	if (pass_usec > TIME_INTERVAL)
	{
		last_tick_count = tick_count;
		tcp_timer();
	}
	else
	{
		// modify block timeout
		DWORD timeout = TIME_INTERVAL - pass_usec;
		setsockopt(udp_fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
	}

	return 0;
}

int rudp_socket()
{
	rudp_state *rudp = (rudp_state *)mem_malloc(sizeof(rudp_state));
	rudp->pcb = tcp_new();
	if (rudp->pcb == NULL)
	{
		mem_free(rudp);
		return -1;
	}
	setup_pcb(rudp, rudp->pcb);

	int try_times = 0;
	do {
		rudp->fd = ++global_fd;
		std::pair<MAP_OF_TCP_PCB::iterator, bool> result = map_of_tcp_pcb.insert(MAP_OF_TCP_PCB::value_type(rudp->fd, rudp));
		if (result.second)
		{
			return result.first->first;
		}
	} while (++try_times <= 3);


	mem_free(rudp);
	return -2;
}

struct rudp_state * rudp_get(int fd)
{
	MAP_OF_TCP_PCB::iterator iter = map_of_tcp_pcb.find(fd);
	if (iter == map_of_tcp_pcb.end())
	{
		return NULL;
	}

	return iter->second;
}

void rudp_close(int fd)
{
	struct rudp_state *rudp = rudp_get(fd);
	if (rudp == NULL)
	{
		return;
	}

	err_t err = tcp_close(rudp->pcb);
	if (err == ERR_OK)
	{
		rudp_free(fd);
		return;
	}

	// possible fail
	// try again by using poll or sent cb
	printf("tcp_close failed, err=%d\n", err);
}

void rudp_free(int fd)
{
	struct rudp_state *rudp = rudp_get(fd);
	if (rudp == NULL)
	{
		return;
	}

	tcp_arg(rudp->pcb, NULL);
	tcp_sent(rudp->pcb, NULL);
	tcp_recv(rudp->pcb, NULL);
	tcp_err(rudp->pcb, NULL);
	tcp_poll(rudp->pcb, NULL, 0);

	free(rudp);
	map_of_tcp_pcb.erase(fd);
}


int rudp_bind(int fd, const char *ipaddr, u16_t port)
{
	struct rudp_state *rudp = rudp_get(fd);
	if (rudp == NULL)
	{
		return -1;
	}

	struct sockaddr_in myaddr;      /* our address */
	/* bind the socket to any valid IP address and a specific port */
	memset((char *)&myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	inet_pton(AF_INET, ipaddr, &myaddr.sin_addr);
	myaddr.sin_port = htons(port);
	if (bind(udp_fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
		perror("bind failed\n");
		return -2;
	}

	err_t err = tcp_bind(rudp->pcb, port);
	if (err != ERR_OK)
	{
		/* abort? output diagnostic? */
		return -3;
	}

	return 0;
}

err_t on_accept(void *arg, tcp_pcb* newpcb, err_t err)
{
	rudp_state* listen_rudp = (rudp_state*)arg;
	if (listen_rudp == NULL)
	{
		return -1;
	}

	if (err != 0)
	{
		return listen_rudp->accept_cb(0, err);
	}

	/* Unless this pcb should have NORMAL priority, set its priority now.
	When running out of pcbs, low priority pcbs can be aborted to create
	new pcbs of higher priority. */
	tcp_setprio(newpcb, TCP_PRIO_MIN);

	rudp_state *new_rudp = (rudp_state *)mem_malloc(sizeof(rudp_state));
	if (new_rudp == NULL)
	{
		// release newpcb?
		tcp_close(newpcb);
		return ERR_MEM;
	}
	setup_pcb(new_rudp, newpcb);
	new_rudp->recv_cb = listen_rudp->recv_cb;
	int try_times = 0;
	do {
		new_rudp->fd = ++global_fd;
		std::pair<MAP_OF_TCP_PCB::iterator, bool> result = map_of_tcp_pcb.insert(MAP_OF_TCP_PCB::value_type(new_rudp->fd, new_rudp));
		if (result.second)
		{
			return listen_rudp->accept_cb(new_rudp->fd, err);
		}
	} while (++try_times <= 3);

	mem_free(new_rudp);
	return -2;
}

/**
The callback function will be passed a NULL pbuf to
indicate that the remote host has closed the connection. If
there are no errors and the callback function is to return
ERR_OK, then it must free the pbuf. Otherwise, it must not
free the pbuf so that lwIP core code can store it.
*/
err_t on_recv(void *arg, tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
	printf("on_recv\n");

	rudp_state* rudp = (rudp_state*)arg;
	if (rudp == NULL)
	{
		if (p == NULL)
		{
			printf("FIN_WAIT_2 fin, ignore\n");
			return ERR_OK;
		}
		else
		{
			return -1;
		}
	}

	// same as read() ret 0
	/* remote host closed connection */
	if (p == NULL)
	{
		printf("remote close\n");

		rudp->recv_cb(rudp->fd, NULL, 0, err);

		return ERR_OK;
	}
	// data recved

	// for now, err passed in can only be ERR_OK
	if (err != ERR_OK)
	{
		printf("on_recv err=%d\n", err);
		rudp->recv_cb(rudp->fd, NULL, 0, err);

		// return ERR_OK means cb execute ok
		// err return is not needed, for up-layer already know
		return ERR_OK;
	}

	const int BUFSIZE = 64 * 1024;
	char buf[BUFSIZE];
	int copy_len = 0;
	while (p != NULL)
	{
		memcpy(buf + copy_len, p->payload, p->len);
		copy_len += p->len;
		p = p->next;
	}
	printf("recv: %s\n", buf);

	tcp_recved(tpcb, copy_len);

	rudp->recv_cb(rudp->fd, buf, copy_len, err);

	return ERR_OK;
}

err_t on_connect(void *arg, tcp_pcb* tpcb, err_t err)
{
	rudp_state *rudp = (rudp_state*)arg;
	if (rudp == NULL)
	{
		return -1;
	}

	int ret = rudp->connected_cb(rudp->fd, err);
	if (ret != 0)
	{
		return ret;
	}

	return err;
}

int rudp_connect(int fd, const char* ipaddr, u16_t port, rudp_connected_fn connected_cb, rudp_recv_fn recv_cb)
{
	struct rudp_state *rudp = rudp_get(fd);
	if (rudp == NULL)
	{
		return -1;
	}

	rudp->connected_cb = connected_cb;
	rudp->recv_cb = recv_cb;

	struct ip_addr_t ip;
	inet_pton(AF_INET, ipaddr, &ip);

	return tcp_connect(rudp->pcb, &ip, port, on_connect);
}

void rudp_error(void *arg, err_t err)
{
	rudp_state *rudp = (rudp_state*)arg;
	if (rudp == NULL)
	{
		return;
	}

	printf("%p err=%d\n", arg, err);

	rudp_free(rudp->fd);
}

err_t rudp_poll(void *arg, struct tcp_pcb *tpcb)
{
	rudp_state *rudp = (rudp_state*)arg;
	if (rudp == NULL)
	{
		return ERR_OK;
	}

	return ERR_OK;
}


err_t rudp_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
	rudp_state *rudp = (rudp_state*)arg;
	if (rudp == NULL)
	{
		return ERR_OK;
	}

	return ERR_OK;
}


int rudp_listen(int fd, rudp_accept_fn accept_cb, rudp_recv_fn recv_cb)
{
	struct rudp_state *rudp = rudp_get(fd);
	if (rudp == NULL)
	{
		return -1;
	}

	struct tcp_pcb *listen = tcp_listen(rudp->pcb);
	if (listen == NULL)
	{
		return -2;
	}

	if (rudp->pcb != listen)
	{
		rudp->pcb = listen;
	}
	rudp->accept_cb = accept_cb;
	rudp->recv_cb = recv_cb;

	tcp_arg(listen, &rudp);
	tcp_accept(listen, on_accept);
	tcp_recv(listen, on_recv);

	return 0;
}

void setup_pcb(rudp_state* rudp, tcp_pcb* pcb)
{
	tcp_arg(pcb, rudp);
	rudp->pcb = pcb;

	tcp_recv(rudp->pcb, on_recv);
	tcp_err(rudp->pcb, rudp_error);
	tcp_poll(rudp->pcb, rudp_poll, 0);
	tcp_sent(rudp->pcb, rudp_sent);
}

int rudp_send(int fd, const void* buf, size_t len)
{
	struct rudp_state *rudp = rudp_get(fd);
	if (rudp == NULL)
	{
		return -1;
	}

	// 不处理TCP_WRITE_FLAG_MORE的情况，意味着不能一次性发送大于最大缓冲区的包
	return tcp_write(rudp->pcb, buf, len, 1);
}
