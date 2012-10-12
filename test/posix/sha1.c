#include "pvfs2-test-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include "sha1.h"
#ifdef WITH_OPENSSL
#ifdef HAVE_OPENSSL_EVP_H
#include <openssl/evp.h>
#endif
#ifdef HAVE_OPENSSL_CRYPTO_H
#include <openssl/crypto.h>
#endif

#if 0
static unsigned char *digest(unsigned char *mesg, unsigned mesgLen)
{
    EVP_MD_CTX mdctx;
    const EVP_MD *md = EVP_sha1();
    unsigned char *md_value;

    unsigned int md_len;

    md_value = (unsigned char *) malloc(EVP_MAX_MD_SIZE);

    EVP_MD_CTX_init(&mdctx);
    EVP_DigestInit_ex(&mdctx, md, NULL);
    EVP_DigestUpdate(&mdctx, mesg, mesgLen);
    EVP_DigestFinal_ex(&mdctx, md_value, &md_len);
    EVP_MD_CTX_cleanup(&mdctx);

    return md_value;
}
#endif

static int sha1(char *input_message, size_t input_length, unsigned char **output_hash, size_t *output_length)
{
	EVP_MD_CTX *digest = NULL;
	const EVP_MD *type = EVP_sha1();
	unsigned int olen;

	/* OpenSSL_add_all_digests(); */
	if (*output_hash == NULL) {
		*output_hash = (unsigned char *) calloc(sizeof(char), EVP_MAX_MD_SIZE);
	}
	if (!*output_hash) {
		return -ENOMEM;
	}
	digest = EVP_MD_CTX_create();
	if (digest == NULL) {
		return -EINVAL;
	}
	EVP_DigestInit(digest, type);
	EVP_DigestUpdate(digest, input_message, input_length);
	EVP_DigestFinal(digest, *output_hash, &olen); 
	EVP_MD_CTX_destroy(digest);
	*output_length = olen;
	return 0;
}

#if 0
static void hash2str(unsigned char *hash, int hash_length, unsigned char *str)
{
	int i, count = 0;

	if (!str || !hash || hash_length < 0) {
		return;
	}
	for (i = 0; i < hash_length; i++) {
		int cnt;
		cnt = sprintf((char *)str + count, "%02x", hash[i]);
		count += cnt;
	}
	return;
}
#endif

static void print_hash(unsigned char *hash, int hash_length)
{
	int i, count = 0;
	unsigned char str[256]; /* 41 bytes is sufficient.. but still */

	for (i = 0; i < hash_length; i++) {
		int cnt;
		cnt = snprintf((char *)str + count, 256, "%02x", hash[i]);
		count += cnt;
	}
	printf("%s\n", str);
	return;
}

void sha1_file_digest(int fd)
{
	struct stat statbuf;
	void *file_addr;
	int ret;
	size_t input_length = 0;
	unsigned char *hash = NULL;
	size_t  hash_length = 0;

	if (fstat(fd, &statbuf) < 0) {
		perror("fstat:");
		return;
	}
	if ((file_addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
		perror("mmap failed");
		return;
	}

	input_length = statbuf.st_size;
	if ((ret = sha1((char *)file_addr, input_length,
					&hash, &hash_length)) < 0) {
		perror("sha1 failed:");
		return;
	}
	munmap(file_addr, statbuf.st_size);
	print_hash(hash, hash_length);
	free(hash);
	return;
}
#else

void sha1_file_digest(int fd)
{
	return; 
}

#endif

#if 0
int main(int argc, char *argv[])
{
	int fd;
	char *fname;

	if (argc < 2) {
		exit(1);
	}
	fname = argv[1];
	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		perror("open:");
		exit(1);
	}
	sha1_file_digest(fd);
	close(fd);
	return 0;
}
#endif
