#include <malloc.h>
#include <pthread.h>
#include <stdatomic.h>

#include "SimpleQueue.h"

struct SimpleQueueNode;
typedef struct SimpleQueueNode SimpleQueueNode;

struct SimpleQueueNode {
    _Atomic(SimpleQueueNode*) next;
    Value item;
};

SimpleQueueNode* SimpleQueueNode_new(Value item)
{
    SimpleQueueNode* node = (SimpleQueueNode*)malloc(sizeof(SimpleQueueNode));
    atomic_init(&node->next, NULL);
    node->item=item;

    return node;
}


struct SimpleQueue {
    SimpleQueueNode* head;
    SimpleQueueNode* tail;
    pthread_mutex_t head_mtx;
    pthread_mutex_t tail_mtx;
};

SimpleQueue* SimpleQueue_new(void)
{
    SimpleQueue* queue = (SimpleQueue*)malloc(sizeof(SimpleQueue));
    queue->head=NULL;
    queue->tail=NULL;
    pthread_mutex_init(&(queue->head_mtx),NULL);
    pthread_mutex_init(&(queue->tail_mtx),NULL);

    return queue;
}

void SimpleQueue_delete(SimpleQueue* queue)
{
	while(queue->tail!=NULL){
		SimpleQueueNode* next=atomic_load(&queue->tail->next);
		free(queue->tail);
		queue->tail=next;
	}
    free(queue);
}

void SimpleQueue_push(SimpleQueue* queue, Value item)
{
	SimpleQueueNode* node = SimpleQueueNode_new(item);
	pthread_mutex_lock(&(queue->head_mtx));
	if(queue->head==NULL){
		pthread_mutex_lock(&(queue->tail_mtx));
		queue->head=node;
		queue->tail=node;
		pthread_mutex_unlock(&(queue->tail_mtx));
	}else{	
		atomic_store(&queue->head->next,node);
		queue->head=node;
	}
	pthread_mutex_unlock(&(queue->head_mtx));
	return;

}

Value SimpleQueue_pop(SimpleQueue* queue)
{
	pthread_mutex_lock(&(queue->tail_mtx));
	if(queue->tail==NULL){
		pthread_mutex_unlock(&queue->tail_mtx);
		return EMPTY_VALUE;
	}
	Value ret=queue->tail->item;
	SimpleQueueNode* next=atomic_load(&queue->tail->next);
	if(queue->tail==queue->head){
		pthread_mutex_lock(&(queue->head_mtx));
		free(queue->tail);
		queue->tail=NULL;
		queue->head=NULL;
		pthread_mutex_unlock(&(queue->head_mtx));
	}else{
		SimpleQueueNode* prev_tail=queue->tail;
		queue->tail=next;
		free(prev_tail);
	}
	pthread_mutex_unlock(&(queue->tail_mtx));
	return ret;
}

bool SimpleQueue_is_empty(SimpleQueue* queue)
{
	if(queue->head==NULL)	return true;
    return false; 
}
