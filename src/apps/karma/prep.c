#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>

#include "karma.h"

static struct gui_graph_data *graph_data = NULL;
static int graph_data_ct;

/* tables for calculating units */
static float time_table[7]  = { 31557600.0, 2629800.0, 604800.0, 86400.0,
				3600.0, 60.0, 1.0 };
static char *time_abbrev[7] = { "years", "months", "weeks", "days", "hours",
				"min", "sec" };

static float size_table[5]  = { 1048576.0 * 1048576.0, 1024.0 * 1048576.0,
				1048576.0, 1024.0, 1.0 };
static char *size_abbrev[5] = { "TB", "GB", "MB", "KB" , "bytes" };

static float count_table[5]  = { 1000000000000.0, 1000000000.0, 1000000.0,
				 1000.0, 1.0 };
static char *count_abbrev[5] = { "trillion", "billion", "million", "thousand", "" };

void gui_data_prepare(struct PVFS_mgmt_server_stat *svr_stat,
		      int svr_stat_ct,
		      struct gui_graph_data **out_graph_data)
{
    int i, j, done = 0;
    int total_free_handles = 0;
    float total_free_space = 0.0;

    assert(svr_stat_ct > 0);

    /* deal with memory */
    if (graph_data == NULL || (graph_data_ct != svr_stat_ct)) {
	if (graph_data != NULL) {
	    for (i=0; i < 6; i++) {
		free(graph_data[i].first_val);
		free(graph_data[i].second_val);
		free(graph_data[i].bar_color);
	    }
	    free(graph_data);
	}

	graph_data = (struct gui_graph_data *)
	    malloc(6 * sizeof(struct gui_graph_data));
	for (i=0; i < 6; i++) {
	    graph_data[i].first_val  = (float *) malloc(svr_stat_ct * sizeof(float));
	    graph_data[i].second_val = (float *) malloc(svr_stat_ct * sizeof(float));
	    graph_data[i].bar_color  = (int *) malloc(svr_stat_ct * sizeof(int));
	}
	graph_data_ct = svr_stat_ct;
    }
    
    /* process space information */
    done = 0;
    for (i=0; i < 4; i++) {
	for (j=0; j < svr_stat_ct && !done; j++) { 
	    if (((float) svr_stat[j].bytes_total) / size_table[i] > 1.0) {
		done = 1;
		break;
	    }
	}
	if (done) break;
    }

    snprintf(graph_data[GUI_STATUS_SPACE].title,
	     64,
	     "Used/Free Space (%s)",
	     size_abbrev[i]);
    graph_data[GUI_STATUS_SPACE].count = svr_stat_ct;
    graph_data[GUI_STATUS_SPACE].has_second_val = 1;

    total_free_space = 0.0;
    for (j=0; j < svr_stat_ct; j++) {
	float first, second;
	int bar;

	first = ((float) (svr_stat[j].bytes_total - svr_stat[j].bytes_available)) /
	    size_table[i];
	second = ((float) svr_stat[j].bytes_available) / size_table[i];

	graph_data[GUI_STATUS_SPACE].first_val[j]  = first;
	graph_data[GUI_STATUS_SPACE].second_val[j] = second;

	/* aesthetics: somewhat arbitrary decisions on colors */
	if (second / (first + second) < 0.1)      bar = BAR_RED;
	else if (second / (first + second) < 0.3) bar = BAR_YELLOW;
	else                                      bar = BAR_GREEN;

	graph_data[GUI_STATUS_SPACE].bar_color[j] = bar;

	total_free_space += second;
    }

    snprintf(graph_data[GUI_STATUS_SPACE].footer,
	     64,
	     "Total Free Space: %.2f%s",
	     total_free_space,
	     size_abbrev[i]);

    /* process uptime information */
    done = 0;
    for (i=0; i < 6 && !done; i++) {
	for (j=0; j < svr_stat_ct && !done; j++) {
	    if (((float) svr_stat[j].uptime_seconds) / time_table[i] > 1.0) {
		done = 1;
	    }
	}
    }

    snprintf(graph_data[GUI_STATUS_UPTIME].title,
	     64,
	     "System Uptime (%s)",
	     time_abbrev[i]);
    graph_data[GUI_STATUS_UPTIME].count = svr_stat_ct;
    graph_data[GUI_STATUS_UPTIME].has_second_val = 0;

    for (j=0; j < svr_stat_ct; j++) {
	int bar;

	graph_data[GUI_STATUS_UPTIME].first_val[j] =
	    ((float) svr_stat[j].uptime_seconds) / time_table[i];

	/* aesthetics: color */
	if (svr_stat[j].uptime_seconds > 3600.0)     bar = BAR_GREEN;
	else if (svr_stat[j].uptime_seconds > 300.0) bar = BAR_YELLOW;
	else                                         bar = BAR_RED;
	
	graph_data[GUI_STATUS_UPTIME].bar_color[j] = bar;
    }

    graph_data[GUI_STATUS_UPTIME].footer[0] = '\0';

    /* process meta handle information -- all handles for now. */
    done = 0;
    for (i=0; i < 4; i++) {
	for (j=0; j < svr_stat_ct && !done; j++) {
	    if (((float) svr_stat[j].handles_total_count) / count_table[i] > 1.0) {
		done = 1;
		break;
	    }
	}
	if (done) break;
    }

    snprintf(graph_data[GUI_STATUS_META].title,
	     64,
	     "Used/Free Handles (%s)",
	     count_abbrev[i]);
    graph_data[GUI_STATUS_META].count = svr_stat_ct;
    graph_data[GUI_STATUS_META].has_second_val = 1;

    for (j=0; j < svr_stat_ct; j++) {
	int bar;
	float first, second;

	first = ((float) (svr_stat[j].handles_total_count -
			  svr_stat[j].handles_available_count)) / count_table[i];
	second = ((float) svr_stat[j].handles_available_count) / count_table[i];
	graph_data[GUI_STATUS_META].first_val[j]  = first;
	graph_data[GUI_STATUS_META].second_val[j] = second;

	/* aesthetics: color */
	if (svr_stat[j].handles_available_count < 100)       bar = BAR_RED;
	else if (svr_stat[j].handles_available_count < 1000) bar = BAR_YELLOW;
	else                                                 bar = BAR_GREEN;
	graph_data[GUI_STATUS_META].bar_color[j] = bar;

	total_free_handles += (int) (svr_stat[j].handles_available_count /
	    (int64_t) count_table[i]);
    }
    snprintf(graph_data[GUI_STATUS_META].footer,
	     64,
	     "Total Free Handles: %d %s",
	     total_free_handles,
	     count_abbrev[i]);

    /* process data handle information */
    snprintf(graph_data[GUI_STATUS_DATA].title,
	     64,
	     "<no separate data/meta info>");
    graph_data[GUI_STATUS_DATA].count          = 0;
    graph_data[GUI_STATUS_DATA].has_second_val = 1;
    graph_data[GUI_STATUS_DATA].footer[0]      = '\0';

    /* process memory information */
    done = 0;
    for (i=0; i < 4; i++) {
	for (j=0; j < svr_stat_ct && !done; j++) { 
	    if ((((float) svr_stat[j].ram_total_bytes) / size_table[i]) > 1.0) {
		done = 1;
		break;
	    }
	}
	if (done) break;
    }

    snprintf(graph_data[GUI_STATUS_MEMORY].title,
	     64,
	     "Used/Free Memory (%s)",
	     size_abbrev[i]);
    graph_data[GUI_STATUS_MEMORY].count          = svr_stat_ct;
    graph_data[GUI_STATUS_MEMORY].has_second_val = 1;

    total_free_space = 0.0;
    for (j=0; j < svr_stat_ct; j++) {
	float first, second;
	int bar;

	first = ((float) (svr_stat[j].ram_total_bytes - svr_stat[j].ram_free_bytes))
	    / size_table[i];
	second = ((float) svr_stat[j].ram_free_bytes) / size_table[i];

	graph_data[GUI_STATUS_MEMORY].first_val[j]  = first;
	graph_data[GUI_STATUS_MEMORY].second_val[j] = second;

	/* aesthetics: somewhat arbitrary decisions on colors */
	if (second / (first + second) < 0.1)      bar = BAR_RED;
	else if (second / (first + second) < 0.3) bar = BAR_YELLOW;
	else                                      bar = BAR_GREEN;

	graph_data[GUI_STATUS_MEMORY].bar_color[j] = bar;

	total_free_space += second;
    }

    snprintf(graph_data[GUI_STATUS_MEMORY].footer,
	     64,
	     "Total Free Memory: %.2f%s",
	     total_free_space,
	     size_abbrev[i]);

    /* process cpu information */
    snprintf(graph_data[GUI_STATUS_CPU].title,
	     64,
	     "<CPU info not supported>");
    graph_data[GUI_STATUS_CPU].count          = 0;
    graph_data[GUI_STATUS_CPU].has_second_val = 1;
    graph_data[GUI_STATUS_CPU].footer[0]      = '\0';

    /* return pointer to graph data */
    *out_graph_data = graph_data;

    return;
}
