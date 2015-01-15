/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


/*
 * Function to help us print out either flavor of handle
 * from a gossip statement.
 *
 * The caller is responsible for allocating and freeing the space for s.
 */
char *k2s(PVFS_khandle *khandle, char *s) {
  int i;
  int dash_count = 0;
  int is64bit = 1;
  struct ihash bit64;
  unsigned char left;
  unsigned char right;
  char hex_lookup[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', \
                        '9', 'A', 'B', 'C', 'D', 'E', 'F' };

  /* determine size of handle. */
  for (i=4; i <= 11; i++)
    if (khandle->u[i] != '\0') {
      is64bit = 0;
      break;
    }

  if (is64bit) {
    /*
     * For 64 bit handles, arrange the bytes from the
     * khandle in an ihash union and sprintf the uint64_t
     * view into the return string.
     */
    bit64.u[0] = khandle->u[0];
    bit64.u[1] = khandle->u[1];
    bit64.u[2] = khandle->u[2];
    bit64.u[3] = khandle->u[3];
    bit64.u[4] = khandle->u[12];
    bit64.u[5] = khandle->u[13];
    bit64.u[6] = khandle->u[14];
    bit64.u[7] = khandle->u[15];
    sprintf(s,"%llu", (unsigned long long) bit64.ino);
  } else {
    /*
     * For 128 bit handles, each byte can be represented
     * as two hex digits.
     *
     * The cannonical way to represent a uuid in ascii is:
     *      xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
     * 4 bytes - 2 bytes - 2 bytes - 2 bytes - 6 bytes
     *
     * Fetch each byte from the khandle. 
     *
     * 01011010 -> 0101   1010  ->   7      A
     *  a byte     left   right     left  right
     * 
     * hex_lookup[left]  = ascii seven
     * hex_lookup[right] = ascii A
     *
     * The formulas in the sprintfs keep track of
     * where to place the ascii hex digits in the 
     * return string, along with where to sprinkle
     * in the dashes.
     *
     * They have added a function to lib/vsprintf.c
     * which could probably replace this home-grown
     * code with a single sprintf line... something
     * like this:
     *
     *     snprintf(s, 39, "%pUL", khandle->u);
     *
     * However, we need to use k2s from the client-core,
     * so we'll stick with this home-grown code for now.
     */
    for (i = 0; i < 16; i++) {
      left = hex_lookup[khandle->u[i] >> 4];
      right = khandle->u[i] << 4;
      right = hex_lookup[right >> 4];
      sprintf(s + (i * 2 + dash_count), "%c", left);
      sprintf(s + (i * 2 + 1 + dash_count), "%c", right);
      if ((i == 4) || (i == 6) || (i == 8) || (i == 10)) {
        sprintf(s + (i * 2 + 2 + dash_count), "%c", '-');
        dash_count++;
      }
    }
  }
  return(s);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

