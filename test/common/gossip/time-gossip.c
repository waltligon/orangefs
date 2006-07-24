#include <math.h>
#include <stdlib.h>
#include <sys/time.h>

#include <gossip.h>

enum{
	DEBUG_SOME_STUFF = 1,
};


double Wtime(void);

int main(int argc, char **argv)	{
	unsigned int iter = 500000;
	unsigned int i;
	float x = 5;
	float y = 0;
	double time1, time2, time3;
	
	/* try the stderr method first */
	gossip_enable_stderr();

	/* now let's turn off debugging */
	gossip_set_debug_mask(0,0);

	time1 = Wtime();

	for(i=0; i<iter; i++)
	{
		x += 3;
		y = x*53;
		/* try a normal debugging message w/ line numbers */
		gossip_ldebug(DEBUG_SOME_STUFF, "This shouldn't print!\n");
	}

	time2 = Wtime();

	for(i=0; i<iter; i++)
	{
		x += 3;
		y = x*53;
		/* no debugging message */
	}

	time3 = Wtime();

	/* turn debugging back on to print out some messages */
	gossip_set_debug_mask(1,DEBUG_SOME_STUFF);

	/* print a normal debugging message */
	gossip_ldebug(DEBUG_SOME_STUFF, "Timing information:\n");
	gossip_ldebug(DEBUG_SOME_STUFF, "%u iterations, w/ debugging statment: %f seconds\n", iter, (time2 - time1));
	gossip_ldebug(DEBUG_SOME_STUFF, "%u iterations, w/ debugging statment: %f seconds\n", iter, (time3 - time2));

	/* shutdown gossip */
	gossip_disable();

	return(0);
}

double Wtime(void)
{
	struct timeval t;

	gettimeofday(&t, NULL);
	return((double)t.tv_sec + (double)t.tv_usec / 1000000);
}
