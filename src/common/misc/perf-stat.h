/* 
 * All functions are borrowed from atop - photosyst in order to get system 
 * statistics.
 */

#ifdef HAVE_SYSINFO
#include <sys/sysinfo.h>
#endif


#ifndef PERFSTAT_H_
#define PERFSTAT_H_

/* borrowed from atop */
typedef long long   count_t;
#define MAXCPU      64
#define MAXDSK      128
#define MAXDKNAM    32
#define MAXINTF     32
#define MAXCNT  64

#define  EQ              0
/************************************************************************/

struct  memstat {
    count_t physmem;    /* number of physical pages     */
    count_t freemem;    /* number of free     pages */
    count_t buffermem;  /* number of buffer   pages */
    count_t slabmem;    /* number of slab     pages */
    count_t cachemem;   /* number of cache    pages */
    count_t committed;  /* number of reserved pages */

    count_t swouts;     /* number of pages swapped out  */
    count_t swins;      /* number of pages swapped in   */

    count_t totswap;    /* number of pages in swap  */
    count_t freeswap;   /* number of free swap pages    */
    count_t future1;    /* reserved for future use  */
    count_t future2;    /* reserved for future use  */
    count_t future3;    /* reserved for future use  */
};

/************************************************************************/

struct  netstat {
    count_t tcpin;  /* TCP received packets         */
    count_t tcpout; /* TCP transmitted packets      */

    count_t udpin;  /* UDP received packets         */
    count_t udpout; /* UDP transmitted packets      */

    count_t ipin;   /* IP  received packets         */
    count_t ipout;  /* IP  transmitted packets      */
    count_t ipindel;/* IP  locally delivered packets    */
    count_t ipfrw;  /* IP  forwarded packets        */
    count_t future1;    /* reserved for future use  */
    count_t future2;    /* reserved for future use  */
    count_t future3;    /* reserved for future use  */
};

/************************************************************************/

struct percpu {
    int cpunr;
    count_t stime;  /* system  time in clock ticks      */
    count_t utime;  /* user    time in clock ticks      */
    count_t ntime;  /* nice    time in clock ticks      */
    count_t itime;  /* idle    time in clock ticks      */
    count_t wtime;  /* iowait  time in clock ticks      */
    count_t Itime;  /* irq     time in clock ticks      */
    count_t Stime;  /* softirq time in clock ticks      */
    count_t future1;    /* reserved for future use  */
    count_t future2;    /* reserved for future use  */
    count_t future3;    /* reserved for future use  */
};

struct  cpustat {
    count_t devint; /* number of device interrupts      */
    count_t csw;    /* number of context switches       */
    count_t nrcpu;  /* number of cpu's          */
    count_t future1;    /* reserved for future use  */
    count_t future2;    /* reserved for future use  */

    struct percpu   all;
    struct percpu   cpu[MAXCPU];
};

/************************************************************************/

struct  perxdsk {
        char    name[MAXDKNAM]; /* empty string for last        */
        count_t nread;  /* number of read  transfers            */
        count_t nrblk;  /* number of sectors read               */
        count_t nwrite; /* number of write transfers            */
        count_t nwblk;  /* number of sectors written            */
        count_t io_ms;  /* number of millisecs spent for I/O    */
        count_t avque;  /* average queue length                 */
    count_t future1;    /* reserved for future use  */
    count_t future2;    /* reserved for future use  */
    count_t future3;    /* reserved for future use  */
};

struct xdskstat {
    int     nrxdsk;
    struct perxdsk  xdsk[MAXDSK];
};

/************************************************************************/

struct  perintf {
        char    name[16];   /* empty string for last        */
        count_t rbyte;  /* number of read bytes                 */
        count_t rpack;  /* number of read packets               */
        count_t sbyte;  /* number of written bytes              */
        count_t spack;  /* number of written packets            */
    count_t future1;    /* reserved for future use  */
    count_t future2;    /* reserved for future use  */
    count_t future3;    /* reserved for future use  */
};

struct intfstat {
    int     nrintf;
    struct perintf  intf[MAXINTF];
};


struct  sstat {
    struct cpustat  cpu;
    struct memstat  mem;
    struct netstat  net;
    struct intfstat intf;
    struct xdskstat xdsk;
};

void photosyst(struct sstat *si);
void deviatsyst(struct sstat *cur, struct sstat *pre, struct sstat *dev);

#endif /*PERFSTAT_H_*/
