#define _GNU_SOURCE
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DATATYPE MPI_BYTE

#define MPI_Sleep(c, n) MPI_Barrier(c); sleep(n); MPI_Barrier(c);

#define DEFAULT_ELEMENTS (10 * 1024 * 1024)
#define DEFAULT_FILENAME "pvfs2:///pvfs2/foobar"
#define DEFAULT_ITERATIONS 10

#define BOOL(n) (((n) == 0) ? "false" : "true")

typedef struct {
	char* name;
	void (*function) (void);
} test;

/* Stuff needed by getopt() */
extern char *optarg;
extern int optind, opterr, optopt;

int rank;
int size;
int datatype_size;

/* Options */
unsigned int elements = DEFAULT_ELEMENTS;
char* filename = DEFAULT_FILENAME;
unsigned int iterations = DEFAULT_ITERATIONS;

int mode = MPI_MODE_RDWR | MPI_MODE_CREATE;
char* buffer = NULL;
MPI_Info info;
MPI_Datatype datatype;

void Test_level0 (void);
void Test_level1 (void);
void Test_level2 (void);
void Test_level3 (void);

void usage (char**);
void get_args (int, char**);

void allocate_buffer (int);
void free_buffer (void);

test tests[] = {
	{ "level0", Test_level0 },
	{ "level1", Test_level1 },
	{ "level2", Test_level2 },
	{ "level3", Test_level3 }
};

/* Tests must return void and take no arguments. */
/* Level 0: non-collective, contiguous */
void Test_level0 ()
{
	MPI_File fh;
	MPI_Status status;
	int i;

	allocate_buffer(elements);

	MPI_File_open(MPI_COMM_WORLD, filename, mode, info, &fh);
	MPI_File_set_view(fh, 0, DATATYPE, datatype, "native", info);
	MPI_File_seek(fh, 0, MPI_SEEK_SET);

	for (i = 0; i < iterations; ++i)
	{
		MPI_File_write(fh, buffer, elements, DATATYPE, &status);
	}

	MPI_Sleep(MPI_COMM_WORLD, 1);
	MPI_File_seek(fh, 0, MPI_SEEK_SET);

	for (i = 0; i < iterations; ++i)
	{
		MPI_File_read(fh, buffer, elements, DATATYPE, &status);
	}

	MPI_File_close(&fh);

	free_buffer();
}

/* Level 1: collective, contiguous */
void Test_level1 ()
{
	MPI_File fh;
	MPI_Status status;
	int i;

	allocate_buffer(elements);

	MPI_File_open(MPI_COMM_WORLD, filename, mode, info, &fh);
	MPI_File_set_view(fh, 0, DATATYPE, datatype, "native", info);
	MPI_File_seek(fh, 0, MPI_SEEK_SET);

	for (i = 0; i < iterations; ++i)
	{
		MPI_File_write_all(fh, buffer, elements, DATATYPE, &status);
	}

	MPI_Sleep(MPI_COMM_WORLD, 1);
	MPI_File_seek(fh, 0, MPI_SEEK_SET);

	for (i = 0; i < iterations; ++i)
	{
		MPI_File_read_all(fh, buffer, elements, DATATYPE, &status);
	}

	MPI_File_close(&fh);

	free_buffer();
}

/* Level 2: non-collective, non-contiguous */
void Test_level2 ()
{
	MPI_File fh;
	MPI_Status status;

	allocate_buffer(iterations * elements);

	MPI_File_open(MPI_COMM_WORLD, filename, mode, info, &fh);
	MPI_File_set_view(fh, 0, DATATYPE, datatype, "native", info);
	MPI_File_seek(fh, 0, MPI_SEEK_SET);

	MPI_File_write(fh, buffer, iterations * elements, DATATYPE, &status);

	MPI_Sleep(MPI_COMM_WORLD, 1);
	MPI_File_seek(fh, 0, MPI_SEEK_SET);

	MPI_File_read(fh, buffer, iterations * elements, DATATYPE, &status);

	MPI_File_close(&fh);

	free_buffer();
}

/* Level 3: collective, non-contiguous */
void Test_level3 ()
{
	MPI_File fh;
	MPI_Status status;

	allocate_buffer(iterations * elements);

	MPI_File_open(MPI_COMM_WORLD, filename, mode, info, &fh);
	MPI_File_set_view(fh, 0, DATATYPE, datatype, "native", info);
	MPI_File_seek(fh, 0, MPI_SEEK_SET);

	MPI_File_write_all(fh, buffer, iterations * elements, DATATYPE, &status);

	MPI_Sleep(MPI_COMM_WORLD, 1);
	MPI_File_seek(fh, 0, MPI_SEEK_SET);

	MPI_File_read_all(fh, buffer, iterations * elements, DATATYPE, &status);

	MPI_File_close(&fh);

	free_buffer();
}

void usage (char** argv)
{
	printf(	"Usage: %s [-d] [-f filename] [-h] [-H hints] [-i iterations] [-s elements]\n"
			"	-d		Toggle delete file on close. (Default: %s)\n"
			"	-f		The name of the file used for the tests. (Default: %s)\n"
			"	-h		Display this help.\n"
			"	-H		Hints, in the form key=value.\n"
			"	-i		Number of iterations. (Default: %d)\n"
			"	-s		The number of elements per iteration per process. (Default: %d)\n"
			"			Filesize = Elements * Iterations * %d\n"
	, argv[0], BOOL(mode & MPI_MODE_DELETE_ON_CLOSE), DEFAULT_FILENAME, DEFAULT_ITERATIONS, DEFAULT_ELEMENTS, datatype_size);
}

void get_args (int argc, char** argv)
{
	int opt;
	char* key;
	char* value;
	char multiplier;

	while ((opt = getopt(argc, argv, "df:hH:i:s:")) != -1)
	{
		switch (opt)
		{
			case 'd':
				mode ^= MPI_MODE_DELETE_ON_CLOSE;
				break;
			case 'f':
				filename = strdup(optarg);
				break;
			case 'h':
				usage(argv);
				MPI_Finalize();
				exit(0);
			case 'H':
				/* Hint format must be key=value. */
				key = optarg;

				if ((value = strchr(optarg, '=')) == NULL)
				{
					if (rank == 0)
					{
						printf("Error: Invalid hint.\n");
					}

					MPI_Abort(MPI_COMM_WORLD, 1);
				}

				/* Separate key and value. */
				*value = '\0';
				++value;

				if (rank == 0)
				{
					printf("Hint: %s=%s\n", key, value);
				}

				MPI_Info_set(info, key, value);

				/* Restore the string. */
				--value;
				*value = '=';
				break;
			case 'i':
				iterations = atoi(optarg);
				break;
			case 's':
				elements = atoi(optarg);
				multiplier = *(optarg + strlen(optarg) - 1);

				switch (multiplier)
				{
					case 'G':
						elements *= 1024;
					case 'M':
						elements *= 1024;
					case 'K':
						elements *= 1024;
						break;
				}

				break;
		}
	}
}

void allocate_buffer (int buffer_size)
{
	if ((buffer = malloc(buffer_size * datatype_size)) == NULL)
	{
		printf("%3d/%3d: Error: Can not allocate memory.\n", rank + 1, size);

		MPI_Abort(MPI_COMM_WORLD, 1);
	}

	memset(buffer, rank, buffer_size * datatype_size);
}

void free_buffer ()
{
	free(buffer);
	buffer = NULL;
}

int main (int argc, char** argv)
{
	int i, j;

	MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Type_size(DATATYPE, &datatype_size);

	printf("%3d/%3d: Hello world!\n", rank + 1, size);

	MPI_Barrier(MPI_COMM_WORLD);

	MPI_Info_create(&info);

	get_args(argc, argv);

	int array_sizes[] = { size * elements };
	int array_subsizes[] = { elements };
	int array_starts[] = { rank * elements };

	MPI_Type_create_subarray(1, array_sizes, array_subsizes, array_starts, MPI_ORDER_C, DATATYPE, &datatype);
	MPI_Type_commit(&datatype);

	MPI_Barrier(MPI_COMM_WORLD);

	/* Run all specified tests in the given order. */
	for (i = optind; i < argc; ++i)
	{
		for (j = 0; j < sizeof(tests) / sizeof(test); ++j)
		{
			if (strcmp(argv[i], tests[j].name) == 0)
			{
				printf("%3d/%3d: Running %s...\n", rank + 1, size, tests[j].name);
				tests[j].function();
			}
		}
	}

	MPI_Type_free(&datatype);
	MPI_Info_free(&info);

	MPI_Finalize();

	return 0;
}
