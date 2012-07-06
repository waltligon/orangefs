
#include <../quicklist/quicklist.h>

extern int line;
extern FILE *code;
extern FILE *code2;
extern FILE *header;
extern const char *in_file_name;

void yyerror(char *);
void *emalloc(size_t size);
char *estrdup(const char *oldstring);

typedef struct list_item_s
{
    char *name;
    struct qlist_head link;
} list_item_t;
