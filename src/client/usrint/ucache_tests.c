/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include "ucache.c"

#define LOG 0
#define tValA 0XAAAAAAAA
#define tValB 0XAAAAAAAAAAAAAAAA

/*  Test Cache Initialization   */
void test_1(void){
    FILE *file = fopen("ucache_test/1.log","w");
    /**/
    if(LOG){out = file;}
    else{out = stdout;}

    /********** Begin Test  **********/
    ucache_initialize();
    assert(ucache);
    /*  Check Global Data   */
    if(DBG){
        fprintf(out, "address of ucache ptr:\t0X%X\n", (unsigned int)&ucache);
        fprintf(out, "ucache ptr:\t\t0X%X\n", (unsigned int)ucache);
        fprintf(out, "ftbl ptr:\t\t0X%X\n", (unsigned int)&(ucache->ftbl));
        fprintf(out, "cache initialized...\n\n");
    }
    fprintf(stdout, "1:\tpass\n");
}

/*  Test Insertion, Lookup, Removal, then Lookup again on a single file */
void test_2(void){
    FILE *file = fopen("ucache_test/2.log","w");
    /**/
    if(LOG){out = file;}
    else{out = stdout;}

    /********** Begin Test  **********/
    ucache_initialize();
    assert(ucache);
    /*  Check Global Data   */
    if(DBG){
        fprintf(out, "address of ucache ptr:\t0X%X\n", (unsigned int)&ucache);
        fprintf(out, "ucache ptr:\t\t0X%X\n", (unsigned int)ucache);
        fprintf(out, "ftbl ptr:\t\t0X%X\n", (unsigned int)&(ucache->ftbl));
        fprintf(out, "cache initialized...\n\n");
    }
    assert((int)insert_file(tValA, tValB)!=NIL);
    assert((int)lookup_file(tValA, tValB, NULL, NULL, NULL, NULL)!=NIL);
    assert(remove_file(tValA, tValB)==1);
    assert((int)lookup_file(tValA, tValB, NULL, NULL, NULL, NULL)==NIL);
    fprintf(stdout, "2:\tpass\n");
}

/*  Test Insertion, Lookup, Removal, then Lookup again on multiple files    */
void test_3(void){
    /*  Setup Test  */
    FILE *file = fopen("ucache_test/3.log","w");
    /**/
    if(LOG){out = file;}
    else{out = stdout;}

    /********** Begin Test  **********/
    ucache_initialize();
    assert(ucache);

    /*  Check Global Data   */
    if(DBG){
        fprintf(out, "address of ucache ptr:\t0X%X\n", (unsigned int)&ucache);
        fprintf(out, "ucache ptr:\t\t0X%X\n", (unsigned int)ucache);
        fprintf(out, "ftbl ptr:\t\t0X%X\n", (unsigned int)&(ucache->ftbl));
        fprintf(out, "cache initialized...\n\n");
    }
    assert((int)insert_file(tValA, tValB)!=NIL);
    assert((int)insert_file(tValA, tValB+31)!=NIL);
    assert((int)insert_file(tValA, tValB-31)!=NIL);
    assert((int)lookup_file(tValA, tValB, NULL, NULL, NULL, NULL)!=NIL);
    assert((int)lookup_file(tValA, tValB+31, NULL, NULL, NULL, NULL)!=NIL);
    assert((int)lookup_file(tValA, tValB-31, NULL, NULL, NULL, NULL)!=NIL);
    assert(remove_file(tValA, tValB)==1);
    assert(remove_file(tValA, tValB+31)==1);
    assert(remove_file(tValA, tValB-31)==1);
    assert((int)lookup_file(tValA, tValB, NULL, NULL, NULL, NULL)==NIL);
    assert((int)lookup_file(tValA, tValB+31, NULL, NULL, NULL, NULL)==NIL);
    assert((int)lookup_file(tValA, tValB-31, NULL, NULL, NULL, NULL)==NIL);
    fprintf(stdout, "3:\tpass\n");
}

/*  Testing file entry lookup after removal */
void test_4(void){
    /*  Setup Test  */
    FILE *file = fopen("ucache_test/4.log","w");
    /**/
    if(LOG){out = file;}
    else{out = stdout;}

    /********** Begin Test  **********/
    ucache_initialize();
    assert((int)ucache!=NIL);

    /*  Check Global Data   */
    if(DBG){
        fprintf(out, "address of ucache ptr:\t0X%X\n", (unsigned int)&ucache);
        fprintf(out, "ucache ptr:\t\t0X%X\n", (unsigned int)ucache);
        fprintf(out, "ftbl ptr:\t\t0X%X\n", (unsigned int)&(ucache->ftbl));
        fprintf(out, "cache initialized...\n\n");
    }
    assert((int)insert_file(tValA, tValB)!=NIL);
    assert((int)insert_file(tValA, tValB+31)!=NIL);
    assert((int)insert_file(tValA, tValB-31)!=NIL);
    assert((int)lookup_file(tValA, tValB, NULL, NULL, NULL, NULL)!=NIL);
    assert((int)lookup_file(tValA, tValB+31, NULL, NULL, NULL, NULL)!=NIL);
    assert((int)lookup_file(tValA, tValB-31, NULL, NULL, NULL, NULL)!=NIL);
    assert(remove_file(tValA, tValB)==1);
    assert(remove_file(tValA, tValB+31)==1);
    assert((int)lookup_file(tValA, tValB+31, NULL, NULL, NULL, NULL)==NIL);
    assert(remove_file(tValA, tValB-31)==1);
    assert((int)lookup_file(tValA, tValB, NULL, NULL, NULL, NULL)==NIL);
    assert((int)lookup_file(tValA, tValB+31, NULL, NULL, NULL, NULL)==NIL);
    assert((int)lookup_file(tValA, tValB-31, NULL, NULL, NULL, NULL)==NIL);
    fprintf(stdout, "4:\tpass\n");
}

/*  Testing insertion, lookup, removal, and lookup of the max allowed file entries  */
void test_5(void){
    /*  Setup Test  */
    FILE *file = fopen("ucache_test/5.log","w");
    /**/
    if(LOG){out = file;}
    else{out = stdout;}

    /********** Begin Test  **********/
    ucache_initialize();
    assert(ucache);

    /*  Check Global Data   */
    if(DBG){
        fprintf(out, "address of ucache ptr:\t0X%X\n", (unsigned int)&ucache);
        fprintf(out, "ucache ptr:\t\t0X%X\n", (unsigned int)ucache);
        fprintf(out, "ftbl ptr:\t\t0X%X\n", (unsigned int)&(ucache->ftbl));
        fprintf(out, "cache initialized...\n\n");
    }

    int i;
    for(i=0; i<FILE_TABLE_ENTRY_COUNT; i++){
        assert((int)insert_file(tValA, (tValB+i))!=NIL);
    }
    for(i=0; i<FILE_TABLE_ENTRY_COUNT; i++){
        assert((int)lookup_file(tValA, (tValB+i), NULL, NULL, NULL, NULL)!=NIL);
    }
    for(i=0+10; i<FILE_TABLE_ENTRY_COUNT; i++){
        assert(remove_file(tValA, (tValB+i))==1);
    }
    for(i=0+10; i<FILE_TABLE_ENTRY_COUNT; i++){
        assert((int)lookup_file(tValA, (tValB+i), NULL, NULL, NULL, NULL)==NIL);
    }
    fprintf(stdout, "5:\tpass\n");
}

/*  Test Insertion, Lookup, Removal, then Lookup again on a single memory entry */
void test_6(void){
    FILE *file = fopen("ucache_test/6.log","w");
    /**/
    if(LOG){out = file;}
    else{out = stdout;}

    /********** Begin Test  **********/
    ucache_initialize();
    assert(ucache);
    /*  Check Global Data   */
    if(DBG){
        fprintf(out, "address of ucache ptr:\t0X%X\n", (unsigned int)&ucache);
        fprintf(out, "ucache ptr:\t\t0X%X\n", (unsigned int)ucache);
        fprintf(out, "ftbl ptr:\t\t0X%X\n", (unsigned int)&(ucache->ftbl));
        fprintf(out, "cache initialized...\n\n");
    }
    assert((int)insert_file(tValA, tValB)!=NIL);
    assert((int)lookup_file(tValA, tValB, NULL, NULL, NULL, 
        NULL)!=NIL);
    assert((int)insert_mem(lookup_file(tValA, tValB, NULL, 
        NULL, NULL, NULL), tValB)!=NIL);
    assert((int)lookup_mem(lookup_file(tValA, tValB, NULL, 
        NULL, NULL, NULL), tValB, NULL, NULL, NULL)!=NIL);
    assert(remove_mem(lookup_file(tValA, tValB, NULL, NULL, 
        NULL, NULL), tValB)==1);
    assert((int)lookup_mem(lookup_file(tValA, tValB, NULL, 
        NULL, NULL, NULL), tValB, NULL, NULL, NULL)==NIL);
    assert(remove_file(tValA, tValB)==1);
    assert((int)lookup_file(tValA, tValB, NULL, NULL, NULL, 
        NULL)==NIL);
    fprintf(stdout, "6:\tpass\n");
}

/*  Test max mem insertions, looking them up, removing them, and ensuring they're gone  */
void test_7(void){
    FILE *file = fopen("ucache_test/7.log","w");
    /**/
    if(LOG){out = file;}
    else{out = stdout;}

    /********** Begin Test  **********/
    ucache_initialize();
    assert(ucache);
    /*  Check Global Data   */
    if(DBG){
        fprintf(out, "address of ucache ptr:\t0X%X\n", (unsigned int)&ucache);
        fprintf(out, "ucache ptr:\t\t0X%X\n", (unsigned int)ucache);
        fprintf(out, "ftbl ptr:\t\t0X%X\n", (unsigned int)&(ucache->ftbl));
        fprintf(out, "cache initialized...\n\n");
    }

    assert((int)insert_file(tValA, tValB)!=NIL);
    struct mem_table_s  *mtbl= lookup_file(tValA, tValB, 
        NULL, NULL, NULL, NULL);
    assert((int)mtbl!=NIL);

    int i;
    for(i=0; i<MEM_TABLE_ENTRY_COUNT; i++){
        assert((int)insert_mem(mtbl, (tValB+i))!=NIL);
    }
    for(i=0; i<MEM_TABLE_ENTRY_COUNT; i++){
        assert((int)lookup_mem(mtbl, (tValB+i), NULL, NULL, NULL)!=NIL);
    }
    for(i=0; i<MEM_TABLE_ENTRY_COUNT; i++){
        assert(remove_mem(mtbl, (tValB+i))==1);
    }
    for(i=0; i<MEM_TABLE_ENTRY_COUNT; i++){
        assert((int)lookup_mem(mtbl, (tValB+i), NULL, NULL, NULL)==NIL);
    }
    assert(remove_file(tValA, tValB)==1);
    assert((int)lookup_file(tValA, tValB, NULL, NULL, NULL, 
        NULL)==NIL);
    fprintf(stdout, "7:\tpass\n");
}

/*  Test Eviction of Memory Entries - Requires #of blocks > MEM_TABLE_ENTRY_COUNT   */
void test_8(void){
    FILE *file = fopen("ucache_test/8.log","w");
    /**/
    if(LOG){out = file;}
    else{out = stdout;}

    /********** Begin Test  **********/
    ucache_initialize();
    assert(ucache);
    /*  Check Global Data   */
    if(DBG){
        fprintf(out, "address of ucache ptr:\t0X%X\n", (unsigned int)&ucache);
        fprintf(out, "ucache ptr:\t\t0X%X\n", (unsigned int)ucache);
        fprintf(out, "ftbl ptr:\t\t0X%X\n", (unsigned int)&(ucache->ftbl));
        fprintf(out, "cache initialized...\n\n");
    }

    assert((int)insert_file(tValA, tValB)!=NIL);
    struct mem_table_s  *mtbl= lookup_file(tValA, tValB, 
        NULL, NULL, NULL, NULL);
    assert((int)mtbl!=NIL);

    int i;
    for(i=0; i<MEM_TABLE_ENTRY_COUNT+1000; i++){
        if(DBG)printf("memory insertion # %d\n", i);
        assert((int)insert_mem(mtbl, (tValB+i))!=NIL);
    }
    assert(remove_file(tValA, tValB)==1);
    assert((int)lookup_file(tValA, tValB, NULL, NULL, NULL, 
        NULL)==NIL);
    fprintf(stdout, "8:\tpass\n");
}

/*  Insert file, then overfill with ments, then overfill with fents     */
void test_9(void){
    FILE *file = fopen("ucache_test/9.log","w");
    /**/
    if(LOG){out = file;}
    else{out = stdout;}

    /********** Begin Test  **********/
    ucache_initialize();
    assert(ucache);
    /*  Check Global Data   */
    if(DBG){
        fprintf(out, "address of ucache ptr:\t0X%X\n", (unsigned int)&ucache);
        fprintf(out, "ucache ptr:\t\t0X%X\n", (unsigned int)ucache);
        fprintf(out, "ftbl ptr:\t\t0X%X\n", (unsigned int)&(ucache->ftbl));
        fprintf(out, "cache initialized...\n\n");
    }

    assert((int)insert_file(tValA, tValB)!=NIL);
    struct mem_table_s  *mtbl= lookup_file(tValA, tValB, 
        NULL, NULL, NULL, NULL);
    assert((int)mtbl!=NIL);

    int i;
    for(i=0; i<MEM_TABLE_ENTRY_COUNT+100; i++){
        assert((int)insert_mem(mtbl, (tValB+i))!=NIL);
    }
    for(i=0; i<FILE_TABLE_ENTRY_COUNT+100; i++){
        assert((int)insert_file(tValA, (tValB+i+1))!=NIL);
    }
    assert(remove_file(tValA, tValB)==1);
    assert((int)lookup_file(tValA, tValB, NULL, NULL, NULL, 
        NULL)==NIL);
    fprintf(stdout, "9:\tpass\n");
}

/*  Test many mem entries, looking them, removing them up and ensuring they're gone */
void test_10(void){
    FILE *file = fopen("ucache_test/10.log","w");
    /**/
    if(LOG){out = file;}
    else{out = stdout;}

    /********** Begin Test  **********/
    ucache_initialize();
    assert(ucache);
    /*  Check Global Data   */
    if(DBG){
        fprintf(out, "address of ucache ptr:\t0X%X\n", (unsigned int)&ucache);
        fprintf(out, "ucache ptr:\t\t0X%X\n", (unsigned int)ucache);
        fprintf(out, "ftbl ptr:\t\t0X%X\n", (unsigned int)&(ucache->ftbl));
        fprintf(out, "cache initialized...\n\n");
    }

    /*  Insert three files  */
    assert((int)insert_file(tValA, tValB)!=NIL);
    assert((int)insert_file(tValA, tValB+1)!=NIL);
    assert((int)insert_file(tValA, tValB+2)!=NIL);

    struct mem_table_s  *mtbl1= lookup_file(tValA, tValB, 
        NULL, NULL, NULL, NULL);
    assert((int)mtbl1!=NIL);
    struct mem_table_s  *mtbl2= lookup_file(tValA, tValB+1, 
        NULL, NULL, NULL, NULL);
    assert((int)mtbl2!=NIL);
    struct mem_table_s  *mtbl3= lookup_file(tValA, tValB+2, 
        NULL, NULL, NULL, NULL);
    assert((int)mtbl3!=NIL);

    /*  Insert (BLOCKS_IN_CACHE-1) memory entries   */
    int i;
    for(i=0; i<(BLOCKS_IN_CACHE-1); i++){
        assert((int)insert_mem(mtbl2, (tValB+i))!=NIL);
    }
    assert((int)insert_mem(mtbl1, (tValB+i))!=NIL);
    assert((int)insert_mem(mtbl3, (tValB+i))!=NIL);
    fprintf(stdout, "10:\tpass\n");
}

/*  Test many file insertions evicting fents when necessary */
void test_11(void)
{
    FILE *file = fopen("ucache_test/11.log","w");
    /**/
    if(LOG){out = file;}
    else{out = stdout;}
    /********** Begin Test  **********/
    ucache_initialize();
    assert(ucache);
    /*  Check Global Data   */
    if(DBG){
        fprintf(out, "address of ucache ptr:\t0X%X\n", (unsigned int)&ucache);
        fprintf(out, "ucache ptr:\t\t0X%X\n", (unsigned int)ucache);
        fprintf(out, "ftbl ptr:\t\t0X%X\n", (unsigned int)&(ucache->ftbl));
        fprintf(out, "cache initialized...\n\n");
    }
    /*  Insert three files  */
    int i;
    for(i = 0; i<FILE_TABLE_ENTRY_COUNT+1; i++)
    {
        assert((int)insert_file(tValA, tValB+i)!=NIL);
    }
    fprintf(stdout, "11:\tpass\n");
}

int run_rmshmem()
{
    popen("./rmshmem 0X3D010002", "r");
}

int main(int argc, char **argv)
{
    fprintf(stdout, "Testing...\n");
    if(argc==2){
        if(atoi(argv[1])==0)    /*  Run all tests   */
        {
            test_1();
            test_2();
            test_3();
            test_4();
            test_5();
            test_6();
            if(BLOCKS_IN_CACHE>MEM_TABLE_ENTRY_COUNT)
            {
                test_7();
            }
            test_8();
            if(BLOCKS_IN_CACHE>MEM_TABLE_ENTRY_COUNT)
            {
                test_9();
            }
            test_10();
            test_11();
        }
        else if(atoi(argv[1])==1)test_1();
        else if(atoi(argv[1])==2)test_2();
        else if(atoi(argv[1])==3)test_3();
        else if(atoi(argv[1])==4)test_4();
        else if(atoi(argv[1])==5)test_5();
        else if(atoi(argv[1])==6)test_6();
        else if(atoi(argv[1])==7)test_7();
        else if(atoi(argv[1])==8)test_8();
        else if(atoi(argv[1])==9)test_9();
        else if(atoi(argv[1])==10)test_10();
        else if(atoi(argv[1])==11)test_11();
    }
    else{printf("ERROR:\tenter an integer >=0\n");}
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

