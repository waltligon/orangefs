/*
 * (C) 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pvfs2.h>
#include <pvfs2-usrint.h>
#include <pvfs2-bgproc.h>

static struct bgproc_prop *props = NULL;
static int nprops = 0;

/*
 * Add a property with an empty value to the props array. 
 */
int bgproc_newprop(char *key, int type)
{
        if (props != NULL) {
                props = realloc(props,
                        (nprops+1)*sizeof(struct bgproc_prop));
                if (props == NULL)
                        return 1;
                nprops += 1;
        } else {
                props = malloc(sizeof(struct bgproc_prop));
                if (props == NULL)
                        return 1;
                nprops = 1;
        }
        props[nprops-1].key = strdup(key);
        if (props[nprops-1].key == NULL) {
                if (nprops > 1) {
                        /* Re-allocation is unnecessary. Memory is not
                         * permanently lost and will be reclaimed if and
                         * when bgproc_newprop is run again. */
                        nprops -= 1;
                } else {
                        free(props);
                        props = NULL;
                        nprops = 1;
                }
                return 1;
        }
        props[nprops-1].type = type;
        props[nprops-1].value = NULL;
        return 0;
}

/*
 * Delete a property, and delete the entire table if it would be empty.
 */
int bgproc_delprop(char *key)
{
        int i, found = 0;
        for (i = 0; i < nprops; i++) {
                if (strcmp(key, props[i].key) == 0) {
                        free(props[i].key);
                        /* XXX: free value? */
                        found = 1;
                }
                if (found && i+1 < nprops) {
                        props[i].key = props[i+1].key;
                        props[i].type = props[i+1].type;
                        props[i].value = props[i+1].value;
                }
        }
        if (found) {
                if (nprops > 1) {
                        props = realloc(props,
                                (nprops-1)*sizeof(struct bgproc_prop));
                        nprops -= 1;
                        /* Take care to decrease nprops when realloc
                         * fails. */
                        if (props == NULL)
                                return 1;
                } else {
                        free(props);
                        props = NULL;
                        nprops = 0;
                }
        } else {
                return 1;
        }
        return 0;
}

/*
 * Return an iteration handle representing the start of the table.
 */
int bgproc_iterstart(void)
{
        return 0;
}

/*
 * Return the key associated with the handle.
 */
char *bgproc_iternext(int i)
{
        if (i < nprops)
                return props[i].key;
        else
                return NULL;
}

/*
 * Find a struct bgproc_prop given a key.
 */
static struct bgproc_prop *bgproc_find(char *key)
{
        int i;
        for (i = 0; i < nprops; i++) {
                if (strcmp(key, props[i].key) == 0) {
                        return props+i;
                }
        }
        return NULL;
}

/*
 * Unset a value for a property.
 */
int bgproc_unset(char *key)
{
        struct bgproc_prop *prop;
        prop = bgproc_find(key);
        if (prop != NULL) {
                if (prop->value != NULL)
                        free(prop->value);
                prop->value = NULL;
                return 0;
        }
        return 1;
}

/*
 * Get the type for a property. Returns zero if the key is not found.
 */
int bgproc_gettype(char *key)
{
        struct bgproc_prop *prop;
        prop = bgproc_find(key);
        if (prop != NULL)
                return prop->type;
        return 0;
}

/*
 * Set an integer value for a property.
 */
int bgproc_set_int(char *key, int value)
{
        struct bgproc_prop *prop;
        prop = bgproc_find(key);
        if (prop != NULL) {
                prop->value = malloc(sizeof(int));
                if (prop->value == NULL)
                        return 1;
                *((int *)prop->value) = value;
                return 0;
        }
        return 1;
}

/*
 * Get an integer value for a property.
 */
int bgproc_get_int(char *key, int *value)
{
        struct bgproc_prop *prop;
        prop = bgproc_find(key);
        if (prop != NULL) {
                *value = *((int *)prop->value);
                return 0;
        }
        return 1;
}

/*
 * Set a string value for a property.
 */
int bgproc_set_str(char *key, char *value)
{
        struct bgproc_prop *prop;
        prop = bgproc_find(key);
        if (prop != NULL) {
                prop->value = strdup(value);
                if (prop->value == NULL)
                        return 1;
                return 0;
        }
        return 1;
}

/*
 * Get a string value for a property.
 */
int bgproc_get_str(char *key, char **value)
{
        struct bgproc_prop *prop;
        prop = bgproc_find(key);
        if (prop != NULL) {
                *value = (char *)prop->value;
                return 0;
        }
        return 1;
}

int bgproc_flushprop(void)
{
	char path[PATH_MAX];
	FILE *f;
	int i;
	char *key;
	/* open the log */
	snprintf(path, PATH_MAX, "%s/prop", bgproc_getdir());
	f = fopen(path, "w");
	if (f == NULL)
		return 1;
	/* output each property */
	i = bgproc_iterstart();
	while ((key = bgproc_iternext(i))) {
		int d;
		char *s;
		switch (bgproc_gettype(key)) {
		case BGPROC_TYPE_INT:
			bgproc_get_int(key, &d);
			fprintf(f, "%s %d\n", key, d);
			break;
		case BGPROC_TYPE_STR:
			bgproc_get_str(key, &s);
			fprintf(f, "%s %s\n", key, s);
		}
		i++;
	}
	fclose(f);
	return 0;
}
