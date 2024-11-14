#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "thrd_pool.h"

int nums = 0;
int done = 0;

pthread_mutex_t lock;

void task(void* arg){
	int cnt = 1;
	int index = (int)arg;
	while(cnt <= 5){
		pthread_mutex_lock(&lock);
		printf("任务%d，cnt现在为:%d\n", index, cnt++);
		pthread_mutex_unlock(&lock);
	}
}

int main(){
	int threads = 8;
	int queue_size = 256;
	
	thread_pool_t* pool = thread_pool_create(threads, queue_size);
	if(pool == NULL){
		printf("thread pool create error!\n");
		return 1;
	}

	for(int i = 1; i <= 10; i++){
		if(thread_pool_post(pool, &task, (void*) i) == 0){
			nums++;
		}
	}

	printf("add %d tasks\n", nums);

	wait_all_done(pool);
	
	printf("did %d tasks\n", done);
	thread_pool_destroy(pool);
	return 0;
}
