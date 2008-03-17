/*
 * Joe insley reported slow open behavior.  Let's find out what is going on
 * when an application opens a bunch of files 
 *
 * This can be further extented to test open behavior across several file
 * systems ( pvfs, pvfs2, nfs, testfs )
 *
 * The timing and command-line parsing were so useful that this was further
 * extended to test resize operations
 *
 * And while the default (and most useful) mode is to compare collective
 * open/create/resize, it is sometimes instructive to compare with independent
 * access
 *
 * usage:  -d /path/to/directory -n number_of_files [-O] [-R] [-i]
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>

#include <mpi.h>

#ifndef PATH_MAX
#define PATH_MAX FILENAME_MAX
#endif



/* we would like to have repeatable results, so if any of the operations need
 * pseudorandom vaules, they can pull from this fixed array and get the same
 * values for each run. 
 */
#define NR_RANDOMS 500

int random_sizes[NR_RANDOMS] = {202517573, 
1996480800, 1328239223, 627968542, 122405398, 1770997087, 
988468815, 890726602, 242661171, 700009448, 1837347329, 
40912155, 1841839745, 328922176, 1313853687, 494151226, 
221004789, 1581243751, 1278729674, 1823702927, 1847627795, 
1046519100, 1409803016, 1967359494, 420743704, 1159369000, 
1527373575, 349553402, 242655795, 700237058, 1372085655, 
445173368, 549234210, 552841231, 1073141910, 671639608, 
176354670, 2061610726, 1562366210, 419015841, 614136526, 
1252229892, 459927997, 308492624, 1581152068, 1773781684, 
802643850, 1802156858, 1207541787, 2081373524, 1478376137, 
907685934, 980408977, 740695505, 727561780, 1401152681, 
1900064505, 107451707, 1750706084, 2142720300, 807688765, 
975308091, 440410021, 1356922976, 1528149322, 1513551931, 
2028562584, 1704503992, 1427679009, 1443445147, 2123519834, 
2041815536, 548191391, 435964183, 202824512, 2129343459, 
62262219, 1005468362, 1784016669, 1269804006, 939358238, 
1114909158, 30006292, 1919767215, 1855604663, 757568072, 
1173436249, 1608185521, 865019779, 776658685, 1603422173, 
1672708545, 1751966776, 2043832194, 882147873, 1132632451, 
1409900478, 763226809, 689652795, 690095839, 59188308, 
665688981, 584427727, 607379699, 1101653164, 787252239, 
589239511, 1163915383, 1792720601, 225772532, 286235741, 
584595192, 1340681691, 316242033, 356878759, 1048802706, 
1073810105, 1530315008, 509504579, 1938829885, 159490045, 
2112926753, 1464054782, 1911456822, 2009275299, 198719007, 
896605625, 1271692129, 961945816, 1586258420, 1961787969, 
1021134125, 104463754, 398732048, 1628513824, 1206116918, 
1185984288, 70269687, 222548654, 831221241, 296042220, 
508784395, 1415816433, 1636723911, 825026429, 1772695193, 
538042969, 1898836534, 1155526553, 1047547549, 1690182771, 
1315016599, 1012990654, 1006753905, 1078989773, 874782305, 
1205472912, 1975595398, 2146474435, 19935081, 1414370170, 
1960778756, 1041069206, 1518833924, 212027156, 522099382, 
577467195, 1398011444, 592369070, 800015849, 81749038, 
888411290, 1308800244, 1497565471, 377651553, 2133826673, 
1122777016, 915694522, 1885179560, 130819922, 1963242071, 
1427878683, 1445836521, 828749077, 287148941, 377342646, 
1703531383, 1492621853, 205454396, 1702522170, 1512556934, 
1619824566, 1515817278, 406142492, 991174843, 1727844434, 
928241875, 1568642038, 978372231, 1520610945, 221174239, 
1060121269, 261538587, 1529974483, 410203092, 639190140, 
1516317509, 1532980109, 1554884662, 1254013421, 1663800031, 
1370643086, 534408456, 962152904, 51908515, 821557397, 
1339495550, 1755439898, 166695603, 1544949946, 1310478420, 
1679252537, 1017290864, 678812050, 2085395030, 2008465707, 
259172837, 866153257, 1429624097, 1237545068, 239280554, 
1650798336, 150182689, 500819141, 1033289172, 560385781, 
1140009281, 402123033, 2093365890, 547410295, 1656136454, 
1609682273, 1918053381, 43061262, 424351529, 1969961897, 
864618660, 1763847079, 1577918147, 1031314263, 1161313377, 
740912920, 563083152, 31120594, 1419724970, 500994534, 
2039586301, 1678897807, 1367147791, 1321726751, 768959227, 
1606428345, 825041439, 919141916, 2107247486, 1858330611, 
1479527698, 1099773119, 112969996, 1425409940, 1647183415, 
1769106450, 887608566, 1417753148, 1812167713, 1311960095, 
1240231397, 529302725, 928323527, 670665897, 1560616988, 
2089636904, 1411578817, 2123700140, 2120757498, 683820139, 
477211027, 2012860152, 215234299, 1844358818, 1187103255, 
984193526, 1303303516, 2012144694, 1903335443, 1263067354, 
1722991658, 1235379493, 215356826, 1835961654, 513305785, 
1862540241, 1457584457, 1400914351, 1132809741, 1122268522, 
565390799, 225557491, 1651571247, 1493714326, 896223388, 
1064704587, 1435867582, 160318557, 1040921079, 1409141433, 
844138696, 1518132106, 1274517937, 1059372995, 1215007277, 
314137544, 2043566522, 370827145, 178798590, 1799418317, 
1633894499, 1901790248, 887314162, 1849251325, 1590268255, 
1400619947, 1564307918, 900369064, 654050651, 549634012, 
2022637586, 1219441450, 775191503, 1526725185, 565672128, 
1671414891, 443946124, 2001539710, 1831733448, 1484867203, 
1263197495, 528388496, 855515662, 390231784, 1587761492, 
2070522939, 704369328, 1483844366, 293866436, 883167919, 
1135779035, 1927760935, 637474519, 2023093197, 1629528613, 
80259126, 1276229496, 1046352883, 980628190, 1930280147, 
1595986895, 855782128, 1002237949, 223694750, 235023665, 
1567910077, 1895109641, 678969789, 1421966140, 1579359441, 
16353345, 537679987, 2107747938, 871869007, 927911772, 
1548025782, 794908298, 1632281100, 884386500, 1088774734, 
367965371, 2020165535, 869052021, 1005439891, 1895775084, 
351096986, 1085699017, 1024520932, 1397449870, 2066327208, 
807317432, 845953117, 774625688, 1809555381, 1069647868, 
1009649354, 1229981811, 817273861, 1688619143, 504464303, 
249149655, 1704972488, 1042144290, 209413945, 429357847, 
1970056062, 1757439727, 1224266145, 1454853515, 494342579, 
165557231, 1822818886, 367024466, 1034609253, 680775129, 
115315902, 1385706239, 1766474147, 1139836834, 635672461, 
1685317707, 1947154266, 1481625579, 312459747, 1609226000, 
403789799, 1322109101, 691724163, 1221063660, 863244597, 
1196188466, 1470213315, 420733437, 90849108, 1679627260, 
850091285, 2060905171, 1289583339, 2074357430, 1368275038, 
1783925918, 92431014, 1043610276, 3466736, 1127040267, 
1724385406, 118782638, 365262858, 1343375905, 1258619473, 
1000935320, 881209964, 1058290091, 335077251, 1193669711, 
520032443, 738867050, 368295165, 1211756606, 1959930710, 
1231539762, 260461424, 1282660378, 1652273199, 351310533, 
814803990, 354880836, 264732056, 2104387330, 281754619, 
1633007094, 1740829600, 374185633, 529133722, 1744296337, 
1501225900, 106035480, 1863078975, 1866488758, 1449411385, 
974214800, 719940430, 183137701, 2032504892, 1055017681, 
1376807413, 405053687, 1793884731, 1745102578, 1616810294, 
1606331794, 829158692, 1877271718, 741508524};

extern char *optarg;
int opt_nfiles;
char opt_basedir[PATH_MAX];
int opt_do_open=0;
int opt_do_resize=0;
int opt_do_indep=0;

void usage(char *name);
int parse_args(int argc, char **argv);
void handle_error(int errcode, char *str);
int test_opens(int nfiles, char * test_dir, MPI_Info info);
int test_resize(int rank, int iterations, char * test_dir, MPI_Info info);

void usage(char *name)
{
	fprintf(stderr, "usage: %s -d /path/to/directory -n #_of_files [TEST] [MODE]\n", name);
	fprintf(stderr, "   where TEST is one of:\n"
			"     -O       test file open times\n"
			"     -R       test file resize times\n"
			"   and MODE is one of:\n"
			"     -i       independent operations\n"
			"     -c       collective operations (default)\n");

		exit(-1);
}
int parse_args(int argc, char **argv)
{
	int c;
	while ( (c = getopt(argc, argv, "d:n:ORic")) != -1 ) {
		switch (c) {
			case 'd':
				strncpy(opt_basedir, optarg, PATH_MAX);
				break;
			case 'n':
				opt_nfiles = atoi(optarg);
				break;
			case 'O':
				opt_do_open = 1;
				break;
			case 'R':
				opt_do_resize = 1;
				break;
			case 'i':
				opt_do_indep = 1;
				break;
			case '?':
			case ':':
			default:
				usage(argv[0]);
		}
	}
	if ( (opt_do_open == 0) && (opt_do_resize == 0) ) {
		usage(argv[0]);
	}
	return 0;
}

void handle_error(int errcode, char *str)
{
        char msg[MPI_MAX_ERROR_STRING];
        int resultlen;
        MPI_Error_string(errcode, msg, &resultlen);
        fprintf(stderr, "%s: %s\n", str, msg);
        MPI_Abort(MPI_COMM_WORLD, 1);
}


int main(int argc, char **argv)
{
	int rank, nprocs;
	MPI_Info info;
	double test_start, test_end;
	double test_time, total_time;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

	parse_args(argc, argv);

	/* provide hints if you want  */
	info = MPI_INFO_NULL;

	test_start = MPI_Wtime();
	if (opt_do_open)
		test_opens(opt_nfiles, opt_basedir, info);
	else if (opt_do_resize)
		test_resize(rank, opt_nfiles, opt_basedir, info);

	test_end = MPI_Wtime();
	test_time = test_end - test_start;
	MPI_Allreduce(&test_time, &total_time, 1, 
			MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
	MPI_Finalize();

	if (rank == 0) {
		printf("%d procs ", nprocs);
		if (opt_do_open) {
			printf("%f seconds to open %d files: %f secs/open: %s\n", 
				total_time, opt_nfiles, 
				(total_time)/opt_nfiles, 
				(opt_do_indep? "independent" : "collective"));
		} else if (opt_do_resize) {
			printf("%f seconds to perform %d resize ops: %f secs/operation: %s\n", 
				total_time, opt_nfiles, 
				(total_time)/opt_nfiles,
				(opt_do_indep? "independent" : "collective"));
		}
			
	}
	return 0;
}

/* in directory 'test_dir', open and immediately close 'nfiles' files */
/* a possible variant: open all the files first, then close */
/* also test MPI_File_open behavior when there are a ton of files */
int test_opens(int nfiles, char * test_dir, MPI_Info info)
{
	int i;
	char test_file[PATH_MAX];
	MPI_File fh;
	MPI_Comm comm = MPI_COMM_WORLD;
	int errcode;

	if (opt_do_indep) 
		comm = MPI_COMM_SELF;

	for (i=0; i<nfiles; i++) {
		snprintf(test_file, PATH_MAX, "%s/testfile.%d", test_dir, i);
		errcode = MPI_File_open(comm, test_file, 
				MPI_MODE_CREATE|MPI_MODE_RDWR, info, &fh);
		if (errcode != MPI_SUCCESS) {
			handle_error(errcode, "MPI_File_open");
		}
		errcode = MPI_File_close(&fh);
		if (errcode != MPI_SUCCESS) {
			handle_error(errcode, "MPI_File_close");
		}

	}
	/* since handle_error aborts, if we got here we are a-ok */
	return 0;
}

/* stuff these into separate object files. have a structure that provides a
 * test() and result() function and a .time member */

/* inside directory 'test_dir', create a file and resize it to 'iterations'
 * different sizes */

int test_resize(int rank, int iterations, char * test_dir, MPI_Info info)
{
	int i;
	char test_file[PATH_MAX];
	MPI_File fh;
	int errcode;
	MPI_Offset size;
	MPI_Comm comm = MPI_COMM_WORLD;

	if (opt_do_indep) 
		comm = MPI_COMM_SELF;

	snprintf(test_file, PATH_MAX, "%s/testfile", test_dir);
	errcode = MPI_File_open(comm, test_file, 
			MPI_MODE_CREATE|MPI_MODE_RDWR, info, &fh);
	if (errcode != MPI_SUCCESS) {
		handle_error(errcode, "MPI_File_open");
	}

	for(i=0; i<iterations; i++) {
		size = random_sizes[i % NR_RANDOMS];
		errcode = MPI_File_set_size(fh, size);
		if (errcode != MPI_SUCCESS) {
			handle_error(errcode, "MPI_File_set_size");
		}
	}

	errcode = MPI_File_close(&fh);
	if (errcode != MPI_SUCCESS) {
		handle_error(errcode, "MPI_File_close");
	}
	return 0;
}
