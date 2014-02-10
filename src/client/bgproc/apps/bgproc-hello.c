extern int bgproc_start(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	if (bgproc_start(argc, argv) != 0) {
		return 1;
	}
	return 42;
}
