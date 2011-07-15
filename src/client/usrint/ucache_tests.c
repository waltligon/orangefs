#include "ucache.c"
//#include <assert.h>

#define LOG 0

/*	Test Cache Initialization	*/
void test_1(void){
	FILE *file = fopen("ucache_test/1.log","w");
	/**/
	if(LOG){out = file;}
	else{out = stdout;}

	/**********	Begin Test	**********/
	ucache_initialize();
	assert(ucache);
	/*	Check Global Data	*/
	if(DBG){
		fprintf(out, "address of ucache ptr:\t0x%x\n", (unsigned int)&ucache);
		fprintf(out, "ucache ptr:\t\t0x%x\n", (unsigned int)ucache);
		fprintf(out, "ftbl ptr:\t\t0x%x\n", (unsigned int)&(ucache->ftbl));
		fprintf(out, "cache initialized...\n\n");
	}
	fprintf(stdout, "Test1:\tPASS!\n");
}

/*	Test Insertion, Lookup, Removal, then Lookup again on a single file	*/
void test_2(void){
	FILE *file = fopen("ucache_test/2.log","w");
	/**/
	if(LOG){out = file;}
	else{out = stdout;}

	/**********	Begin Test	**********/
	ucache_initialize();
	assert(ucache);
	/*	Check Global Data	*/
	if(DBG){
		fprintf(out, "address of ucache ptr:\t0x%x\n", (unsigned int)&ucache);
		fprintf(out, "ucache ptr:\t\t0x%x\n", (unsigned int)ucache);
		fprintf(out, "ftbl ptr:\t\t0x%x\n", (unsigned int)&(ucache->ftbl));
		fprintf(out, "cache initialized...\n\n");
	}
	assert((int)insert_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA)!=NIL);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL, NULL)!=NIL);
	assert(remove_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA)==1);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL, NULL)==NIL);
	fprintf(stdout, "Test2:\tPASS!\n");
}

void test_3(void){
	/*	Setup Test	*/
	FILE *file = fopen("ucache_test/3.log","w");
	/**/
	if(LOG){out = file;}
	else{out = stdout;}

	/**********	Begin Test	**********/
	ucache_initialize();
	assert(ucache);

	/*	Check Global Data	*/
	if(DBG){
		fprintf(out, "address of ucache ptr:\t0x%x\n", (unsigned int)&ucache);
		fprintf(out, "ucache ptr:\t\t0x%x\n", (unsigned int)ucache);
		fprintf(out, "ftbl ptr:\t\t0x%x\n", (unsigned int)&(ucache->ftbl));
		fprintf(out, "cache initialized...\n\n");
	}
	assert((int)insert_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA)!=NIL);
	assert((int)insert_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAC9)!=NIL);
	assert((int)insert_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAA8B)!=NIL);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL, NULL)!=NIL);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAC9, NULL, NULL, NULL, NULL)!=NIL);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAA8B, NULL, NULL, NULL, NULL)!=NIL);
	assert(remove_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA)==1);
	assert(remove_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAC9)==1);
	assert(remove_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAA8B)==1);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL, NULL)==NIL);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAC9, NULL, NULL, NULL, NULL)==NIL);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAA8B, NULL, NULL, NULL, NULL)==NIL);
	fprintf(stdout, "Test3:\tPASS!\n");
}

/*	Slightly Different than test 3	*/
void test_4(void){
	/*	Setup Test	*/
	FILE *file = fopen("ucache_test/4.log","w");
	/**/
	if(LOG){out = file;}
	else{out = stdout;}

	/**********	Begin Test	**********/
	ucache_initialize();
	assert(ucache);

	/*	Check Global Data	*/
	if(DBG){
		fprintf(out, "address of ucache ptr:\t0x%x\n", (unsigned int)&ucache);
		fprintf(out, "ucache ptr:\t\t0x%x\n", (unsigned int)ucache);
		fprintf(out, "ftbl ptr:\t\t0x%x\n", (unsigned int)&(ucache->ftbl));
		fprintf(out, "cache initialized...\n\n");
	}
	assert((int)insert_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA)!=NIL);
	assert((int)insert_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAC9)!=NIL);
	assert((int)insert_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAA8B)!=NIL);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL, NULL)!=NIL);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAC9, NULL, NULL, NULL, NULL)!=NIL);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAA8B, NULL, NULL, NULL, NULL)!=NIL);
	assert(remove_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA)==1);
	assert(remove_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAC9)==1);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAC9, NULL, NULL, NULL, NULL)==NIL);
	assert(remove_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAA8B)==1);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL, NULL)==NIL);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAC9, NULL, NULL, NULL, NULL)==NIL);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAA8B, NULL, NULL, NULL, NULL)==NIL);
	fprintf(stdout, "Test4:\tPASS!\n");
}

void test_5(void){
	/*	Setup Test	*/
	FILE *file = fopen("ucache_test/5.log","w");
	/**/
	if(LOG){out = file;}
	else{out = stdout;}

	/**********	Begin Test	**********/
	ucache_initialize();
	assert(ucache);

	/*	Check Global Data	*/
	if(DBG){
		fprintf(out, "address of ucache ptr:\t0x%x\n", (unsigned int)&ucache);
		fprintf(out, "ucache ptr:\t\t0x%x\n", (unsigned int)ucache);
		fprintf(out, "ftbl ptr:\t\t0x%x\n", (unsigned int)&(ucache->ftbl));
		fprintf(out, "cache initialized...\n\n");
	}

	int i;
	uint64_t current = 0XAAAAAAAAAAAAAAAA;
	for(i=0; i<FILE_TABLE_ENTRY_COUNT; i++){
		assert((int)insert_file(0XAAAAAAAA, (current+i))!=NIL);
	}
	for(i=0; i<FILE_TABLE_ENTRY_COUNT; i++){
		assert((int)lookup_file(0XAAAAAAAA, (current+i), NULL, NULL, NULL, NULL)!=NIL);
	}
	for(i=0+10; i<FILE_TABLE_ENTRY_COUNT; i++){
		assert(remove_file(0XAAAAAAAA, (current+i))==1);
	}
	for(i=0+10; i<FILE_TABLE_ENTRY_COUNT; i++){
		assert((int)lookup_file(0XAAAAAAAA, (current+i), NULL, NULL, NULL, NULL)==NIL);
	}
	fprintf(stdout, "Test5:\tPASS!\n");
}

/*	Test Insertion, Lookup, Removal, then Lookup again on a single memory entry	*/
void test_6(void){
	FILE *file = fopen("ucache_test/6.log","w");
	/**/
	if(LOG){out = file;}
	else{out = stdout;}

	/**********	Begin Test	**********/
	ucache_initialize();
	assert(ucache);
	/*	Check Global Data	*/
	if(DBG){
		fprintf(out, "address of ucache ptr:\t0x%x\n", (unsigned int)&ucache);
		fprintf(out, "ucache ptr:\t\t0x%x\n", (unsigned int)ucache);
		fprintf(out, "ftbl ptr:\t\t0x%x\n", (unsigned int)&(ucache->ftbl));
		fprintf(out, "cache initialized...\n\n");
	}
	assert((int)insert_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA)!=NIL);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL, 
		NULL)!=NIL);
	assert((int)insert_mem(lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, 
		NULL, NULL, NULL), 0XAAAAAAAAAAAAAAAA)!=NIL);
	assert((int)lookup_mem(lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, 
		NULL, NULL, NULL), 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL)!=NIL);
	assert(remove_mem(lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, 
		NULL, NULL), 0XAAAAAAAAAAAAAAAA)==1);
	assert((int)lookup_mem(lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, 
		NULL, NULL, NULL), 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL)==NIL);
	assert(remove_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA)==1);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL, 
		NULL)==NIL);
	fprintf(stdout, "Test6:\tPASS!\n");
}

/*	Test many mem entries, looking them, removing them up and ensuring they're gone	*/
void test_7(void){
	FILE *file = fopen("ucache_test/7.log","w");
	/**/
	if(LOG){out = file;}
	else{out = stdout;}

	/**********	Begin Test	**********/
	ucache_initialize();
	assert(ucache);
	/*	Check Global Data	*/
	if(DBG){
		fprintf(out, "address of ucache ptr:\t0x%x\n", (unsigned int)&ucache);
		fprintf(out, "ucache ptr:\t\t0x%x\n", (unsigned int)ucache);
		fprintf(out, "ftbl ptr:\t\t0x%x\n", (unsigned int)&(ucache->ftbl));
		fprintf(out, "cache initialized...\n\n");
	}

	assert((int)insert_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA)!=NIL);
	struct mem_table_s  *mtbl= lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, 
		NULL, NULL, NULL, NULL);
	assert((int)mtbl!=NIL);

	int i;
	uint64_t current = 0XAAAAAAAAAAAAAAAA;
	for(i=0; i<MEM_TABLE_ENTRY_COUNT; i++){
		assert((int)insert_mem(mtbl, (current+i))!=NIL);
	}
	for(i=0; i<MEM_TABLE_ENTRY_COUNT; i++){
		assert((int)lookup_mem(mtbl, (current+i), NULL, NULL, NULL)!=NIL);
	}
	for(i=0; i<MEM_TABLE_ENTRY_COUNT; i++){
		assert(remove_mem(mtbl, (current+i))==1);
	}
	for(i=0; i<MEM_TABLE_ENTRY_COUNT; i++){
		assert((int)lookup_mem(mtbl, (current+i), NULL, NULL, NULL)==NIL);
	}
	assert(remove_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA)==1);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL, 
		NULL)==NIL);
	fprintf(stdout, "Test7:\tPASS!\n");
}

/*	Test Eviction of Memory Entries - Requires #of blocks > MEM_TABLE_ENTRY_COUNT	*/
void test_8(void){
	FILE *file = fopen("ucache_test/8.log","w");
	/**/
	if(LOG){out = file;}
	else{out = stdout;}

	/**********	Begin Test	**********/
	ucache_initialize();
	assert(ucache);
	/*	Check Global Data	*/
	if(DBG){
		fprintf(out, "address of ucache ptr:\t0x%x\n", (unsigned int)&ucache);
		fprintf(out, "ucache ptr:\t\t0x%x\n", (unsigned int)ucache);
		fprintf(out, "ftbl ptr:\t\t0x%x\n", (unsigned int)&(ucache->ftbl));
		fprintf(out, "cache initialized...\n\n");
	}

	assert((int)insert_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA)!=NIL);
	struct mem_table_s  *mtbl= lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, 
		NULL, NULL, NULL, NULL);
	assert((int)mtbl!=NIL);

	int i;
	uint64_t current = 0XAAAAAAAAAAAAAAAA;
	for(i=0; i<MEM_TABLE_ENTRY_COUNT+1000; i++){
		printf("i=%d\n", i);
		assert((int)insert_mem(mtbl, (current+i))!=NIL);
	}
	assert(remove_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA)==1);
	assert((int)lookup_file(0XAAAAAAAA, 0XAAAAAAAAAAAAAAAA, NULL, NULL, NULL, 
		NULL)==NIL);
	fprintf(stdout, "Test8:\tPASS!\n");
}

int main(int argc, char **argv){
	if(argc==2){
		if(argv[1][0]=='0'){	/*	Run all tests	*/
			test_1();
			test_2();
			test_3();
			test_4();
			test_5();
			test_6();
			test_7();
			test_8();
		}
		else if(argv[1][0]=='1')test_1();
		else if(argv[1][0]=='2')test_2();
		else if(argv[1][0]=='3')test_3();
		else if(argv[1][0]=='4')test_4();
		else if(argv[1][0]=='5')test_5();
		else if(argv[1][0]=='6')test_6();
		else if(argv[1][0]=='7')test_7();
		else if(argv[1][0]=='8')test_8();
	}
	else{printf("ERROR:\tenter an integer >=0\n");}
	return 0;
}
