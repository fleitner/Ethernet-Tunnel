
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "forward.h"

struct sysrw_data {
	char *in_buffer;
	char *out_buffer;
	size_t length;
};


int sysrw_init(void *data)
{
	unsigned long *ptr = data;
	struct sysrw_data *s;

	s = malloc(sizeof(struct sysrw_data));
	if (!s)
		return -ENOMEM;

	s->in_buffer = malloc(PACKET_LENGTH);
	if (!s->in_buffer) {
		free(s);
		return -ENOMEM;
	}

	s->out_buffer = malloc(PACKET_LENGTH);
	if (!s->out_buffer) {
		free(s->in_buffer);
		free(s);
		return -ENOMEM;
	}

	s->length = PACKET_LENGTH;
	*ptr = (unsigned long)s;
	return 0;

}


int sysrw_destroy(void *data)
{
	struct sysrw_data *s = (struct sysrw_data *)data; 

	if (s->in_buffer)
		free(s->in_buffer);

	if (s->out_buffer)
		free(s->out_buffer);

	free(s);

	return 0;
}


static int forward(int orig, int dest, char *buffer, size_t len)
{
	ssize_t bytes;

	while ((bytes = read(orig, buffer, len)) > 0)
		write(dest, buffer, bytes);

	/* FIXME: return the right amount of bytes */
	return len;
}


ssize_t sysrw_forward(void *data, struct forward_cmd *cmd)
{
	struct sysrw_data *s = (struct sysrw_data *)data;
	size_t len;

	len = -ENOMEM;
	if (cmd->length > s->length)
		goto out;

	if (cmd->dir == fd1TOfd2)
		len = forward(cmd->fd1, cmd->fd2, s->out_buffer, cmd->length);
	else
		len = forward(cmd->fd2, cmd->fd1, s->in_buffer, cmd->length);

out:
	return len;
}

