#ifndef _SYSRW_H_
#define _SYSRW_H_

int sysrw_init(void *);
int sysrw_destroy(void *);
ssize_t sysrw_forward(void *, struct forward_cmd *);

#endif
