#if 0
gcc -std=c89 -O3 shitty-server.c -o /usr/local/sbin/shitty-server
exit
#endif
/*
 * Shitty Server: an unusually agressive Discard Protocol server <https://en.wikipedia.org/wiki/Discard_Protocol>
 * it reads and discards all your data as long as you feed it regularly
 * after a few seconds of idling (at least 5), it drops your connection on the floor, without a FIN or anything
 * probably somewhat useful to test your program's resilience against network failures
 *
 * system requirements: requires linux and root because TCP_REPAIR demands that, and because port 9 is privileged
 * if you need to test a windows program against dropped sockets, run this on another machine, possibly a virtual machine
 * or if you just want a quick test, you may use my instance: floating.muncher.se:9 or :99
 *
 * license: WTFPL, any version (for this file only, the rest of this directory is differently licensed)
 *
 * further reading: http://oroboro.com/dealing-with-network-port-abuse-in-sockets-in-c
 */

/* feel free to replace with port 99 if you want to test in firefox or something */
/* (no idea why they'd block the port specifically defined to not parse your data) */
#define PORTNR 9

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#ifndef DEBUG
#define DEBUG 0
#endif

#ifndef bool
#define bool int
#endif
#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif

static int listen_create(int port)
{
	struct sockaddr_in sa;
	int fd;
	int status;
	
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
	{
		perror("socket");
		return -1;
	}
	status = bind(fd, (struct sockaddr*)&sa, sizeof(sa));
	if (status < 0)
	{
		perror("bind");
		return -1;
	}
	status = listen(fd, 10);
	if (status < 0)
	{
		perror("listen");
		return -1;
	}
	
	return fd;
}

static void print_fd_list(int* fds, int nfds, int highlight)
{
	int i;
	for (i=0;i<nfds;i++)
	{
		if (i == highlight) printf("[%i] ", fds[i]);
		else printf("%i ", fds[i]);
	}
}

static bool err_is_permanent(int ret, int err)
{
	if (ret == 0) return true;
	if (ret > 0) return false;
	if (err == EAGAIN || errno == EWOULDBLOCK) return false;
	if (err == EINTR) return false;
	/* all other errors I can see mean either broken connection or bad arguments */
	return true;
}

static void close_noFIN(int fd)
{
	static const int yes = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_REPAIR, &yes, sizeof(yes));
	close(fd);
}

static int* fds = NULL;
static int nfds_prev = 0; /* whenever nextcycle arrives, discard this many sockets from the array */
static int nfds = 0;

static void close_noFIN_range(int start, int num)
{
	int i;
	for (i=0;i<num;i++)
	{
		close_noFIN(fds[start + i]);
	}
	memmove(fds+start, fds+start+num, sizeof(int)*(nfds-start-num));
	nfds -= num;
	
	if (start + num < nfds_prev) nfds_prev -= num;
	else if (start < nfds_prev) nfds_prev = start;
	/* else nothing */
}

static void fd_swap(int a, int b)
{
	int swap = fds[a];
	fds[a] = fds[b];
	fds[b] = swap;
}

int main()
{
	time_t nextcycle = time(NULL);
	int listen;
	
	signal(SIGPIPE, SIG_IGN);
	
	listen = listen_create(PORTNR);
	if (listen < 0) return 1;
	
	while (true)
	{
		int i;
		
		fd_set fdset;
		FD_ZERO(&fdset);
		FD_SET(listen, &fdset);
		for (i=0;i<nfds;i++) FD_SET(fds[i], &fdset);
		if (DEBUG)
		{
			print_fd_list(fds, nfds, -1);
			printf("select\n");
		}
		select(FD_SETSIZE, &fdset, NULL, NULL, NULL); /* don't bother with timeouts */
		
		if (FD_ISSET(listen, &fdset))
		{
			int newfd = accept4(listen, NULL, NULL, SOCK_NONBLOCK);
			if (newfd < 0) continue;
			
			if (nfds > 65535) close_noFIN_range(0, 1);
			
			int* newfds = realloc(fds, sizeof(int)*(nfds+1));
			if (!newfds)
			{
				close_noFIN(newfd);
				continue;
			}
			
			fds = newfds;
			fds[nfds] = newfd;
			nfds++;
			
			if (DEBUG)
			{
				/* this is wrong if nfds > 65535, but debugging such a big device is silly already */
				print_fd_list(fds, nfds, nfds-1);
				printf("accept\n");
			}
		}
		
		for (i=0;i<nfds;i++)
		{
			if (!FD_ISSET(fds[i], &fdset)) continue;
			
			static char dump[1024];
			int amount = recv(fds[i], dump, sizeof(dump), MSG_DONTWAIT);
			bool should_close = err_is_permanent(amount, errno);
			if (DEBUG)
			{
				print_fd_list(fds, nfds, i);
				printf("recv %i\n", amount);
			}
			/* as long as we're getting data, keep the socket alive */
			if (amount > 0 && i < nfds_prev)
			{
				if (nfds_prev < nfds) fd_swap(i, nfds_prev);
				nfds_prev--;
				
				/* to ensure the one we swapped with is processed as well */
				/* this will cause the old fds[i] to be processed twice, but that's fine */
				i--;
			}
			else if (should_close)
			{
				if (DEBUG)
				{
					print_fd_list(fds, nfds, nfds-1);
					printf("close EOF/error\n");
				}
				close_noFIN_range(i, 1);
				i--;
			}
		}
		
		if (time(NULL) > nextcycle)
		{
			nextcycle = time(NULL)+5;
			close_noFIN_range(0, nfds_prev);
			nfds_prev = nfds;
		}
	}
}
