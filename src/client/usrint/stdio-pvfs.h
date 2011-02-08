
#ifdef stdin
#undef stdin
#endif

#define stdin (&pvfs_stdin)

#ifdef stdout
#undef stdout
#endif

#define stdout (&pvfs_stdout)

#ifdef stderr
#undef stderr
#endif

#define stderr (&pvfs_stderr)

#ifdef FILE
#undef FILE
#endif

#define FILE pvfs_descriptor
