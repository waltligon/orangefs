#include <stdio.h>

int main(int argc, char **argv)	
{
	int message_size = 0;
	int num_peers = 0;
	float min, max, ave, stddev;
	float bw = 0;
	float bw_ave;
	int i;
	int ret = 0;
	char char_buffer[256];

	while(1)	
	{
		bw_ave = 0.0;
		for(i=0; i<5; i++)
		{
			if(fgets(char_buffer, 254, stdin) == NULL)
			{
				return(0);
			}
			ret = sscanf(char_buffer, "%d %d %f %f %f %f %f",
				&message_size, &num_peers, &min, &max, &ave, &stddev,
				&bw);
			bw_ave += bw;
			if(ret < 7)
			{
				fprintf(stderr, "parse error.\n");
				return(-1);
			}
		}
		
		bw_ave /= 5.0;

		printf("%d %f (size,bw)\n", message_size, bw_ave);
	}

	return(0);

}
