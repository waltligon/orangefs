/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <SDL.h>

#include "pvfs2.h"
#include "pvfs2-vis.h"
#include "pvfs2-mgmt.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

struct options
{
    char* mnt_point;
    int mnt_point_set;
};

static void* thread_fn(void* foo);
static struct options* parse_args(int argc, char* argv[]);
static void usage(int argc, char** argv);

#define S_LOCK() do{ \
if (SDL_MUSTLOCK(screen) ) { \
    SDL_LockSurface(screen);}\
}while(0)

#define S_UNLOCK() do{ \
if (SDL_MUSTLOCK(screen) ) { \
    SDL_UnlockSurface(screen);}\
}while(0)

struct drawbar
{
    SDL_Rect full;
    SDL_Rect bar;
};

int main(int argc, char **argv)	
{
    struct options* user_opts = NULL;
    int ret = -1;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	usage(argc, argv);
	return(-1);
    }

    ret = pvfs2_vis_start(user_opts->mnt_point, thread_fn);
    if(ret < 0)
    {
	PVFS_perror("pvfs2_vis_start", ret);
	return(-1);
    }

    return(0);
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char* argv[])
{
    /* getopt stuff */
    extern char* optarg;
    extern int optind, opterr, optopt;
    char flags[] = "vm:";
    int one_opt = 0;
    int len = 0;

    struct options* tmp_opts = NULL;
    int ret = -1;

    /* create storage for the command line options */
    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if(!tmp_opts){
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF){
	switch(one_opt)
        {
            case('v'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
	    case('m'):
		len = strlen(optarg)+1;
		tmp_opts->mnt_point = (char*)malloc(len+1);
		if(!tmp_opts->mnt_point)
		{
		    free(tmp_opts);
		    return(NULL);
		}
		memset(tmp_opts->mnt_point, 0, len+1);
		ret = sscanf(optarg, "%s", tmp_opts->mnt_point);
		if(ret < 1){
		    free(tmp_opts);
		    return(NULL);
		}
		/* TODO: dirty hack... fix later.  The remove_dir_prefix()
		 * function expects some trailing segments or at least
		 * a slash off of the mount point
		 */
		strcat(tmp_opts->mnt_point, "/");
		tmp_opts->mnt_point_set = 1;
		break;
	    case('?'):
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    if(!tmp_opts->mnt_point_set)
    {
	free(tmp_opts);
	return(NULL);
    }

    return(tmp_opts);
}


/* thread_fn()
 *
 * function that does the actual visualization work
 *
 * always returns NULL
 */
static void* thread_fn(void* foo)
{
    int i;
    int ret = -1;
    SDL_Surface* screen;
    struct drawbar* read_bws;
    struct drawbar* write_bws;
    int channel_width = 0;
    int left_offset = 0;
    double bw;
    double max_bw = 13.0;
    int stat_depth = pint_vis_shared.io_depth - 1;

    /* TODO: need a way to tell the main thread to stop if we have 
     * an error
     */

    /* TODO: ditto for the other direction; it would be nice to be able
     * to exit cleanly from this thread
     */

    ret = SDL_Init(SDL_INIT_VIDEO);
    if(ret < 0)
    {
	fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
	return(NULL);
    }
	
    screen = SDL_SetVideoMode(800, 600, 0, SDL_HWSURFACE|SDL_DOUBLEBUF);
    if(!screen)
    {
	fprintf(stderr, "SDL_SetVideoMode: %s\n", SDL_GetError());
	return(NULL);
    }

    SDL_WM_SetCaption("PVFS2 Server Bandwidth", "PVFS2");

    /* allocate two arrays of structs to track the state of each
     * bar that we might want to draw 
     */
    read_bws = (struct drawbar*)malloc(pint_vis_shared.io_count
	*2*sizeof(struct drawbar));
    if(!read_bws)
    {
	perror("malloc");
	return(NULL);
    }
    write_bws = &read_bws[pint_vis_shared.io_count];

    /* compute width of each bar */
    channel_width = (screen->w - 30) / (pint_vis_shared.io_count*3);
    if(channel_width > 30)
	channel_width = 30;

    left_offset = (screen->w - (channel_width*3))/2;

    /* fill in starting values for all bar graph areas */
    for(i=0; i<pint_vis_shared.io_count; i++)
    {
	read_bws[i].full.x = left_offset + i*3;
	read_bws[i].full.y = 30;
	read_bws[i].full.h = (screen->h-60);
	read_bws[i].full.w = channel_width;
	read_bws[i].bar = read_bws[i].full;
	read_bws[i].bar.h = 1;

	write_bws[i].full = read_bws[i].full;
	write_bws[i].bar = read_bws[i].bar;
	write_bws[i].full.x += channel_width+2;
	write_bws[i].bar.x += channel_width+2;
    }

    while(1)
    {
	pthread_mutex_lock(&pint_vis_mutex);
	pthread_cond_wait(&pint_vis_cond, &pint_vis_mutex);
	pthread_mutex_unlock(&pint_vis_mutex);
	
	pthread_mutex_lock(&pint_vis_mutex);
	for(i=0; i<pint_vis_shared.io_count; i++)
	{
	    S_LOCK();

	    /* fill in black background of each bar region */
	    SDL_FillRect(screen, &read_bws[i].full, 
		SDL_MapRGB(screen->format, 0x00, 0x00, 0x00));
	    SDL_FillRect(screen, &write_bws[i].full, 
		SDL_MapRGB(screen->format, 0x00, 0x00, 0x00));

	    /* compute height of each bar, and draw it */
	    bw = ((double)pint_vis_shared.io_perf_matrix[i][stat_depth].read * 1000.0)/
		(double)(pint_vis_shared.io_end_time_ms_array[i] -
		pint_vis_shared.io_perf_matrix[i][stat_depth].start_time_ms);
	    bw = bw / (double)(1024.0*1024.0);
	    read_bws[i].bar.h = read_bws[i].full.h * (bw/max_bw);
	    if(read_bws[i].bar.h > read_bws[i].full.h)
		read_bws[i].bar.h = read_bws[i].full.h;
	    read_bws[i].bar.y = 30 + read_bws[i].full.h - read_bws[i].bar.h;
	    SDL_FillRect(screen, &read_bws[i].bar,
		SDL_MapRGB(screen->format, 0xcc, 0x0, 0x0));

	    bw = ((double)pint_vis_shared.io_perf_matrix[i][stat_depth].write * 1000.0)/
		(double)(pint_vis_shared.io_end_time_ms_array[i] -
		pint_vis_shared.io_perf_matrix[i][stat_depth].start_time_ms);
	    bw = bw / (double)(1024.0*1024.0);
	    write_bws[i].bar.h = write_bws[i].full.h * (bw/max_bw);
	    if(write_bws[i].bar.h > write_bws[i].full.h)
		write_bws[i].bar.h = write_bws[i].full.h;
	    write_bws[i].bar.y = 30 + write_bws[i].full.h - write_bws[i].bar.h;
	    SDL_FillRect(screen, &write_bws[i].bar,
		SDL_MapRGB(screen->format, 0x0, 0x0, 0xcc));

	    S_UNLOCK();
	}
	pthread_mutex_unlock(&pint_vis_mutex);

	SDL_Flip(screen);
    }

    return(NULL);
}

static void usage(int argc, char** argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s [-m fs_mount_point]\n",
	argv[0]);
    fprintf(stderr, "Example: %s -m /mnt/pvfs2\n",
	argv[0]);
    return;
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
