#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>

#include "karma.h"

static struct gui_status_graph_data *graph_data = NULL;
static int graph_data_ct;

/* gui_status_data_prepare()
 */
void gui_status_data_prepare(struct PVFS_mgmt_server_stat *svr_stat,
			     int svr_stat_ct,
			     struct gui_status_graph_data **out_graph_data)
{
    int i, j, done = 0;
    int64_t total_free_handles = 0;
    float total_free_space = 0.0;
    float divisor;
    char *units;

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

	graph_data = (struct gui_status_graph_data *)
	    malloc(6 * sizeof(struct gui_status_graph_data));
	for (i=0; i < 6; i++) {
	    graph_data[i].first_val  = (float *) malloc(svr_stat_ct * sizeof(float));
	    graph_data[i].second_val = (float *) malloc(svr_stat_ct * sizeof(float));
	    graph_data[i].bar_color  = (int *) malloc(svr_stat_ct * sizeof(int));
	}
	graph_data_ct = svr_stat_ct;
    }
    
    /* process space information */
    i = 0;
    for (j=1; j < svr_stat_ct; j++) { 
	if (svr_stat[j].bytes_total > svr_stat[i].bytes_total) i = j;
    }
    units = gui_units_size(svr_stat[i].bytes_total, &divisor);

    snprintf(graph_data[GUI_STATUS_SPACE].title,
	     64,
	     "Used/Free Space (%s)",
	     units);
    graph_data[GUI_STATUS_SPACE].count = svr_stat_ct;
    graph_data[GUI_STATUS_SPACE].has_second_val = 1;

    total_free_space = 0.0;
    for (j=0; j < svr_stat_ct; j++) {
	float first, second;
	int bar;

	first = ((float) (svr_stat[j].bytes_total - svr_stat[j].bytes_available)) /
	    divisor;
	second = ((float) svr_stat[j].bytes_available) / divisor;

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
	     units);

    /* process uptime information */
    i = 0;
    for (j=1; j < svr_stat_ct; j++) {
	if (svr_stat[j].uptime_seconds > svr_stat[i].uptime_seconds) i = j;
    }
    units = gui_units_time(svr_stat[i].uptime_seconds, &divisor);

    snprintf(graph_data[GUI_STATUS_UPTIME].title,
	     64,
	     "System Uptime (%s)",
	     units);
    graph_data[GUI_STATUS_UPTIME].count = svr_stat_ct;
    graph_data[GUI_STATUS_UPTIME].has_second_val = 0;

    for (j=0; j < svr_stat_ct; j++) {
	int bar;

	graph_data[GUI_STATUS_UPTIME].first_val[j] =
	    ((float) svr_stat[j].uptime_seconds) / divisor;

	/* aesthetics: color */
	if (svr_stat[j].uptime_seconds > 3600.0)     bar = BAR_GREEN;
	else if (svr_stat[j].uptime_seconds > 300.0) bar = BAR_YELLOW;
	else                                         bar = BAR_RED;
	
	graph_data[GUI_STATUS_UPTIME].bar_color[j] = bar;
    }

    graph_data[GUI_STATUS_UPTIME].footer[0] = '\0';

    /* process meta handle information -- all handles for now. */
    i = 0;
    for (j=1; j < svr_stat_ct; j++) {
	if (svr_stat[j].handles_total_count > svr_stat[i].handles_total_count)
	{
	    i = j;
	}
    }
    units = gui_units_count(svr_stat[i].handles_total_count, &divisor);

    snprintf(graph_data[GUI_STATUS_META].title,
	     64,
	     "Used/Free Handles (%s)",
	     units);
    graph_data[GUI_STATUS_META].count = svr_stat_ct;
    graph_data[GUI_STATUS_META].has_second_val = 1;

    for (j=0; j < svr_stat_ct; j++) {
	int bar;
	float first, second;

	first = ((float) (svr_stat[j].handles_total_count -
			  svr_stat[j].handles_available_count)) / divisor;
	second = ((float) svr_stat[j].handles_available_count) / divisor;
	graph_data[GUI_STATUS_META].first_val[j]  = first;
	graph_data[GUI_STATUS_META].second_val[j] = second;

	/* aesthetics: color */
	if (svr_stat[j].handles_available_count < 100)       bar = BAR_RED;
	else if (svr_stat[j].handles_available_count < 1000) bar = BAR_YELLOW;
	else                                                 bar = BAR_GREEN;
	graph_data[GUI_STATUS_META].bar_color[j] = bar;

	total_free_handles += svr_stat[j].handles_available_count;
    }
    snprintf(graph_data[GUI_STATUS_META].footer,
	     64,
	     "Total Free Handles: %d %s",
	     (int) (total_free_handles / (int64_t) divisor),
	     units);

    /* process data handle information */
    snprintf(graph_data[GUI_STATUS_DATA].title,
	     64,
	     "<no separate data/meta info>");
    graph_data[GUI_STATUS_DATA].count          = 0;
    graph_data[GUI_STATUS_DATA].has_second_val = 1;
    graph_data[GUI_STATUS_DATA].footer[0]      = '\0';

    /* process memory information */
    i = 0;
    for (j=1; j < svr_stat_ct && !done; j++) { 
	if (svr_stat[j].ram_total_bytes > svr_stat[i].ram_total_bytes) i = j;
    }
    units = gui_units_size(svr_stat[i].ram_total_bytes, &divisor);

    snprintf(graph_data[GUI_STATUS_MEMORY].title,
	     64,
	     "Used/Free Memory (%s)",
	     units);
    graph_data[GUI_STATUS_MEMORY].count          = svr_stat_ct;
    graph_data[GUI_STATUS_MEMORY].has_second_val = 1;

    total_free_space = 0.0;
    for (j=0; j < svr_stat_ct; j++) {
	float first, second;
	int bar;

	first = ((float) (svr_stat[j].ram_total_bytes - svr_stat[j].ram_free_bytes))
	    / divisor;
	second = ((float) svr_stat[j].ram_free_bytes) / divisor;

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
	     units);

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

/* gui_traffic_data_prepare()
 *
 * Performs data formatting to convert "raw" performance data into
 * values for graphing (deciding on units, updating titles).
 *
 * Parameters:
 * raw    - pointer to array of raw data (svr_ct elements)
 * svr_ct - number of elements in raw array
 * graph  - pointer to preallocated graph data
 *
 * Assumes that svr_data pointer in graph points to allocated region.
 *
 */
void gui_traffic_data_prepare(struct gui_traffic_raw_data *raw,
			      int svr_ct,
			      struct gui_traffic_graph_data *graph)
{
    int i;
    uint64_t max_rate = 0;
    char *units;
    float divisor;
    static float hist_max_io = 0.0, hist_max_meta = 0.0;

    /* find max. I/O value, get units, format data */
    for (i=0; i < svr_ct; i++) {
	uint64_t time_ms = raw[i].elapsed_time_ms;
	float write_rate = 0.0, read_rate = 0.0;

	if (time_ms) {
	    write_rate = (raw[i].data_write_bytes * 1000) / time_ms;
	    read_rate  = (raw[i].data_read_bytes * 1000) / time_ms;

	    /* save these values for later */
	    graph->svr_data[i].data_write = (float) write_rate;
	    graph->svr_data[i].data_read = (float) read_rate;
	}

	if (write_rate > max_rate) max_rate = write_rate;
	if (read_rate > max_rate)  max_rate = read_rate;

	/* keep historical data to limit rate of change of units/divisor */
	if (hist_max_io < max_rate) hist_max_io = max_rate;
	else hist_max_io = 0.8 * hist_max_io + 0.2 * max_rate;
    }

    units = gui_units_size(hist_max_io, &divisor);

    snprintf(graph->io_label,
	     64,
	     "I/O Bandwidth (%s/sec)\norange = read, blue = write",
	     units);

    /* adjust for units */
    for (i=0; i < svr_ct; i++) {
	graph->svr_data[i].data_write = graph->svr_data[i].data_write /divisor;
	graph->svr_data[i].data_read  = graph->svr_data[i].data_read / divisor;
    }

    /* find max. metadata op value, get units, format data */
    max_rate = 0;
    for (i=0; i < svr_ct; i++) {
	uint64_t time_ms = raw[i].elapsed_time_ms;
	uint64_t write_rate = 0, read_rate = 0;

	if (time_ms) {
	    write_rate = raw[i].meta_write_ops * 1000 / time_ms;
	    read_rate  = raw[i].meta_read_ops * 1000 / time_ms;

	    graph->svr_data[i].meta_write = (float) write_rate;
	    graph->svr_data[i].meta_read  = (float) read_rate;
	}

	if (write_rate > max_rate) max_rate = write_rate;
	if (read_rate > max_rate) max_rate  = read_rate;

	if (hist_max_meta < max_rate) hist_max_meta = max_rate;
	else hist_max_meta = 0.8 * hist_max_meta + 0.2 * max_rate;
    }

    units = gui_units_ops(hist_max_meta, &divisor);

    snprintf(graph->meta_label,
	     64,
	     "Metadata Ops (%s/sec)\ngreen = read, purple = modify",
	     units);

    for (i=0; i < svr_ct; i++) {
	graph->svr_data[i].meta_write = graph->svr_data[i].meta_write /divisor;
	graph->svr_data[i].meta_read  = graph->svr_data[i].meta_read / divisor;
    }
}
