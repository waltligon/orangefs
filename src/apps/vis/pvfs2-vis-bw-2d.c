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
#include <assert.h>

#include <SDL.h>
#include <SDL_ttf.h>

#include "pvfs2.h"
#include "pvfs2-vis.h"
#include "pvfs2-mgmt.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

#define TOP_BORDER 20
#define BOTTOM_BORDER 60
#define SIDE_BORDER 20

struct options
{
    char* mnt_point;
    int mnt_point_set;
    int width;
    int width_set;
    int update_interval;
    int update_interval_set;
};

static int draw(void);
static struct options* parse_args(int argc, char* argv[]);
static void usage(int argc, char** argv);
static struct options* user_opts = NULL;
static int check_for_exit(void);

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
    SDL_Rect peak;
};

int main(int argc, char **argv)	
{
    int ret = -1;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	usage(argc, argv);
	return(-1);
    }

    ret = pvfs2_vis_start(user_opts->mnt_point, user_opts->update_interval);
    if(ret < 0)
    {
	PVFS_perror("pvfs2_vis_start", ret);
	return(-1);
    }

    ret = draw();
    if(ret < 0)
    {
	fprintf(stderr, "Error: failed to draw visualization screen.\n");
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
    char flags[] = "vm:w:i:";
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
	    case('w'):
		ret = sscanf(optarg, "%d", &tmp_opts->width);
		if(ret < 1)
		{
		    free(tmp_opts);
		    return(NULL);
		}
		tmp_opts->width_set = 1;
		break;
	    case('i'):
		ret = sscanf(optarg, "%d", &tmp_opts->update_interval);
		if(ret < 1)
		{
		    free(tmp_opts);
		    return(NULL);
		}
		tmp_opts->update_interval_set = 1;
		break;

	    case('?'):
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    if(!tmp_opts->mnt_point_set || !tmp_opts->width_set || 
	!tmp_opts->update_interval_set)
    {
	free(tmp_opts);
	return(NULL);
    }

    return(tmp_opts);
}


/* draw()
 *
 * function that does the actual visualization work
 *
 * always returns NULL
 */
static int draw(void)
{
    int i,j;
    int ret = -1;
    SDL_Surface* screen;
    struct drawbar* read_bws;
    struct drawbar* write_bws;
    int channel_width = 0;
    int left_offset = 0;
    double bw;
    double max_bw = 1.0;
    int stat_depth = pint_vis_shared.io_depth - 1;
    SDL_Rect scratch;
    double** read_bw_matrix;
    double** write_bw_matrix;
    double peak;
    TTF_Font *font;
    SDL_Surface *text;
    char scratch_string[256];
    SDL_Color font_color = {0xcc, 0xcc, 0xcc, 0};
    SDL_Color black_color = {0x0, 0x0, 0x0, 0};
    SDL_Rect text_rect;

    ret = SDL_Init(SDL_INIT_VIDEO);
    if(ret != 0)
    {
	fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
	return(-1);
    }

    ret = TTF_Init();
    if(ret != 0)
    {
	fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
	SDL_Quit();
	return(-1);
    }

    font = TTF_OpenFont("VeraBd.ttf", 16);
    if(!font)
    {
	fprintf(stderr, "TTF_Openfont: %s\n", TTF_GetError());
	TTF_Quit();
	SDL_Quit();
	return(-1);
    }
	
    screen = SDL_SetVideoMode(user_opts->width, ((user_opts->width * 3)/4), 
	0, SDL_HWSURFACE|SDL_DOUBLEBUF);
    if(!screen)
    {
	fprintf(stderr, "SDL_SetVideoMode: %s\n", SDL_GetError());
	TTF_Quit();
	SDL_Quit();
	return(-1);
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
	TTF_Quit();
	SDL_Quit();
	return(-1);
    }
    write_bws = &read_bws[pint_vis_shared.io_count];

    /* allocate space to store calculated bandwidth figures */
    read_bw_matrix = (double**)malloc(2*pint_vis_shared.io_count*sizeof(double*));
    if(!read_bw_matrix)
    {
	perror("malloc");
	TTF_Quit();
	SDL_Quit();
	return(-1);
    }
    write_bw_matrix = &(read_bw_matrix[pint_vis_shared.io_count]);
    for(i=0; i<pint_vis_shared.io_count; i++)
    {
	read_bw_matrix[i] = (double*)malloc(2*pint_vis_shared.io_depth*sizeof(double));
	if(!read_bw_matrix[i])
	{
	    perror("malloc");
	    TTF_Quit();
	    SDL_Quit();
	    return(-1);
	}
	write_bw_matrix[i] = &(read_bw_matrix[i][pint_vis_shared.io_depth]);
    }

    /* compute width of each bar */
    channel_width = (screen->w - SIDE_BORDER*2) / (pint_vis_shared.io_count*3);
    if(channel_width > SIDE_BORDER)
	channel_width = SIDE_BORDER;

    left_offset = (screen->w - (channel_width*3))/2;

    /* fill in starting values for all bar graph areas */
    for(i=0; i<pint_vis_shared.io_count; i++)
    {
	read_bws[i].full.x = left_offset + i*3;
	read_bws[i].full.y = TOP_BORDER;
	read_bws[i].full.h = (screen->h-(TOP_BORDER+BOTTOM_BORDER));
	read_bws[i].full.w = channel_width;
	read_bws[i].bar = read_bws[i].full;
	read_bws[i].bar.h = 1;
	read_bws[i].peak = read_bws[i].full;
	read_bws[i].peak.h = 1;

	write_bws[i].full = read_bws[i].full;
	write_bws[i].bar = read_bws[i].bar;
	write_bws[i].peak = read_bws[i].peak;
	write_bws[i].full.x += channel_width+2;
	write_bws[i].bar.x += channel_width+2;
	write_bws[i].peak.x += channel_width+2;
    }

    while(1)
    {
	if(check_for_exit())
	{
	    TTF_Quit();
	    SDL_Quit();
	    return(0);
	}

	pthread_mutex_lock(&pint_vis_mutex);
	pthread_cond_wait(&pint_vis_cond, &pint_vis_mutex);

	if(check_for_exit() || pint_vis_error)
	{
	    pthread_mutex_unlock(&pint_vis_mutex);
	    TTF_Quit();
	    SDL_Quit();
	    return(0);
	}

	/* calculate bandwith for each time step and server */
	for(i=0; i<pint_vis_shared.io_count; i++)
	{
	    for(j=0; j<pint_vis_shared.io_depth; j++)
	    {
		if(j!=stat_depth)
		{
		    read_bw_matrix[i][j] = 
			((double)pint_vis_shared.io_perf_matrix[i][j].read * 1000.0)/
			(double)(pint_vis_shared.io_perf_matrix[i][j+1].start_time_ms -
			pint_vis_shared.io_perf_matrix[i][j].start_time_ms);
		    write_bw_matrix[i][j] = 
			((double)pint_vis_shared.io_perf_matrix[i][j].write * 1000.0)/
			(double)(pint_vis_shared.io_perf_matrix[i][j+1].start_time_ms -
			pint_vis_shared.io_perf_matrix[i][j].start_time_ms);
		}
		else
		{
		    read_bw_matrix[i][j] = 
			((double)pint_vis_shared.io_perf_matrix[i][j].read * 1000.0)/
			(double)(pint_vis_shared.io_end_time_ms_array[i] -
			pint_vis_shared.io_perf_matrix[i][j].start_time_ms);
		    write_bw_matrix[i][j] = 
			((double)pint_vis_shared.io_perf_matrix[i][j].write * 1000.0)/
			(double)(pint_vis_shared.io_end_time_ms_array[i] -
			pint_vis_shared.io_perf_matrix[i][j].start_time_ms);
		}
		read_bw_matrix[i][j] = read_bw_matrix[i][j] 
		    / (double)(1024.0*1024.0);
		if(read_bw_matrix[i][j] > max_bw)
		    max_bw = read_bw_matrix[i][j] + .5;
		write_bw_matrix[i][j] = write_bw_matrix[i][j] 
		    / (double)(1024.0*1024.0);
		if(write_bw_matrix[i][j] > max_bw)
		    max_bw = write_bw_matrix[i][j] + .5;
	    }
	}

	/* x axis */
	for(i=0; i<pint_vis_shared.io_count; i++)
	{
	    sprintf(scratch_string, "%d", i);
	    text = TTF_RenderText_Shaded(font, scratch_string, font_color, black_color);
	    assert(text);
	    text_rect.h = text->h;
	    text_rect.w = text->w;
	    text_rect.x = read_bws[i].full.x;
	    text_rect.y = read_bws[i].full.y + read_bws[i].full.h + 1;
	    ret = SDL_BlitSurface(text, NULL, screen, &text_rect);
	    assert(ret == 0);
	    SDL_FreeSurface(text);
	}

	/* draw a border */
	scratch.h = screen->h - TOP_BORDER - BOTTOM_BORDER + 1;
	scratch.w = screen->w - SIDE_BORDER*2;
	scratch.y = TOP_BORDER+2;
	scratch.x = SIDE_BORDER;
	SDL_FillRect(screen, &scratch, SDL_MapRGB(screen->format,
	    0xcc, 0xcc, 0xcc));
	scratch.h = scratch.h / 3 - 2;
	scratch.w -= 4;
	scratch.x += 2;
	scratch.y += 2;
	SDL_FillRect(screen, &scratch, SDL_MapRGB(screen->format,
	    0x0, 0x0, 0x0));
	scratch.y += scratch.h + 2;
	SDL_FillRect(screen, &scratch, SDL_MapRGB(screen->format,
	    0x0, 0x0, 0x0));
	scratch.y += scratch.h + 2;
	SDL_FillRect(screen, &scratch, SDL_MapRGB(screen->format,
	    0x0, 0x0, 0x0));

	/* label some parts of the graph */
	/* top of y */
	sprintf(scratch_string, "%.1f", max_bw);
	text = TTF_RenderText_Shaded(font, scratch_string, font_color, black_color);
	assert(text);
	text_rect.x = 2;
	text_rect.y = 2;
	text_rect.w = text->w;
	text_rect.h = text->h;
	ret = SDL_BlitSurface(text, NULL, screen, &text_rect);
	assert(ret == 0);
	SDL_FreeSurface(text);

	/* 2/3 of y */
	sprintf(scratch_string, "%.1f", 2*max_bw/3);
	text = TTF_RenderText_Shaded(font, scratch_string, font_color, black_color);
	assert(text);
	text_rect.y += scratch.h + 2;
	text_rect.w = text->w;
	ret = SDL_BlitSurface(text, NULL, screen, &text_rect);
	assert(ret == 0);
	SDL_FreeSurface(text);

	/* 1/3 of y */
	sprintf(scratch_string, "%.1f", max_bw/3);
	text = TTF_RenderText_Shaded(font, scratch_string, font_color, black_color);
	assert(text);
	text_rect.y += scratch.h + 2;
	text_rect.w = text->w;
	ret = SDL_BlitSurface(text, NULL, screen, &text_rect);
	assert(ret == 0);
	SDL_FreeSurface(text);

	/* key */
	text_rect.x = 4;
	text_rect.y = screen->h - text->h - 4;
	text_rect.w = text->h;
	SDL_FillRect(screen, &text_rect, SDL_MapRGB(screen->format,
	    0xcc, 0x0, 0x0));
	sprintf(scratch_string, "read bandwidth, MB/s");
	text = TTF_RenderText_Shaded(font, scratch_string, font_color, black_color);
	assert(text);
	text_rect.x = text_rect.x + text_rect.w + 4;
	text_rect.w = text->w;
	ret = SDL_BlitSurface(text, NULL, screen, &text_rect);
	assert(ret == 0);
	SDL_FreeSurface(text);

	text_rect.x = text_rect.x + text_rect.w + 16;
	text_rect.w = text->h;
	SDL_FillRect(screen, &text_rect, SDL_MapRGB(screen->format,
	    0x0, 0x0, 0xcc));
	sprintf(scratch_string, "write bandwidth, MB/s");
	text = TTF_RenderText_Shaded(font, scratch_string, font_color, black_color);
	assert(text);
	text_rect.x = text_rect.x + text_rect.w + 4;
	text_rect.w = text->w;
	ret = SDL_BlitSurface(text, NULL, screen, &text_rect);
	assert(ret == 0);
	SDL_FreeSurface(text);




	for(i=0; i<pint_vis_shared.io_count; i++)
	{
	    S_LOCK();

	    /* read bw bar */
	    bw = read_bw_matrix[i][stat_depth];
	    read_bws[i].bar.h = read_bws[i].full.h * (bw/max_bw);
	    if(read_bws[i].bar.h > read_bws[i].full.h)
		read_bws[i].bar.h = read_bws[i].full.h;
	    read_bws[i].bar.y = TOP_BORDER + read_bws[i].full.h - read_bws[i].bar.h;
	    SDL_FillRect(screen, &read_bws[i].bar,
		SDL_MapRGB(screen->format, 0xcc, 0x0, 0x0));

	    /* find the peak */
	    peak = 0;
	    for(j=0; j<pint_vis_shared.io_depth; j++)
	    {
		if(read_bw_matrix[i][j] > peak)
		    peak = read_bw_matrix[i][j];
	    }
	    read_bws[i].peak.h = read_bws[i].full.h * (peak/max_bw);
	    if(read_bws[i].peak.h > read_bws[i].full.h)
		read_bws[i].peak.h = read_bws[i].full.h;
	    read_bws[i].peak.y = TOP_BORDER + read_bws[i].full.h - read_bws[i].peak.h;
	    read_bws[i].peak.h = 3;
	    SDL_FillRect(screen, &read_bws[i].peak,
		SDL_MapRGB(screen->format, 0xee, 0x55, 0x55));

	    /* write bw bar */
	    bw = write_bw_matrix[i][stat_depth];
	    write_bws[i].bar.h = write_bws[i].full.h * (bw/max_bw);
	    if(write_bws[i].bar.h > write_bws[i].full.h)
		write_bws[i].bar.h = write_bws[i].full.h;
	    write_bws[i].bar.y = TOP_BORDER + write_bws[i].full.h - write_bws[i].bar.h;
	    SDL_FillRect(screen, &write_bws[i].bar,
		SDL_MapRGB(screen->format, 0x0, 0x0, 0xcc));

	    /* find the peak */
	    peak = 0;
	    for(j=0; j<pint_vis_shared.io_depth; j++)
	    {
		if(write_bw_matrix[i][j] > peak)
		    peak = write_bw_matrix[i][j];
	    }
	    write_bws[i].peak.h = write_bws[i].full.h * (peak/max_bw);
	    if(write_bws[i].peak.h > write_bws[i].full.h)
		write_bws[i].peak.h = write_bws[i].full.h;
	    write_bws[i].peak.y = TOP_BORDER + write_bws[i].full.h - write_bws[i].peak.h;
	    write_bws[i].peak.h = 3;
	    SDL_FillRect(screen, &write_bws[i].peak,
		SDL_MapRGB(screen->format, 0x55, 0x55, 0xee));

	    S_UNLOCK();
	}
	pthread_mutex_unlock(&pint_vis_mutex);

	SDL_Flip(screen);
    }

    TTF_Quit();
    SDL_Quit();
    return(0);
}

static void usage(int argc, char** argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s -m [fs_mount_point] -w [width in pixels] -i [update interval (msecs)]\n",
	argv[0]);
    fprintf(stderr, "Example: %s -m /mnt/pvfs2 -w 800 -i 1000\n",
	argv[0]);
    return;
}

static int check_for_exit(void)
{
    SDL_Event event;

    while(SDL_PollEvent(&event))
    {
	if(event.type == SDL_QUIT)
	{
	    return(1);
	}
	else if(event.type == SDL_KEYDOWN)
	{
	    switch(event.key.keysym.sym)
	    {
		case SDLK_q:
		case SDLK_ESCAPE:
		    return(1);
		    break;
		default:
		    break;
	    }
	}
    }
    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
