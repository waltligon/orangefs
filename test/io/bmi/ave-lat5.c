#include <stdio.h>

int main(int argc, char **argv)	
{
	int message_size = 0;
	float total_lat = 0.0;
	float ind_lat = 0.0;
	float ind_ave, total_ave;
	int i;
	int ret = 0;
	char char_buffer[256];

	while(1)	
	{
		ind_ave = 0.0;
		total_ave = 0.0;
		for(i=0; i<5; i++)
		{
			if(fgets(char_buffer, 254, stdin) == NULL)
			{
				return(0);
			}
			ret = sscanf(char_buffer, "%d\t%f\t%f",
				&message_size, &total_lat, &ind_lat);
			ind_ave += ind_lat;
			total_ave += total_lat;
			if(ret < 3)
			{
				fprintf(stderr, "parse error.\n");
				return(-1);
			}
		}
		
		ind_ave /= 5.0;
		total_ave /= 5.0;

		printf("%d %f %f (size,total,ind)\n", message_size, total_ave, ind_ave);
	}

	return(0);

}
