/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <pvfs2-sysint.h>
#include <pint-sysint.h>

int g_session_tag;
gen_mutex_t *g_session_tag_mt_lock;

void debug_print_type(void* thing, int type)
{
	if (type ==0)
	{
		struct PVFS_server_req_s * req = thing;
		switch( req->op )
		{
			case PVFS_SERV_SETATTR:
				printf("setattr request");
				break;
			case PVFS_SERV_GETCONFIG:
				printf("getconfig request");
				break;
			case PVFS_SERV_GETATTR:
				printf("getattr request");
				break;
			default:
				printf("unknown request = %d", req->op);
				break;
		}
	}
	else
	{
		struct PVFS_server_resp_s * resp = thing;
		switch( resp->op )
		{
			case PVFS_SERV_SETATTR:
				printf("setattr request");
				break;
			case PVFS_SERV_GETCONFIG:
				printf("getconfig reply");
				break;
			case PVFS_SERV_GETATTR:
				printf("getattr reply");
				break;
			default:
				printf("unknown reply = %d", resp->op);
				break;
		}
	}
}

int get_next_session_tag(void)
{
        int ret = 0;
        /* grab a lock for this variable */
        gen_mutex_lock(g_session_tag_mt_lock);

        ret = g_session_tag;

        /* increment the tag, don't use zero */
        if (g_session_tag + 1 == 0)
        {
                g_session_tag = 1;
        }
        else
        {
                g_session_tag++;
        }

        /* release the lock */
        gen_mutex_unlock(g_session_tag_mt_lock);
        return ret;
}


/* get_no_of_segments
 *
 * calculates the number of segments in the path
 *
 * returns nothing
 */
void get_no_of_segments(char *path,int *num)
{
	char *s = path;
	int len = strlen(path);
	int pos = 0;

	for(pos = 0;pos < len;pos++) 
	{
		if (s[pos] == '/' && (pos + 1) < len) 
			(*num)++;
	}
}

/* get_next_path
 *
 * gets remaining path given number of path segments to bypass 
 *
 * returns 0 on success, -errno on failure
 */
int get_next_path(char *fname,int num,int *start,int *end)
{
	char *ptr = fname;
	int pos = *start; /* Initialize the position to current */
	int len = strlen(fname);
	int cnt = 0; /* */

	/* NULL value passed */
	if (!fname)
	{
		*start = *end = 0;
		return(-1);
	}

	/* Skip through to the segment you want */
	while(pos < len)
	{
		if (ptr[pos] == '/')
			cnt++;
		if(cnt == (num + 1))
			break;
			
		/* Start of remaining path */
		if (cnt == num)
		{
			/* Record start position */
			*start = pos + 1;
		}
		pos++;
	}
	/* Record end position */
	*end = pos - 1;

	return(0);
}

/* get_next_segment
 *
 * gets next path segment given full pathname
 *
 * returns 0 on success, -errno on failure
 */
int get_next_segment(char *input, char **output, int *start)
{
	char *p1 = NULL,*p2 = NULL;
	char *s = (char *)&input[*start];
	int size = 0;

	/* Search for start of segment */
	p1 = index(s,'/');
	if (p1 != NULL)
	{
		/* Search for end of segment */
		p2 = index(p1 + 1,'/');
		if (p2 != NULL)
			size = p2 - p1;
		else
			size = &(input[strlen(input)]) - p1;
	}
	else
	{
		size = strlen(s) + 1;
	}

	/* In case we have no segment e.g "/" */
	if (size - 1)
	{
		/* Set up the segment to be returned */
		*output = (char *)malloc(size);
		strncpy(*output, p1 + 1, size - 1);
		(*output)[size - 1] = '\0';
		*start += size;
	}
	else
	{
		*output = NULL;
		*start = -1;
	}

	return(0);
}

/* check_perms
 *
 * check permissions of a PVFS object against the access mode
 *
 * returns 0 on success, -1 on error
 */
int check_perms(PVFS_object_attr attr,PVFS_permissions mode,int uid,int gid)
{
	int ret = 0;

	if (((attr.perms & 7) & mode) == mode)
		ret = 0;
	else if (((attr.group == gid && (attr.perms >> 3) & 7) & mode) == mode)
		ret = 0;
	else if (((attr.owner == uid && (attr.perms >> 6) & 7) & mode) == mode)
		ret = 0;
	else
		ret = -1;

	return(ret);
}

/* sysjob_free
 *  
 * frees job associated data structures
 *
 * returns 0 on success, -errno on failure
 * 
 */
int sysjob_free(bmi_addr_t server,void *tmp_job,bmi_size_t size, const int op,
	int (*func)(void *,int))
{
	/* Call the respective free function */
	if (func)
		(*func)(tmp_job,0);
	if (tmp_job)
		BMI_memfree(server,tmp_job,size,op);

	return(0);
}

