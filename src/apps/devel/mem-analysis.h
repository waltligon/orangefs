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

