
#ifdef REDEF_STD_STREAMS
#ifdef stdin
#undef stdin
#endif

#define stdin (&pvfs_stdin)   /* this is wrong!!! */

#ifdef stdout
#undef stdout
#endif

#define stdout (&pvfs_stdout)   /* this is wrong!!! */

#ifdef stderr
#undef stderr
#endif

#define stderr (&pvfs_stderr)   /* this is wrong!!! */
#endif

#ifdef FILE
#undef FILE
#endif

#define FILE pvfs_descriptor
