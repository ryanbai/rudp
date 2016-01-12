// win_rudp_client.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "rudp.h"

int connected(int fd, int err);
void echo_recv(int fd, const void *p, size_t len, int err);

bool run_flag = true;
int main(int argc, const char* argv[])
{
	int ret = rudp_init();
	if (ret != 0)
		return 1;

	int fd = rudp_socket();
	if (fd == NULL)
	{
		printf("get fd failed\n");
		return 1;
	}

	ret = rudp_connect(fd, "10.12.21.11", 10001, connected, echo_recv);
	if (ret != 0)
	{
		printf("connect failed\n");
		return 1;
	}

	do {
		rudp_update();
	} while (run_flag);

	return 0;
}

const static char* ECHO_STR = "hello world!";
int connected(int fd, int err)
{
	printf("connected\n");

	int ret = rudp_send(fd, ECHO_STR, strlen(ECHO_STR));
	if (ret != 0)
	{
		printf("rudp_send failed, ret=%d", ret);
		rudp_close(fd);
		return ret;
	}

	return 0;
}

void echo_recv(int fd, const void *p, size_t len, int err)
{
	printf("echo_recv\n");

	if (p != NULL)
	{
		((char*)p)[len] = 0;
		printf("recv %zu\n", len);
		printf("content: %s\n", (char*)p);


		int ret = rudp_send(fd, ECHO_STR, strlen(ECHO_STR));
		if (ret != 0)
		{
			printf("rudp_send failed, ret=%d", ret);
			rudp_close(fd);
		}
	}
	else
	{
		printf("remote closed\n");
		// closed
		//      run_flag = false;
		rudp_close(fd);
	}
}

