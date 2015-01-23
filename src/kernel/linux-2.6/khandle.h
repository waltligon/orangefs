/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 * The 2.9 core will put 64 bit handles in a PVFS_khandle like this:
 *    1234 0000 0000 5678
 * The 3.0 and beyond cores will put 128 bit handles in khandles like this:
 *    1234 5678 90AB CDEF
 * The kernel module will always use the first four bytes and
 * the last four bytes as an inum.
 */

/*
 * k2s is a function in pvfs2-utils whose job is to return a
 * string representation of a khandle. k2s callers are responsible
 * for allocating memory for the return string from k2s.
 * HANDLESTRINGSIZE is a safe amount of space for the memory allocation.
 */
char *k2s(PVFS_khandle *, char *);
#define HANDLESTRINGSIZE 40

/*
 * compare 2 khandles assumes little endian thus from large address to
 * small address
 */
static __inline__ int PVFS_khandle_cmp(const PVFS_khandle *kh1,
				       const PVFS_khandle *kh2)
{
  int i;

  for (i = 15; i >= 0; i--) {
    if (kh1->u[i] > kh2->u[i])
      return 1;
    if (kh1->u[i] < kh2->u[i])
      return -1;
  }

  return 0;
}

/* copy a khandle to a field of arbitrary size */
static __inline__ void PVFS_khandle_to(const PVFS_khandle *kh,
                                       void *p, int size)
{
  int i;
  unsigned char *c = p;

  memset(p, 0, size);

  for (i = 0; i < 16 && i < size; i++)
    c[i] = kh->u[i];
}

/* copy a khandle from a field of arbitrary size */
static __inline__ void PVFS_khandle_from(PVFS_khandle *kh,
                                         void *p, int size)
{
  int i;
  unsigned char *c = p;

  memset(kh, 0, 16);

  for (i = 0; i < 16 && i < size; i++)
    kh->u[i] = c[i];
}

/* ino_t descends from "unsigned long", 8 bytes, 64 bits. */
static ino_t pvfs2_khandle_to_ino(PVFS_khandle *khandle)
{
  struct ihash ihandle;

  ihandle.u[0] = khandle->u[0];
  ihandle.u[1] = khandle->u[1];
  ihandle.u[2] = khandle->u[2];
  ihandle.u[3] = khandle->u[3];
  ihandle.u[4] = khandle->u[12];
  ihandle.u[5] = khandle->u[13];
  ihandle.u[6] = khandle->u[14];
  ihandle.u[7] = khandle->u[15];

  return ihandle.ino;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

