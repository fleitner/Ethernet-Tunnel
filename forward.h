#ifndef _FORWARD_H_
#define _FORWARD_H_

/* jumbo frames - 9000 bytes */
#define PACKET_LENGTH 9000


enum direction {
	fd1TOfd2,
	fd2TOfd1
};

struct forward_cmd {
	int fd1;
	int fd2;
	ssize_t length;
	enum direction dir;
};

struct forward_operations {
	int (*init) (void *);
	int (*destroy) (void *);
	ssize_t (*forward) (void *, struct forward_cmd *);
	void *data;
};
 
#endif
