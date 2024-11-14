
#include <pthread.h>
#include "thrd_pool.h"
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

typedef struct task_t{
	handler_pt func;
	void *arg;
}task_t;

typedef struct task_queue_t{
	uint32_t head;
	uint32_t tail;
	uint32_t count;
	task_t *queue;
}task_queue_t;

typedef struct thread_pool_t{
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	
	pthread_t *threads;
	
	task_queue_t task_queue;
	
	int closed;
	int started;
	
	int thrd_count;
	int queue_size;
}thread_pool_t;

static int thread_pool_free(thread_pool_t* pool);

static void* thread_worker(void* arg){
	thread_pool_t* pool = (thread_pool_t*)arg;
	
	task_queue_t *que;
	task_t task;
	while(1){

		// 对pool的取任务需要加锁
		pthread_mutex_lock(& pool->mutex);
		que = &pool->task_queue;
		
		// 没有任务且没有标记退出， 线程休眠， 用while是怕虚假唤醒
		while(que->count == 0 && pool->closed == 0){
			// 先释放mutex，然后阻塞再condition（线程休眠
			// 解除阻塞， 加上mutex
			pthread_cond_wait(&(pool->condition), &(pool->mutex));
			
		}
		
		// 销毁线程池
		if(pool->closed == 1){
			break;
		}
		
		task = que->queue[que->head];
		que->head = (que->head + 1) % pool->queue_size;
		que->count--;
		pthread_mutex_unlock(&(pool->mutex));
		
		// 运行任务
		(*(task.func))(task.arg);
	}
	pool->started--;

	// 正常退出不会解锁，因此特殊处理
	pthread_mutex_unlock(&(pool->mutex));
	pthread_exit(NULL);
	return NULL;
}


thread_pool_t *thread_pool_create(int thrd_count, int queue_size){
	thread_pool_t* pool;
	// 参数合法
	if(thrd_count <= 0 || queue_size <= 0){
		return NULL;
	}
	
	pool = (thread_pool_t *)malloc(sizeof(*pool));
	// 防止内存不足
	if(pool == NULL){
		return NULL;
	}
	
	// 初始化内存池
	pool->closed = pool->started = 0;
	pool->thrd_count = 0;
	pool->queue_size = queue_size;
	
	pool->task_queue.head = pool->task_queue.tail = pool->task_queue.count = 0;

	pool->task_queue.queue = (task_t*)malloc(sizeof(task_t)* queue_size);

	// 分配失败，防止内存不足
	if(pool->task_queue.queue == NULL){
		thread_pool_free(pool);
		return NULL;
	}

	pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * thrd_count);

	if(pool->threads == NULL){
		return NULL;
	}
	
	
	int i = 0;
	for(; i < thrd_count; i++){
		if(pthread_create(&pool->threads[i], NULL, thread_worker, (void*)pool) != 0){
			thread_pool_free(pool);
			return NULL;
		}
		
		pool->started ++;
		pool->thrd_count ++;
	}
	
	pthread_mutex_init(&pool->mutex, NULL);
	pthread_cond_init(&pool->condition, NULL);
	
	return pool;
}

// 释放内存
static int thread_pool_free(thread_pool_t* pool){

	if(pool == NULL || pool->started <= 0){
		return -1;
	}
	if(pool->threads){
		free(pool->threads);
		pool->threads = NULL;
		
		// 单个锁单个操作
		pthread_mutex_lock(&(pool->mutex));
		pthread_mutex_destroy(&(pool->mutex));
		pthread_cond_destroy(&(pool->condition));
	}
	
	if(pool->task_queue.queue){
		free(pool->task_queue.queue);
		pool->task_queue.queue = NULL;
		
	}

	free(pool);
	return 0;
}


int thread_pool_destroy(thread_pool_t* pool){
	if(pool == NULL){
		return -1;
	}
	
	// 避免其他进程再往队列里再添加任务
	if(pthread_mutex_lock(&(pool->mutex)) != 0){
		return -2;
	}
	// 标记退出
	if(pool->closed == 1){
		return -3;
	}
	
	pool->closed = 1;
	
	// 让所有线程苏醒退出
	if(pthread_cond_broadcast(&(pool->condition)) != 0){
		pthread_mutex_unlock(&(pool->mutex));
		return -2;
	}
	
	// 等待所有线程正常退出
	wait_all_done(pool);
	
	thread_pool_free(pool);
	return 0;
}

// 添加任务到任务队列
int thread_pool_post(thread_pool_t* pool, handler_pt func, void* arg){
	if(pool == NULL || func == NULL){
		return -1;
	}
	
	// 一个线程取任务
	if(pthread_mutex_lock(&(pool->mutex)) != 0){
		return -2;
	}
	
	if(pool->closed == 1){
		pthread_mutex_unlock(&(pool->mutex));
		return -3;
	}
	
	if(pool->queue_size == pool->task_queue.count){
		pthread_mutex_unlock(&(pool->mutex));
		return -4;
	}

	// 获取任务队列
	task_queue_t* task_queue = &(pool->task_queue);

	// 插入任务，处理队列指针
	
	task_t* task = (&task_queue->queue[task_queue->tail]);
	task->func = func;
	task->arg = arg;
	task_queue->tail = (task_queue->tail + 1) % pool->queue_size;
	task_queue->count++;
	
	// 通知某个线程苏醒，对任务进行操作
	if(pthread_cond_signal(&(pool->condition)) != 0){
		pthread_mutex_unlock(&(pool->mutex));
		return -5;
	}
	pthread_mutex_unlock(&(pool->mutex));
	return 0;
}

// 等待所有线程结束
int wait_all_done(thread_pool_t* pool){
	int i, ret = 0;
	for(i = 0; i < pool->thrd_count; i++){
		if(pthread_join((pool->threads[i]), NULL) != 0){
			ret = 1;
		}
	}
	return ret;
}
