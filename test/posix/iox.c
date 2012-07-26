/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#undef _FILE_OFFSET_BITS

#include <stdlib.h>
#include <stdio.h>
#include <sys/uio.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <linux/unistd.h>
#include <mpi.h>

#define READ 0
#define WRITE 1
#define UIO_FASTIOV	8
#define UIO_MAXIOV	1024
#if defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
#define __NR_readx  321
#define __NR_writex 322
#elif defined (x86_64) || defined (__x86_64__)
#define __NR_readx  280
#define __NR_writex 281
#endif

#define BUFSIZE 65536

static int amode = O_RDWR | O_CREAT | O_LARGEFILE;
static int niters = 1, do_unlink = 0, correctness = 0, verbose = 0;

enum {
	MODE_RDWR = 0,
	MODE_RDWR_VEC = 1,
	MODE_RDWR_X = 2,
};

static int mode = -1;
static long bufsize = BUFSIZE;

struct xtvec {
	off_t xtv_off;
	size_t xtv_len;
};

static ssize_t readx(unsigned long fd,
		const struct iovec * iov, unsigned long iovlen, 
		const struct xtvec * xtv, unsigned long xtvlen)
{
	 return syscall(__NR_readx, fd, iov, iovlen, xtv, xtvlen);
}

static ssize_t writex(unsigned long fd, 
		const struct iovec * iov, unsigned long iovlen,
		const struct xtvec * xtv, unsigned long xtvlen)
{
	 return syscall(__NR_writex, fd, iov, iovlen, xtv, xtvlen);
}

#ifndef min
#define min(a, b) (a) < (b) ? (a) : (b)
#endif

#ifndef max
#define max(a, b) (a) > (b) ? (a) : (b)
#endif

#ifndef Ld
#define Ld(x) (x)
#endif

#ifndef FNAME
#define FNAME "/tmp/test.out"
#endif

static int memory_ct = 25, stream_ct = 25;
static char *fname = FNAME;

static ssize_t do_readx_writex(int type, int file,
			       const struct iovec  * uvector,
			       unsigned long nr_segs, 
					 const struct xtvec  * xtuvector,
					 unsigned long xtnr_segs)
{
	typedef ssize_t (*io_fn_t)(int, char *, size_t);
	typedef ssize_t (*iov_fn_t)(int, const struct iovec *, unsigned long);
	typedef ssize_t (*iox_fn_t)(int, const struct iovec *, unsigned long, 
			const struct xtvec *, unsigned long);

	size_t tot_len, tot_xtlen;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack;
	struct xtvec xtvstack[UIO_FASTIOV];
	struct xtvec *xtv=xtvstack;
	ssize_t ret;
	int seg;
	io_fn_t fn = NULL;
	iov_fn_t fnv = NULL;
	iox_fn_t fnx = NULL;

	/*
	 * readx does not make much sense if nr_segs <= 0 (OR) xtnr_segs <= 0
	 * We return 0 similar to how readv/writev do.
	 */
	ret = 0;
	if (nr_segs == 0 || xtnr_segs == 0)
		goto out;

	/*
	 * First get the "struct iovec" from user memory and
	 * verify all the pointers
	 */
	ret = -EINVAL;
	if ((nr_segs > UIO_MAXIOV) || (nr_segs <= 0))
		goto out;
	if ((xtnr_segs > UIO_MAXIOV) || (xtnr_segs <= 0))
		goto out;
	if (file < 0)
		goto out;
	if (nr_segs > UIO_FASTIOV) {
		ret = -ENOMEM;
		iov = malloc(nr_segs * sizeof(struct iovec));
		if (!iov)
			goto out;
	}
	if (xtnr_segs > UIO_FASTIOV) {
		ret = -ENOMEM;
		xtv = malloc(xtnr_segs * sizeof(struct xtvec));
		if (!xtv) {
			goto out;
		}
	}
	memcpy(iov, uvector, nr_segs * sizeof(*uvector));
	memcpy(xtv, xtuvector, xtnr_segs * sizeof(*xtuvector));

	tot_len = 0;
	ret = -EINVAL;
	for (seg = 0; seg < nr_segs; seg++) {
		ssize_t len = (ssize_t)iov[seg].iov_len;

		if (len < 0)	/* size_t not fitting an ssize_t .. */
			goto out;
		tot_len += len;
		if ((ssize_t)tot_len < 0) /* maths overflow on the ssize_t */
			goto out;
	}
	if (tot_len == 0) {
		ret = 0;
		goto out;
	}
	tot_xtlen = 0;
	ret = -EINVAL;
	for (seg = 0; seg < xtnr_segs; seg++) {
		loff_t off = (loff_t) xtv[seg].xtv_off;
		ssize_t len = (ssize_t)xtv[seg].xtv_len;

		if (len < 0)	/* size_t not fitting an ssize_t .. */
			goto out;
		if (off < 0)   /* off_t not fitting an loff_t */
			goto out;
		tot_xtlen += len;
		if ((ssize_t)tot_xtlen < 0) /* overflow on the ssize_t */
			goto out;
	}
	/* if sizes of file and mem don't match up, error out */
	if (tot_xtlen != tot_len) {
		ret = -EINVAL;
		goto out;
	}

	if (type == READ) {
		fn = (io_fn_t) &read;
		fnv = (iov_fn_t) &readv;
		fnx = (iox_fn_t) &readx;
	} else {
		fn = (io_fn_t) &write;
		fnv = (iov_fn_t) &writev;
		fnx = (iox_fn_t) &writex;
	}
	/* rdwrx specified for mode */
	if (mode == MODE_RDWR_X) {
		ret = fnx(file, iov, nr_segs, xtv, xtnr_segs);
		goto out;
	}
	/* else try to do it by hand using readv/writev operations */
	else if (mode == MODE_RDWR_VEC) {
		unsigned long xtiov_index = 0, op_iov_index = 0, iov_index = 0;
		struct iovec *op_iov = NULL, *copied_iovector = NULL;
		struct xtvec *copied_xtvector = NULL;

		ret = -ENOMEM;
		op_iov = (struct iovec *) malloc(nr_segs * sizeof(struct iovec));
		if (!op_iov) 
			goto err_out1;
		copied_iovector = (struct iovec *) malloc(nr_segs * sizeof(struct iovec));
		if (!copied_iovector) 
			goto err_out1;
		copied_xtvector = (struct xtvec *) malloc(xtnr_segs * sizeof(struct xtvec));
		if (!copied_xtvector)
			goto err_out1;
		memcpy(copied_iovector, iov, nr_segs * sizeof(struct iovec));
		memcpy(copied_xtvector, xtv, xtnr_segs * sizeof(struct xtvec));
		ret = 0;
		iov_index = 0;
		for (xtiov_index = 0; xtiov_index < xtnr_segs; xtiov_index++) {
			loff_t pos;
			ssize_t nr, tot_nr;

			pos = copied_xtvector[xtiov_index].xtv_off;
			lseek(file, pos, SEEK_SET);
			op_iov_index = 0;
			tot_nr = 0;
			
			/* Finish an entire stream and .. */
			while (copied_xtvector[xtiov_index].xtv_len > 0) {
				size_t min_len;
				if (iov_index >= nr_segs || op_iov_index >= nr_segs) {
					fprintf(stderr, "iov_index %ld or op_iov_index %ld cannot exceed number of iov segments (%ld)\n",
							iov_index, op_iov_index, nr_segs);
					ret = -EINVAL;
					goto err_out1;
				}
				min_len = min(copied_xtvector[xtiov_index].xtv_len, copied_iovector[iov_index].iov_len);
				op_iov[op_iov_index].iov_base = copied_iovector[iov_index].iov_base;
				op_iov[op_iov_index++].iov_len = min_len;
				copied_xtvector[xtiov_index].xtv_len -= min_len;
				copied_iovector[iov_index].iov_len -= min_len;
				copied_iovector[iov_index].iov_base = (char *) copied_iovector[iov_index].iov_base + min_len;
				tot_nr += min_len;
				/* Advance memory stream if we have exhausted it */
				if (copied_iovector[iov_index].iov_len <= 0) {
					iov_index++;
				}
			}
			/* .. issue a vectored operation for that region */
			nr = fnv(file, op_iov, op_iov_index);
			if (nr < 0) {
				if (!ret) ret = nr;
				break;
			}
			ret += nr;
			if (nr != tot_nr)
				break;
		}
err_out1:
		free(op_iov);
		free(copied_iovector);
		free(copied_xtvector);
		goto out;
	}
	/* Do it by hand, with plain read/write operations */
	else {
		unsigned long mem_ct = 0, str_ct = 0;
		struct xtvec *copied_xtvector = NULL;
		struct iovec *copied_iovector = NULL;

		ret = -ENOMEM;
		copied_iovector = (struct iovec *) malloc(nr_segs * sizeof(struct iovec));
		if (!copied_iovector)
			goto err_out2;
		copied_xtvector = (struct xtvec *) malloc(xtnr_segs * sizeof(struct xtvec));
		if (!copied_xtvector)
			goto err_out2;
		memcpy(copied_iovector, iov, nr_segs * sizeof(struct iovec));
		memcpy(copied_xtvector, xtv, xtnr_segs * sizeof(struct xtvec));

		ret = 0;
		mem_ct = 0;
		str_ct = 0;
		while ((mem_ct < nr_segs) && (str_ct < xtnr_segs)) {
			size_t min_len;
			loff_t pos;
			ssize_t nr;
			void  *base;

			pos = copied_xtvector[str_ct].xtv_off;
			lseek(file, pos, SEEK_SET);
			base = copied_iovector[mem_ct].iov_base;
			min_len = min(copied_xtvector[str_ct].xtv_len, copied_iovector[mem_ct].iov_len);
			copied_xtvector[str_ct].xtv_len -= min_len;
			copied_xtvector[str_ct].xtv_off += min_len;
			copied_iovector[mem_ct].iov_len -= min_len;
			copied_iovector[mem_ct].iov_base = (char *) copied_iovector[mem_ct].iov_base + min_len;
			if (copied_iovector[mem_ct].iov_len <= 0)
				mem_ct++;
			if (copied_xtvector[str_ct].xtv_len <= 0)
				str_ct++;
			/* Issue the smallest region that is contiguous in memory and on file */
			nr = fn(file, base, min_len);
			if (nr < 0) {
				if (!ret) ret = nr;
				break;
			}
			ret += nr;
			if (nr != min_len)
				break;
		}
err_out2:
		free(copied_xtvector);
		free(copied_iovector);
	}
out:
	if (iov != iovstack)
		free(iov);
	if (xtv != xtvstack)
		free(xtv);
	return ret;
}

static void usage(char *str)
{
	fprintf(stderr, "Usage: %s -f <filename> -s <stream count max> -b <buffer size> -m <mode> "
			"-c {correctness} -u {unlink file} -v {verbose}\n", str);
	return;
}

static void parse(int argc, char *argv[])
{
	int c;
	while ((c = getopt(argc, argv, "n:m:f:b:s:ucv")) != EOF) {
		switch (c) {
			case 'v':
				verbose = 1;
				break;
			case 'c':
				correctness = 1;
				break;
			case 'u':
				do_unlink = 1;
				break;
			case 'n':
				niters = atoi(optarg);
				break;
			case 'm':
				mode = atoi(optarg);
				break;
			case 'f':
				fname = optarg;
				break;
			case 'b':
				bufsize = atol(optarg);
				break;
			case 's':
				stream_ct = atoi(optarg);
				break;
			default:
				usage(argv[0]);
				exit(1);
		}
	}
	if (stream_ct <= 0 || bufsize <= 0)
	{
		fprintf(stderr, "Invalid stream count/buffer size\n");
		usage(argv[0]);
		exit(1);
	}
	if (mode != MODE_RDWR_X && mode != MODE_RDWR_VEC && mode != MODE_RDWR)
	{
		fprintf(stderr, "Invalid mode specified %d\n", mode);
		usage(argv[0]);
		exit(1);
	}
	if (correctness != 0 && correctness != 1)
	{
		fprintf(stderr, "Invalid values\n");
		usage(argv[0]);
		exit(1);
	}
	return;
}

static void fillup_buffers(char ***ptr, int nr_segs, int fill)
{
	int i;
	*ptr = (char **) malloc(nr_segs * sizeof(char *));
	for (i = 0; i < nr_segs; i++) 
	{
		char *p;
		p = (*ptr)[i] = (char *) calloc(1, bufsize);
		if (fill)
		{
			int j;
			for (j = 0; j < bufsize; j++) {
				*((char *) p + j) = 'a' + j % 26;
			}
		}
	}
	return;
}

static void free_buffers(char **ptr, int nr_segs)
{
	int i;
	for (i = 0; i < nr_segs; i++) {
		if (ptr[i])
			free(ptr[i]);
	}
	free(ptr);
}

static int compare_buffers(struct iovec *iov1, struct iovec *iov2, int count)
{
	int i, j;
	for (i = 0; i < count; i++) 
	{
		if (iov1[i].iov_len != iov2[i].iov_len)
		{
			fprintf(stderr, "length mismatch\n");
			break;
		}
		for (j = 0; j < iov1[i].iov_len; j++)
		{
			if (*((char *)iov1[i].iov_base + j) != *((char *) iov2[i].iov_base + j))
			{
				fprintf(stderr, "index %d, char %d in streamsize %ld\n",
						i, j, (long) iov1[i].iov_len);
				break;
			}
		}
		if (j != iov1[i].iov_len)
			break;
		/*
		if (memcmp(iov1[i].iov_base, iov2[i].iov_base, iov1[i].iov_len) == 0)
			continue;
		break;
		*/
	}
	if (i != count)
	{
		return 0;
	}
	else {
		return 1;
	}
}

int main(int argc, char *argv[])
{
	int fd, i, j, mynod=0, nprocs=1, err;
	int my_correct = 1, correct;
	unsigned long iter_jump = 0;
	double stim, etim;
	double write_tim = 0;
	double read_tim = 0;
	double read_bw, write_bw;
	double max_read_tim, max_write_tim;
	double min_read_tim, min_write_tim;
	double sum_read_tim, sum_write_tim;
	double ave_read_tim, ave_write_tim;
	double var_read_tim, var_write_tim;
	double sumsq_read_tim, sumsq_write_tim;
	double sq_read_tim, sq_write_tim;
	struct iovec *wvec, *rvec;
	struct xtvec *xc;
	unsigned long nr_segs = 0, xtnr_segs = 0;
	unsigned long total = 0, xt_total = 0, mem_total = 0;
	char **wrptr = NULL, **rdptr = NULL;
	loff_t loc;

	MPI_Init(&argc,&argv);
	MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
	MPI_Comm_rank(MPI_COMM_WORLD, &mynod);
	/* parse the command line arguments */
	parse(argc, argv);

	iter_jump = nprocs * bufsize;
	nr_segs = memory_ct;
	fillup_buffers(&wrptr, memory_ct, correctness);
	fillup_buffers(&rdptr, memory_ct, 0);
	wvec = (struct iovec *) malloc(nr_segs * sizeof(struct iovec));
	rvec = (struct iovec *) malloc(nr_segs * sizeof(struct iovec));
	total = bufsize;
	for (i = 0; i < nr_segs; i++)
	{
		if (i == nr_segs - 1)
		{
			wvec[i].iov_len = (total - mem_total);
		}
		else {
			wvec[i].iov_len = bufsize / nr_segs;
		}
		wvec[i].iov_base = (char *) wrptr[i];
		rvec[i].iov_len = wvec[i].iov_len;
		rvec[i].iov_base = (char *) rdptr[i];
		mem_total += wvec[i].iov_len;
		if (verbose)
			printf("%d) <%p,%p> WRITE %ld bytes\n", i, wvec[i].iov_base, 
				(char *) wvec[i].iov_base + wvec[i].iov_len, (long) wvec[i].iov_len); 
	}
	xtnr_segs = stream_ct;
	xc = (struct xtvec *) malloc(xtnr_segs * sizeof(struct xtvec));
	for (i = 0; i < xtnr_segs; i++)
	{
		if (i == xtnr_segs - 1)
		{
			xc[i].xtv_len = (mem_total - xt_total);
		}
		else {
			xc[i].xtv_len = bufsize / xtnr_segs;
		}
		xt_total += xc[i].xtv_len;
	}
	if (xt_total != mem_total)
	{
		fprintf(stderr, "mem_total (%ld) != xt_total (%ld)\n",
				(long) mem_total, (long) xt_total);
		goto err;
	}

	fd = open(fname, O_TRUNC | amode, 0644);
	if (fd < 0) {
		fprintf(stderr, "node %d, open error: %s\n", mynod,
			  strerror(errno));
		goto err;
	}
	/* repeat write number of times specified on the command line */
	for (j=0; j < niters; j++) {
		ssize_t cnt = 0;
		/* seek to an appropriate position depending on the iteration and
		 * rank of the current process */
		loc = j * iter_jump + (mynod * bufsize);
		lseek(fd, loc, SEEK_SET);
		MPI_Barrier(MPI_COMM_WORLD);
		stim = MPI_Wtime();
		/* Adjust the file offset */
		for (i = 0; i < xtnr_segs; i++)
		{
			xc[i].xtv_off = loc + cnt;
			cnt += xc[i].xtv_len;
			if (verbose)
				printf("%d) WRITE offset %ld length %zd\n", i, xc[i].xtv_off, xc[i].xtv_len);
		}
		if ((err = do_readx_writex(WRITE, fd, wvec, nr_segs,
				xc, xtnr_segs)) < 0) {
			fprintf(stderr, "(Write) readx_writex failed with error %s\n", strerror(-err));
			close(fd);
			goto err;
		}
		etim = MPI_Wtime();
		write_tim += (etim - stim);
	}
	close(fd);
	MPI_Barrier(MPI_COMM_WORLD);

	fd = open(fname, amode, 0644);

	if (fd < 0) {
		fprintf(stderr, "node %d, open error: %s\n", mynod,
			  strerror(errno));
		goto err;
	}
	/* repeat read, number of times specified on the command line */
	for (j=0; j < niters; j++) {
		ssize_t cnt = 0;
		/* seek to an appropriate position depending on the iteration and
		 * rank of the current process */
		loc = j * iter_jump + (mynod * bufsize);
		lseek(fd, loc, SEEK_SET);
		MPI_Barrier(MPI_COMM_WORLD);
		stim = MPI_Wtime();
		/* Adjust the file offset */
		for (i = 0; i < xtnr_segs; i++)
		{
			xc[i].xtv_off = loc + cnt;
			cnt += xc[i].xtv_len;
			if (verbose)
				printf("%d) READ offset %ld length %zd\n", i, xc[i].xtv_off, xc[i].xtv_len);
		}
		if ((err = do_readx_writex(READ, fd, rvec, nr_segs,
				xc, xtnr_segs)) < 0) {
			fprintf(stderr, "(Read) readx_writex failed with error %s\n", strerror(-err));
			close(fd);
			goto err;
		}
		etim = MPI_Wtime();
		read_tim += (etim - stim);
	}
	close(fd);

	if (do_unlink && mynod == 0) {
		unlink(fname);
	}

	MPI_Allreduce(&read_tim, &max_read_tim, 1, MPI_DOUBLE, MPI_MAX,
		MPI_COMM_WORLD);
	MPI_Allreduce(&read_tim, &min_read_tim, 1, MPI_DOUBLE, MPI_MIN,
		MPI_COMM_WORLD);
	MPI_Allreduce(&read_tim, &sum_read_tim, 1, MPI_DOUBLE, MPI_SUM,
		MPI_COMM_WORLD);
	/* calculate our part of the summation used for variance */
	sq_read_tim = read_tim - (sum_read_tim / nprocs);
	sq_read_tim = sq_read_tim * sq_read_tim;
	MPI_Allreduce(&sq_read_tim, &sumsq_read_tim, 1, MPI_DOUBLE, MPI_SUM,
		MPI_COMM_WORLD);

	MPI_Allreduce(&write_tim, &max_write_tim, 1, MPI_DOUBLE, MPI_MAX,
		MPI_COMM_WORLD);
	MPI_Allreduce(&write_tim, &min_write_tim, 1, MPI_DOUBLE, MPI_MIN,
		MPI_COMM_WORLD);
	MPI_Allreduce(&write_tim, &sum_write_tim, 1, MPI_DOUBLE, MPI_SUM,
		MPI_COMM_WORLD);
	/* calculate our part of the summation used for variance */
	sq_write_tim = write_tim - (sum_write_tim / nprocs);
	sq_write_tim = sq_write_tim * sq_write_tim;
	MPI_Allreduce(&sq_write_tim, &sumsq_write_tim, 1, MPI_DOUBLE, MPI_SUM,
		MPI_COMM_WORLD);
	/* calculate the mean from the sum */
	ave_read_tim = sum_read_tim / nprocs; 
	ave_write_tim = sum_write_tim / nprocs; 

	/* calculate variance */
	if (nprocs > 1) {
		var_read_tim = sumsq_read_tim / (nprocs-1);
		var_write_tim = sumsq_write_tim / (nprocs-1);
	}
	else {
		var_read_tim = 0;
		var_write_tim = 0;
	}
	if (mynod == 0) {
	   read_bw = ((int64_t)(bufsize*nprocs*niters))/
			(max_read_tim*1000000.0);
	   write_bw = ((int64_t)(bufsize*nprocs*niters))/
			(max_write_tim*1000000.0);
		
		printf("# Using %s mode\n", 
				(mode == MODE_RDWR) ? "read/write" : (mode == MODE_RDWR_VEC) ? "readv/writev" : "readx/writex");
		printf("# nr_procs = %d, nr_iter = %d, blk_sz = %ld, stream_ct = %d\n",
			nprocs, niters, bufsize, stream_ct);
		
		printf("# total_size = %ld\n", (bufsize*nprocs*niters));
		
		printf("# Write:  min_t = %f, max_t = %f, mean_t = %f, var_t = %f\n", 
			min_write_tim, max_write_tim, ave_write_tim, var_write_tim);
		printf("# Read:  min_t = %f, max_t = %f, mean_t = %f, var_t = %f\n", 
			min_read_tim, max_read_tim, ave_read_tim, var_read_tim);
		
		printf("Write bandwidth = %g Mbytes/sec\n", write_bw);
		printf("Read bandwidth = %g Mbytes/sec\n", read_bw);
	}
	if (correctness) {
		my_correct = compare_buffers(rvec, wvec, nr_segs);
		MPI_Allreduce(&my_correct, &correct, 1, MPI_INT, MPI_MIN,
						  MPI_COMM_WORLD);
		if (mynod == 0) {
			if (correct == 1)
				printf("Tests passed!\n");
			else 
				printf("Tests failed!\n");
		}
	}
	free_buffers(rdptr, memory_ct);
	free_buffers(wrptr, memory_ct);
	free(rvec);
	free(wvec);
	free(xc);
err:
	MPI_Finalize();
	exit(1);
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
