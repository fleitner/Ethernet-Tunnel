
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_tun.h>
#include <errno.h>
#include "forward.h"
#include "sysrw.h"

#define ETHERTUN_VERSION "0.7"

//#define DEBUG

#define	ETHERTUN_MAX_EVENTS	10
#define	ETHERTUN_TIMEOUT	-1

struct ether_tunnel {
	pthread_t tx_thread;
	pthread_t rx_thread;
	char host_devname[IFNAMSIZ];
	char bridge_devname[IFNAMSIZ];
	unsigned char host_devmac[ETH_ALEN];
	unsigned char bridge_devmac[ETH_ALEN];
	int host_fd;
	int bridge_fd;
	struct forward_operations op;
};

void sigterm_handler(int signum)
{
}

void set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}


/* create a tap device named as dev_name */
int tap_create(char *dev_name)
{
	int fd;
	int rc;
	struct ifreq ifr;

	fd = open("/dev/net/tun" , O_RDWR);
	if (fd < 0 ) {
		perror("Opening /dev/net/tun");
		return fd;
	}

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = (IFF_TAP | IFF_NO_PI);
	strncpy(ifr.ifr_name, dev_name, IFNAMSIZ);
	rc = ioctl(fd, TUNSETIFF, (void *)&ifr);
	if (rc < 0 ) {
		perror("ioctl(TUNSETIFF)");
		close(fd);
		return rc;
	}


	return fd;
}

int tap_fix_macaddr(char *dev_name, unsigned char *dev_mac)
{
	int rc;
	int s;
	struct ifreq ifr;

	s = socket(AF_INET, SOCK_DGRAM, 0);

	strncpy(ifr.ifr_name, dev_name, IFNAMSIZ);
	rc = ioctl(s, SIOCGIFHWADDR, &ifr);
	if (rc != 0) {
		perror("ioctl(SIOCGIFHWADDR)");
		goto out;
	}

	/* fix MACADDR to start at 0xFE to prevent bridge
	 * to change its macaddr */
	ifr.ifr_hwaddr.sa_data[0] = 0xFE;
	ifr.ifr_hwaddr.sa_data[1] = dev_mac[1];
	ifr.ifr_hwaddr.sa_data[2] = dev_mac[2];
	ifr.ifr_hwaddr.sa_data[3] = dev_mac[3];
	ifr.ifr_hwaddr.sa_data[4] = dev_mac[4];
	ifr.ifr_hwaddr.sa_data[5] = dev_mac[5];

	rc = ioctl(s, SIOCSIFHWADDR, &ifr);
	if (rc != 0)
		perror("ioctl(SIOCSIFHWADDR)");

out:
	close(s);
	return 0;
}


int transmit_loop(struct ether_tunnel *ethert, enum direction dir)
{
	ssize_t (*forward) (void *, struct forward_cmd *);
	void *data;
	struct forward_cmd cmd;
	struct epoll_event events;
	int epoll_fd;
	int rc;

	cmd.fd1 = ethert->host_fd;
	cmd.fd2 = ethert->bridge_fd;
	cmd.dir = dir;
	cmd.length = PACKET_LENGTH;

	epoll_fd = epoll_create(1);
	events.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
	if (dir == fd1TOfd2) {
		events.data.fd = cmd.fd1;
		rc = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cmd.fd1, &events);
	}
	else {
		events.data.fd = cmd.fd2;
		rc = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cmd.fd2, &events);
	}

	if (rc < 0) {
		perror("epoll_ctl add failed");
		goto out;
	}

	forward = ethert->op.forward;
	data = ethert->op.data;

	/* connect each other */
	while (1) {
		rc = epoll_wait(epoll_fd, &events, 1, ETHERTUN_TIMEOUT);
		if (rc < 0) {
			/* terminating */
			if (errno == EINTR) {
				rc = 0;
				goto out;
			}

			perror("Error in epoll_wait");
			goto out;
		}

		forward(data, &cmd);
	}

	rc = 0;
out:
	return rc;

}

void *tx_thread_func(void *ptr)
{
	struct ether_tunnel *ethert = (struct ether_tunnel *)ptr;

	transmit_loop(ethert, fd1TOfd2);
	pthread_exit(NULL);
}

void *rx_thread_func(void *ptr)
{
	struct ether_tunnel *ethert = (struct ether_tunnel *)ptr;

	transmit_loop(ethert, fd2TOfd1);
	pthread_exit(NULL);
}

int start_tunnel(struct ether_tunnel *ethert)
{
	pthread_create(&ethert->tx_thread, NULL,
		       tx_thread_func, (void *)ethert);

	pthread_create(&ethert->rx_thread, NULL,
		       rx_thread_func, (void *)ethert);

	return 0;
}

int stop_tunnel(struct ether_tunnel *ethert)
{
	/* send the signal */
	pthread_kill(ethert->tx_thread, SIGTERM);
	pthread_kill(ethert->rx_thread, SIGTERM);

	/* wait them die */
	pthread_join(ethert->tx_thread, NULL);
	pthread_join(ethert->rx_thread, NULL);
}

int create_tunnel(struct ether_tunnel *ethert, int method)
{
	int fd;

	/* create host's side interface */
	fd = tap_create(ethert->host_devname);
	if (fd < 0)
		return fd;

	tap_fix_macaddr(ethert->host_devname, ethert->host_devmac);
	set_nonblock(fd);
	ethert->host_fd = fd;

	/* create bridge's side interface */
	fd = tap_create(ethert->bridge_devname);
	if (fd < 0) {
		close(ethert->host_fd);
		return fd;
	}

	tap_fix_macaddr(ethert->bridge_devname, ethert->bridge_devmac);
	set_nonblock(fd);
	ethert->bridge_fd = fd;

	switch (method) {
		case 0:
		default:
			ethert->op.init = sysrw_init;
			ethert->op.destroy = sysrw_destroy;
			ethert->op.forward = sysrw_forward;
			break;
	}

	if (ethert->op.init(&ethert->op.data)) {
		close(ethert->bridge_fd);
		close(ethert->host_fd);
		return -1;
	}

	return 0;
}

int destroy_tunnel(struct ether_tunnel *ethert)
{
	/* close the resources */
	ethert->op.destroy(ethert->op.data);
	close(ethert->host_fd);
	close(ethert->bridge_fd);
}

void parser_macaddr(unsigned char *buffer, unsigned char *macaddr)
{
	int start, i, idx;

	idx = i = start = 0;
	while (macaddr[i] != '\0') {
		if (macaddr[i] == ':') {
			macaddr[i] = '\0';
			buffer[idx] = strtol(&macaddr[start], NULL, 16);
			start = i + 1;
			idx++;
		}
		i++;
	}

	/* last byte */
	buffer[idx] = strtol(&macaddr[start], NULL, 16);
}

void usage(void)
{
	printf("Ethernet tunneling - version %s\n", ETHERTUN_VERSION);
	printf(" --hdevname <iface name>\tSets the host's interface name\n");
	printf("                        \tDefault: host0\n");
	printf(" --hdevmac <macaddr>\t\tSets the host's interface MAC ADDR\n");
	printf("                    \t\tDefault: FE:21:12:74:F3:88\n");
	printf(" --tdevname <iface name>\tSets the tunnel's interface name\n");
	printf("                        \tDefault: btun0\n");
	printf(" --tdevmac <macaddr>\t\tSets the tunnel's interface MAC ADDR\n");
	printf("                    \t\tDefault: FE:21:11:33:6D:05\n");
	printf(" --sysrw\t\t\tUse read/write syscalls\n");
	printf("                      \t\tDefault: sysrw\n");
	printf("\n");
}

int main(int argc, char *argv[])
{
	int rc;
	struct ether_tunnel ethert;
	unsigned char bridge_devmac[] = { 0xFE, 0x21, 0x11, 0x33, 0x6D, 0x05 };
	unsigned char host_devmac[] = { 0xFE, 0x21, 0x12, 0x74, 0xF3, 0x88 };
	int c;
	int method = 0;

	/* defaults */
	sprintf(ethert.bridge_devname, "%s", "btun0");
	memcpy(ethert.bridge_devmac, bridge_devmac, ETH_ALEN);
	sprintf(ethert.host_devname, "%s", "host0");
	memcpy(ethert.host_devmac, host_devmac, ETH_ALEN);

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"hdevname", 1, 0, 0},
			{"hdevmac", 1, 0, 0},
			{"tdevname", 1, 0, 0},
			{"tdevmac", 1, 0, 0},
			{"sysrw", 0, 0, 0},
			{"help", 0, 0, 0},
			{0, 0, 0, 0}
		};

		c = getopt_long (argc, argv, "", long_options, &option_index);
		if (c == -1)
			break;

		switch (option_index) {
			case 0:
				snprintf(ethert.host_devname, IFNAMSIZ, "%s", optarg);
				break;

			case 1:
				parser_macaddr(ethert.host_devmac, optarg);
				break;

			case 2:
				snprintf(ethert.bridge_devname, IFNAMSIZ, "%s", optarg);
				break;

			case 3:
				parser_macaddr(ethert.bridge_devmac, optarg);
				break;

			case 4:
				method = 0;
				break;

			case 5:
			default:
				usage();
				return -1;
		}
	}

	signal(SIGTERM, sigterm_handler);

#ifndef DEBUG
	daemon(0,0);
#endif

	if (create_tunnel(&ethert, method))
		goto out;

	if (start_tunnel(&ethert))
		goto out;

	pause();

	stop_tunnel(&ethert);
	destroy_tunnel(&ethert);
out:
	return rc;
}
