#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>

pthread_cond_t cond1 = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mut1 = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond2 = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mut2 = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond1B = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mut1B = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond2B = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mut2B = PTHREAD_MUTEX_INITIALIZER;

int done = 0;
int todo = 0;
int doneB = 0;
int todoB = 0;

double wtime(void);
void* thread1_fn(void* foo);
void* thread1_fnB(void* foo);
void wakeywakeyB(void);
void wakeywakey(void);

double wtime(void)
{
    struct timeval t;

    gettimeofday(&t, NULL);
    return((double)t.tv_sec + (double)t.tv_usec / 1000000);
}

void* thread1_fn(void* foo)
{
    int ret = -1;

    while(1)
    {
	pthread_mutex_lock(&mut1);
	    while(todo == 0)
	    {
		ret = pthread_cond_wait(&cond1, &mut1);
		assert(ret == 0);
	    }
	    wakeywakeyB();
	    todo = 0;
	pthread_mutex_unlock(&mut1);
	
	pthread_mutex_lock(&mut2);
	done = 1;
	pthread_mutex_unlock(&mut2);
	pthread_cond_signal(&cond2);
    }

}

void* thread1_fnB(void* foo)
{
    int ret = -1;

    while(1)
    {
	pthread_mutex_lock(&mut1B);
	    while(todoB == 0)
	    {
		ret = pthread_cond_wait(&cond1B, &mut1B);
		assert(ret == 0);
	    }
	    todoB = 0;
	pthread_mutex_unlock(&mut1B);
	
	pthread_mutex_lock(&mut2B);
	doneB = 1;
	pthread_mutex_unlock(&mut2B);
	pthread_cond_signal(&cond2B);
    }

}


void wakeywakeyB(void)
{
    int ret = -1;

    pthread_mutex_lock(&mut1B);
    todoB = 1;
    pthread_mutex_unlock(&mut1B);
    pthread_cond_signal(&cond1B);
    
    pthread_mutex_lock(&mut2B);
	while(doneB == 0)
	{
	    ret = pthread_cond_wait(&cond2B, &mut2B);
	    assert(ret == 0);
	}
	doneB = 0;
    pthread_mutex_unlock(&mut2B);
    
}

void wakeywakey(void)
{
    int ret = -1;

    pthread_mutex_lock(&mut1);
    todo = 1;
    pthread_mutex_unlock(&mut1);
    pthread_cond_signal(&cond1);
    
    pthread_mutex_lock(&mut2);
	while(done == 0)
	{
	    ret = pthread_cond_wait(&cond2, &mut2);
	    assert(ret == 0);
	}
	done = 0;
    pthread_mutex_unlock(&mut2);
    
}

#define ITERATIONS 100000
int main(int argc, char **argv)	
{
    pthread_t thread1;
    pthread_t thread1B;
    int ret = -1;
    int i = 0;
    double time1, time2;

    ret = pthread_create(&thread1, NULL, thread1_fn, NULL);
    assert(ret == 0);
    ret = pthread_create(&thread1B, NULL, thread1_fnB, NULL);
    assert(ret == 0);

    time1 = wtime();
    for(i=0; i<ITERATIONS; i++)
    {
	wakeywakey();
    }
    time2 = wtime();

    printf("time for %d iterations: %f seconds.\n", ITERATIONS, (time2-time1));
    printf("per iteration: %f\n", (time2-time1)/(double)ITERATIONS);

    return(0);
}
