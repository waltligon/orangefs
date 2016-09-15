

/* force malloc to not be redefined for mem_analysis */
#define PVFS_MALLOC_REDEF_OVERRIDE 1

struct clause
{
    int type;
    int value;
};

struct entry
{
    int  op;
    char *afilename;
    char *filename;
    int  aline;
    int  fline;
    int  size;
    int  addr;
    int  realaddr;
    int  align;
};

extern int line;
extern int col;

