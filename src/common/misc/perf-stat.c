/* 
 * All functions are borrowed from atop - photosyst in order to get system 
 * statistics.
 */
 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/param.h>
#include <regex.h>

#include "perf-stat.h"

static int isrealdisk(char *curname, char *newname, int maxlen);
static void nullmodname(char *curname, char *newname, int maxlen);
static void abbrevname1(char *curname, char *newname, int maxlen);
static count_t subcount(count_t newval, count_t oldval);

static void
abbrevname1(char *curname, char *newname, int maxlen)
{
    char    cutype[128];
    int hostnum, busnum, targetnum, lunnum;

    sscanf(curname, "%[^/]/host%d/bus%d/target%d/lun%d",
            cutype, &hostnum, &busnum, &targetnum, &lunnum);

    snprintf(newname, maxlen, "%c-h%db%dt%d", 
            cutype[0], hostnum, busnum, targetnum);
}
 
void
photosyst(struct sstat *si)
{
    register int    i, nr, nlines;
    count_t     cnts[MAXCNT];
    FILE        *fp;
    char        linebuf[1024], nam[64], origdir[1024];
    static char part_stats = 1; /* per-partition statistics ? */
    unsigned int    pgsz = sysconf(_SC_PAGESIZE);

    memset(si, 0, sizeof(struct sstat));

    getcwd(origdir, sizeof origdir);
    chdir("/proc");

    /*
    ** gather various general statistics from the file /proc/stat and
    ** store them in binary form
    */
    if ( (fp = fopen("stat", "r")) != NULL)
    {
        while ( fgets(linebuf, sizeof(linebuf), fp) != NULL)
        {
            nr = sscanf(linebuf,
             "%s   %lld %lld %lld %lld %lld %lld %lld %lld %lld "
             "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld "
             "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld "
             "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld "
             "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld "
             "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld "
             "%lld %lld %lld %lld %lld ",
                nam,
                &cnts[0],  &cnts[1],  &cnts[2],  &cnts[3],
                &cnts[4],  &cnts[5],  &cnts[6],  &cnts[7],
                &cnts[8],  &cnts[9],  &cnts[10], &cnts[11],
                &cnts[12], &cnts[13], &cnts[14], &cnts[15],
                &cnts[16], &cnts[17], &cnts[18], &cnts[19],
                &cnts[20], &cnts[21], &cnts[22], &cnts[23],
                &cnts[24], &cnts[25], &cnts[26], &cnts[27],
                &cnts[28], &cnts[29], &cnts[30], &cnts[31],
                &cnts[32], &cnts[33], &cnts[34], &cnts[35],
                &cnts[36], &cnts[37], &cnts[38], &cnts[39],
                &cnts[40], &cnts[41], &cnts[42], &cnts[43],
                &cnts[44], &cnts[45], &cnts[46], &cnts[47],
                &cnts[48], &cnts[49], &cnts[50], &cnts[51],
                &cnts[52], &cnts[53], &cnts[54], &cnts[55],
                &cnts[56], &cnts[57], &cnts[58], &cnts[59],
                &cnts[60], &cnts[61], &cnts[62], &cnts[63]);

            if (nr < 2)     /* headerline ? --> skip */
                continue;

            if ( strcmp("cpu", nam) == EQ)
            {
                si->cpu.all.utime   = cnts[0];
                si->cpu.all.ntime   = cnts[1];
                si->cpu.all.stime   = cnts[2];
                si->cpu.all.itime   = cnts[3];

                if (nr > 5) /* 2.6 kernel? */
                {
                    si->cpu.all.wtime   = cnts[4];
                    si->cpu.all.Itime   = cnts[5];
                    si->cpu.all.Stime   = cnts[6];
                }
                continue;
            }

            if ( strncmp("cpu", nam, 3) == EQ)
            {
                i = atoi(&nam[3]);

                si->cpu.cpu[i].cpunr    = i;
                si->cpu.cpu[i].utime    = cnts[0];
                si->cpu.cpu[i].ntime    = cnts[1];
                si->cpu.cpu[i].stime    = cnts[2];
                si->cpu.cpu[i].itime    = cnts[3];

                if (nr > 5) /* 2.6 kernel? */
                {
                    si->cpu.cpu[i].wtime    = cnts[4];
                    si->cpu.cpu[i].Itime    = cnts[5];
                    si->cpu.cpu[i].Stime    = cnts[6];
                }

                si->cpu.nrcpu++;
                continue;
            }

            if ( strcmp("ctxt", nam) == EQ)
            {
                si->cpu.csw = cnts[0];
                continue;
            }

            if ( strcmp("intr", nam) == EQ)
            {
                si->cpu.devint  = cnts[0];
                continue;
            }

            if ( strcmp("swap", nam) == EQ)   /* < 2.6 */
            {
                si->mem.swins   = cnts[0];
                si->mem.swouts  = cnts[1];
                continue;
            }
        }

        fclose(fp);

        if (si->cpu.nrcpu == 0)
            si->cpu.nrcpu = 1;
    }

    /*
    ** gather virtual memory statistics from the file /proc/vmstat and
    ** store them in binary form (>= kernel 2.6)
    */
    if ( (fp = fopen("vmstat", "r")) != NULL)
    {
        int nrfields = 2;   /* number of fields to be filled */

        while ( fgets(linebuf, sizeof(linebuf), fp) != NULL &&
                                nrfields > 0)
        {
            nr = sscanf(linebuf, "%s %lld", nam, &cnts[0]);

            if (nr < 2)     /* headerline ? --> skip */
                continue;

            if ( strcmp("pswpin", nam) == EQ)
            {
                si->mem.swins   = cnts[0];
                nrfields--;
                continue;
            }

            if ( strcmp("pswpout", nam) == EQ)
            {
                si->mem.swouts  = cnts[0];
                nrfields--;
                continue;
            }
        }

        fclose(fp);
    }

    /*
    ** gather memory-related statistics from the file /proc/meminfo and
    ** store them in binary form
    **
    ** in the file /proc/meminfo a 2.4 kernel starts with two lines
    ** headed by the strings "Mem:" and "Swap:" containing all required
    ** fields, except proper value for page cache
        ** if these lines are present we try to skip parsing the rest
    ** of the lines; if these lines are not present we should get the
    ** required field from other lines
    */
    si->mem.physmem     = (count_t)-1; 
    si->mem.freemem     = (count_t)-1;
    si->mem.buffermem   = (count_t)-1;
    si->mem.cachemem    = (count_t)-1;
    si->mem.slabmem     = (count_t) 0;
    si->mem.totswap     = (count_t)-1;
    si->mem.freeswap    = (count_t)-1;
    si->mem.committed   = (count_t) 0;

    if ( (fp = fopen("meminfo", "r")) != NULL)
    {
        int nrfields = 8;   /* number of fields to be filled */

        while ( fgets(linebuf, sizeof(linebuf), fp) != NULL && 
                                nrfields > 0)
        {
            nr = sscanf(linebuf,
                "%s %lld %lld %lld %lld %lld %lld %lld "
                    "%lld %lld %lld\n",
                nam,
                &cnts[0],  &cnts[1],  &cnts[2],  &cnts[3],
                &cnts[4],  &cnts[5],  &cnts[6],  &cnts[7],
                &cnts[8],  &cnts[9]);

            if (nr < 2)     /* headerline ? --> skip */
                continue;

            if ( strcmp("Mem:", nam) == EQ)
            {
                si->mem.physmem     = cnts[0] / pgsz; 
                si->mem.freemem     = cnts[2] / pgsz;
                si->mem.buffermem   = cnts[4] / pgsz;
                nrfields -= 3;
            }
            else    if ( strcmp("Swap:", nam) == EQ)
                {
                    si->mem.totswap  = cnts[0] / pgsz;
                    si->mem.freeswap = cnts[2] / pgsz;
                    nrfields -= 2;
                }
            else    if (strcmp("Cached:", nam) == EQ)
                {
                    if (si->mem.cachemem  == (count_t)-1)
                    {
                        si->mem.cachemem  =
                            cnts[0]*1024/pgsz;
                        nrfields--;
                    }
                }
            else    if (strcmp("MemTotal:", nam) == EQ)
                {
                    if (si->mem.physmem  == (count_t)-1)
                    {
                        si->mem.physmem  =
                            cnts[0]*1024/pgsz;
                        nrfields--;
                    }
                }
            else    if (strcmp("MemFree:", nam) == EQ)
                {
                    if (si->mem.freemem  == (count_t)-1)
                    {
                        si->mem.freemem  =
                            cnts[0]*1024/pgsz;
                        nrfields--;
                    }
                }
            else    if (strcmp("Buffers:", nam) == EQ)
                {
                    if (si->mem.buffermem  == (count_t)-1)
                    {
                        si->mem.buffermem  =
                            cnts[0]*1024/pgsz;
                        nrfields--;
                    }
                }
            else    if (strcmp("SwapTotal:", nam) == EQ)
                {
                    if (si->mem.totswap  == (count_t)-1)
                    {
                        si->mem.totswap  =
                            cnts[0]*1024/pgsz;
                        nrfields--;
                    }
                }
            else    if (strcmp("SwapFree:", nam) == EQ)
                {
                    if (si->mem.freeswap  == (count_t)-1)
                    {
                        si->mem.freeswap  =
                            cnts[0]*1024/pgsz;
                        nrfields--;
                    }
                }
            else    if (strcmp("Slab:", nam) == EQ)
                {
                    si->mem.slabmem = cnts[0]*1024/pgsz;
                    nrfields--;
                }
            else    if (strcmp("Committed_AS:", nam) == EQ)
                {
                    si->mem.committed = cnts[0]*1024/pgsz;
                    nrfields--;
                }
        }

        fclose(fp);
    }

    /*
    ** gather network-related statistics
    **  - IPv4      stats from the file /proc/net/snmp
    **  - IPv6      stats from the file /proc/net/snmp6
    **  - interface stats from the file /proc/net/dev
    */
    if ( (fp = fopen("net/snmp", "r")) != NULL)
    {
        nlines = 3;

        while ( fgets(linebuf, sizeof(linebuf), fp) != NULL &&
                                                       nlines > 0)
        {
            nr = sscanf(linebuf,
             "%s   %lld %lld %lld %lld %lld %lld %lld %lld %lld "
             "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld "
             "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld "
             "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld "
             "%lld\n",
                nam,
                &cnts[0],  &cnts[1],  &cnts[2],  &cnts[3],
                &cnts[4],  &cnts[5],  &cnts[6],  &cnts[7],
                &cnts[8],  &cnts[9],  &cnts[10], &cnts[11],
                &cnts[12], &cnts[13], &cnts[14], &cnts[15],
                &cnts[16], &cnts[17], &cnts[18], &cnts[19],
                &cnts[20], &cnts[21], &cnts[22], &cnts[23],
                &cnts[24], &cnts[25], &cnts[26], &cnts[27],
                &cnts[28], &cnts[29], &cnts[30], &cnts[31],
                &cnts[32], &cnts[33], &cnts[34], &cnts[35],
                &cnts[36], &cnts[37], &cnts[38], &cnts[39]);

            if (nr < 2)     /* headerline ? --> skip */
                continue;

            if ( strcmp("Ip:", nam) == EQ)
            {
                si->net.ipin    = cnts[2];
                si->net.ipout   = cnts[9];
                si->net.ipindel = cnts[8];
                si->net.ipfrw   = cnts[5];
                nlines--;
                continue;
            }

            if ( strcmp("Tcp:", nam) == EQ)
            {
                si->net.tcpin   = cnts[9];
                si->net.tcpout  = cnts[10];
                nlines--;
                continue;
            }

            if ( strcmp("Udp:", nam) == EQ)
            {
                si->net.udpin   = cnts[0];
                si->net.udpout  = cnts[3];
                nlines--;
                continue;
            }
        }

        fclose(fp);
    }

    if ( (fp = fopen("net/snmp6", "r")) != NULL)
    {
        count_t countval;

        /*
        ** one name-value pair per line
        */
        while ( fgets(linebuf, sizeof(linebuf), fp) != NULL)
        {
            nr = sscanf(linebuf, "%s %lld", nam, &countval);

            if (nr < 2)     /* unexpected line ? --> skip */
                continue;

            if ( memcmp("Icmp6", nam, 5) == EQ)
                continue;

            if ( memcmp("Ip6", nam, 3) == EQ)
            {
                if      ( strcmp("Ip6InReceives", nam) == EQ)
                    si->net.ipin    += countval;
                else if ( strcmp("Ip6OutRequests", nam) == EQ)
                    si->net.ipout   += countval;
                else if ( strcmp("Ip6InDelivers", nam) == EQ)
                    si->net.ipindel += countval;
                else if (strcmp("Ip6OutForwDatagrams",nam)==EQ)
                    si->net.ipfrw   += countval;

                continue;
            }

            if ( memcmp("Udp6", nam, 4) == EQ)
            {
                if      ( strcmp("Udp6InDatagrams", nam) == EQ)
                    si->net.udpin  += countval;
                else if ( strcmp("Udp6OutDatagrams", nam) ==EQ)
                    si->net.udpout += countval;

                continue;
            }
        }

        fclose(fp);
    }

    if ( (fp = fopen("net/dev", "r")) != NULL)
    {
        char *cp;

        i = 0;

        while ( fgets(linebuf, sizeof(linebuf), fp) != NULL)
        {
            if ( (cp = strchr(linebuf, ':')) != NULL)
                *cp = ' ';      /* substitute ':' by space */

            nr = sscanf(linebuf,
                "%15s  %lld %lld %*d %*d %*d %*d %*d "
                "%*d %lld %lld %*d %*d %*d %*d %*d %*d",
                  si->intf.intf[i].name,
                &(si->intf.intf[i].rbyte),
                &(si->intf.intf[i].rpack),
                &(si->intf.intf[i].sbyte),
                &(si->intf.intf[i].spack));

            if (nr == 5)    /* skip header & lines without stats */
            {
                if (++i >= MAXINTF-1)
                    break;
            }
        }

        si->intf.intf[i].name[0] = '\0'; /* set terminator for table */
        si->intf.nrintf = i;

        fclose(fp);
    }

    /*
    ** check if extended partition-statistics are provided < kernel 2.6
    */
    if ( part_stats && (fp = fopen("partitions", "r")) != NULL)
    {
        char diskname[256];

        i = 0;

        while ( fgets(linebuf, sizeof(linebuf), fp) )
        {
            nr = sscanf(linebuf,
                  "%*d %*d %*d %255s %lld %*d %lld %*d "
                  "%lld %*d %lld %*d %*d %lld %lld",
                    diskname,
                &(si->xdsk.xdsk[i].nread),
                &(si->xdsk.xdsk[i].nrblk),
                &(si->xdsk.xdsk[i].nwrite),
                &(si->xdsk.xdsk[i].nwblk),
                &(si->xdsk.xdsk[i].io_ms),
                &(si->xdsk.xdsk[i].avque) );

            /*
            ** check if this line concerns the entire disk
            ** or just one of the partitions of a disk (to be
            ** skipped)
            */
            if (nr == 7)    /* full stats-line ? */
            {
                if ( !isrealdisk(diskname,
                                 si->xdsk.xdsk[i].name,
                         MAXDKNAM) )
                       continue;
            
                if (++i >= MAXDSK-1)
                    break;
            }
        }

        si->xdsk.xdsk[i].name[0] = '\0'; /* set terminator for table */
        si->xdsk.nrxdsk = i;

        fclose(fp);

        if (i == 0)
            part_stats = 0; /* do not try again for next cycles */
    }


    /*
    ** check if disk-statistics are provided (kernel 2.6 onwards)
    */
    if ( (fp = fopen("diskstats", "r")) != NULL)
    {
        char diskname[256];
        i = 0;

        while ( fgets(linebuf, sizeof(linebuf), fp) )
        {
            nr = sscanf(linebuf,
                  "%*d %*d %255s %lld %*d %lld %*d "
                  "%lld %*d %lld %*d %*d %lld %lld",
                diskname,
                &(si->xdsk.xdsk[i].nread),
                &(si->xdsk.xdsk[i].nrblk),
                &(si->xdsk.xdsk[i].nwrite),
                &(si->xdsk.xdsk[i].nwblk),
                &(si->xdsk.xdsk[i].io_ms),
                &(si->xdsk.xdsk[i].avque) );

            /*
            ** check if this line concerns the entire disk
            ** or just one of the partitions of a disk (to be
            ** skipped)
            */
            if (nr == 7)    /* full stats-line ? */
            {
                if ( !isrealdisk(diskname,
                                 si->xdsk.xdsk[i].name,
                         MAXDKNAM) )
                       continue;
            
                if (++i >= MAXDSK-1)
                    break;
            }
        }

        si->xdsk.xdsk[i].name[0] = '\0'; /* set terminator for table */
        si->xdsk.nrxdsk = i;

        fclose(fp);
    }

    chdir(origdir);
}

/*
** set of subroutines to determine which disks should be monitored
** and to translate long name strings into short name strings
*/
static void
nullmodname(char *curname, char *newname, int maxlen)
{
    strncpy(newname, curname, maxlen-1);
    *(newname+maxlen-1) = 0;
}

/*
** table contains the names (in regexp format) of valid disks
** to be supported, together a function to modify the name-strings
** (i.e. to abbreviate long strings);
** this table is used in the function isrealdisk()
*/
static struct {
    char    *regexp;
    regex_t compreg;
    void    (*modname)(char *, char *, int);
} validdisk[] = {
    {   "^sd[a-z][a-z]*$",          {0},    nullmodname, },
    {   "^hd[a-z]$",                {0},    nullmodname, },
    {   "^rd/c[0-9][0-9]*d[0-9][0-9]*$",    {0},    nullmodname, },
    {   "^cciss/c[0-9][0-9]*d[0-9][0-9]*$", {0},    nullmodname, },
    {   "/host.*/bus.*/target.*/lun.*/disc",    {0},    abbrevname1, },
};

static int
isrealdisk(char *curname, char *newname, int maxlen)
{
    static int  firstcall = 1;
    register int    i;

    if (firstcall)      /* compile the regular expressions */
    {
        for (i=0; i < sizeof validdisk/sizeof validdisk[0]; i++)
            regcomp(&validdisk[i].compreg, validdisk[i].regexp,
                                REG_NOSUB);
        firstcall = 0;
    }

    /*
    ** try to recognize one of the compiled regular expressions
    */
    for (i=0; i < sizeof validdisk/sizeof validdisk[0]; i++)
    {
        if (regexec(&validdisk[i].compreg, curname, 0, NULL, 0) == 0)
        {
            /*
            ** name-string recognized; modify name-string
            */
            (*validdisk[i].modname)(curname, newname, maxlen);

            return 1;
        }
    }

    return 0;
}

/* borrowed from deviate.c */
#define MAX32BITVAL 0x100000000LL

/*
** calculate the system-activity during the last sample
*/
void
deviatsyst(struct sstat *cur, struct sstat *pre, struct sstat *dev)
{
    register int    i;

    dev->cpu.devint    = subcount(cur->cpu.devint, pre->cpu.devint);
    dev->cpu.csw       = subcount(cur->cpu.csw,    pre->cpu.csw);
    dev->cpu.nrcpu     = cur->cpu.nrcpu;

    dev->cpu.all.stime = subcount(cur->cpu.all.stime, pre->cpu.all.stime);
    dev->cpu.all.utime = subcount(cur->cpu.all.utime, pre->cpu.all.utime);
    dev->cpu.all.ntime = subcount(cur->cpu.all.ntime, pre->cpu.all.ntime);
    dev->cpu.all.itime = subcount(cur->cpu.all.itime, pre->cpu.all.itime);
    dev->cpu.all.wtime = subcount(cur->cpu.all.wtime, pre->cpu.all.wtime);
    dev->cpu.all.Itime = subcount(cur->cpu.all.Itime, pre->cpu.all.Itime);
    dev->cpu.all.Stime = subcount(cur->cpu.all.Stime, pre->cpu.all.Stime);

    if (dev->cpu.nrcpu == 1)
    {
        dev->cpu.cpu[0] = dev->cpu.all;
    }
    else
    {
        for (i=0; i < dev->cpu.nrcpu; i++)
        {
            dev->cpu.cpu[i].cpunr = cur->cpu.cpu[i].cpunr;
            dev->cpu.cpu[i].stime = subcount(cur->cpu.cpu[i].stime,
                                 pre->cpu.cpu[i].stime);
            dev->cpu.cpu[i].utime = subcount(cur->cpu.cpu[i].utime,
                                 pre->cpu.cpu[i].utime);
            dev->cpu.cpu[i].ntime = subcount(cur->cpu.cpu[i].ntime,
                                 pre->cpu.cpu[i].ntime);
            dev->cpu.cpu[i].itime = subcount(cur->cpu.cpu[i].itime,
                                 pre->cpu.cpu[i].itime);
            dev->cpu.cpu[i].wtime = subcount(cur->cpu.cpu[i].wtime,
                                 pre->cpu.cpu[i].wtime);
            dev->cpu.cpu[i].Itime = subcount(cur->cpu.cpu[i].Itime,
                                 pre->cpu.cpu[i].Itime);
            dev->cpu.cpu[i].Stime = subcount(cur->cpu.cpu[i].Stime,
                                 pre->cpu.cpu[i].Stime);
        }
    }

    dev->mem.physmem    = cur->mem.physmem;
    dev->mem.freemem    = cur->mem.freemem;
    dev->mem.buffermem  = cur->mem.buffermem;
    dev->mem.slabmem    = cur->mem.slabmem;
    dev->mem.committed  = cur->mem.committed;
    dev->mem.cachemem   = cur->mem.cachemem;
    dev->mem.totswap    = cur->mem.totswap;
    dev->mem.freeswap   = cur->mem.freeswap;

    dev->mem.swouts     = subcount(cur->mem.swouts,  pre->mem.swouts);
    dev->mem.swins      = subcount(cur->mem.swins,   pre->mem.swins);

    dev->net.ipin       = subcount(cur->net.ipin,    pre->net.ipin);
    dev->net.ipout      = subcount(cur->net.ipout,   pre->net.ipout);
    dev->net.ipindel    = subcount(cur->net.ipindel, pre->net.ipindel);
    dev->net.ipfrw      = subcount(cur->net.ipfrw,   pre->net.ipfrw);
    dev->net.tcpin      = subcount(cur->net.tcpin,   pre->net.tcpin);
    dev->net.tcpout     = subcount(cur->net.tcpout,  pre->net.tcpout);
    dev->net.udpin      = subcount(cur->net.udpin,   pre->net.udpin);
    dev->net.udpout     = subcount(cur->net.udpout,  pre->net.udpout);

    if (pre->intf.intf[0].name[0] == '\0')  /* first sample? */
    {
        for (i=0; cur->intf.intf[i].name[0]; i++)
            strcpy(pre->intf.intf[i].name, cur->intf.intf[i].name);
    }
    
    for (i=0; cur->intf.intf[i].name[0]; i++)
    {
        /*
        ** check if an interface has been added or removed;
        ** in that case, skip further handling for this sample
        */
        if (strcmp(cur->intf.intf[i].name, pre->intf.intf[i].name) != 0)
            break;

        strcpy(dev->intf.intf[i].name, cur->intf.intf[i].name);

        dev->intf.intf[i].rbyte = subcount(cur->intf.intf[i].rbyte,
                                              pre->intf.intf[i].rbyte);
        dev->intf.intf[i].rpack = subcount(cur->intf.intf[i].rpack,
                                          pre->intf.intf[i].rpack);
        dev->intf.intf[i].sbyte = subcount(cur->intf.intf[i].sbyte,
                                          pre->intf.intf[i].sbyte);
        dev->intf.intf[i].spack = subcount(cur->intf.intf[i].spack,
                                          pre->intf.intf[i].spack);
    }

    dev->intf.intf[i].name[0] = '\0';
    dev->intf.nrintf = i;

    for (i=0; cur->xdsk.xdsk[i].name[0]; i++)
    {
        strcpy(dev->xdsk.xdsk[i].name, cur->xdsk.xdsk[i].name);

        dev->xdsk.xdsk[i].nread  = subcount(cur->xdsk.xdsk[i].nread,
                                           pre->xdsk.xdsk[i].nread);
        dev->xdsk.xdsk[i].nwrite = subcount(cur->xdsk.xdsk[i].nwrite,
                                           pre->xdsk.xdsk[i].nwrite);
        dev->xdsk.xdsk[i].nrblk  = subcount(cur->xdsk.xdsk[i].nrblk,
                                           pre->xdsk.xdsk[i].nrblk);
        dev->xdsk.xdsk[i].nwblk  = subcount(cur->xdsk.xdsk[i].nwblk,
                                           pre->xdsk.xdsk[i].nwblk);
        dev->xdsk.xdsk[i].io_ms  = subcount(cur->xdsk.xdsk[i].io_ms,
                                           pre->xdsk.xdsk[i].io_ms);
        dev->xdsk.xdsk[i].avque  = subcount(cur->xdsk.xdsk[i].avque,
                                           pre->xdsk.xdsk[i].avque);
    }

    dev->xdsk.xdsk[i].name[0] = '\0';
    dev->xdsk.nrxdsk = i;
}

/*
** Generic function to subtract two counters taking into 
** account the possibility of overflow of a 32-bit kernel-counter.
*/
static count_t
subcount(count_t newval, count_t oldval)
{
    if (newval >= oldval)
        return newval - oldval;
    else
        return MAX32BITVAL + newval - oldval;
}

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */

