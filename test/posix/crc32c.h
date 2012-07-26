#ifndef _CRC32C_H
#define _CRC32C_H

#include <sys/types.h>

struct chksum_ctx {
	unsigned int crc;
};

extern unsigned int crc32c_le(unsigned int crc, unsigned char const *address, size_t length);
extern unsigned int crc32c_be(unsigned int crc, unsigned char const *address, size_t length);

#define crc32c(seed, data, length)  crc32c_le(seed, (unsigned char const *)data, length)

void chksum_init(void *ctx);
void chksum_update(void *ctx, const unsigned char *data, unsigned int length);
void chksum_final(void *ctx, unsigned char *out);
int chksum_setkey(void *ctx, const unsigned char *key, unsigned int keylen,
	                  unsigned int *flags);

int32_t compute_check_sum(unsigned char *text, int len);

#endif	/* _LINUX_CRC32C_H */
