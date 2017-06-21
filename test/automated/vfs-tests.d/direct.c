#define _GNU_SOURCE		/* syscall() is not POSIX */

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#define BUF "1234567890abcd\n"

/*
 *  "The thing that has always disturbed me about O_DIRECT is that
 *   the whole interface is just stupid, and was probably designed by
 *   a deranged monkey on some serious mind-controlling substances"
 *
 *                                       Linus
 */

int main(int argc, char *argv[])
{
	int fd;
	int rc;
	void *buf;

	if (argc != 2) {
		printf("usage: %s /file/name\n", argv[0]);
		exit(0);
	}

	if ((rc = posix_memalign(&buf, 512, 512) < 0 )) {
		printf("%s: alignment failed, errno:%d:\n", __func__, errno);
		return -1;
	}

	memset(buf, 0, 512);

	strcat(buf, BUF);

	fd = open(argv[1], O_RDWR | O_CREAT | O_DIRECT, S_IRUSR | S_IWUSR);

	if (fd < 0) {
		printf("%s: open failed, errno:%d:\n", __func__, errno);
		return -1;
	}

	if ((rc = write(fd, buf, 512)) < 0) {
		printf("%s: write failed, errno:%d:\n", __func__, errno);
		return -1;
	}

	close(fd);
}
