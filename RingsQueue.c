#include <malloc.h>
#include <pthread.h>
#include <stdatomic.h>


#include <stdlib.h>

#include "HazardPointer.h"
#include "RingsQueue.h"

struct RingsQueueNode;
typedef struct RingsQueueNode RingsQueueNode;

struct RingsQueueNode {
    _Atomic(RingsQueueNode*) next;
    Value* buff;
    _Atomic(size_t)	push_idx;
    _Atomic(size_t)	pop_idx;

};



struct RingsQueue {
    RingsQueueNode* head;
    RingsQueueNode* tail;
    pthread_mutex_t pop_mtx;
    pthread_mutex_t push_mtx;
};

RingsQueueNode* RingsQueueNode_new(void){
	RingsQueueNode* node = (RingsQueueNode*)malloc(sizeof(RingsQueueNode));
	atomic_store(&node->next,NULL);
	node->buff =  malloc(sizeof(Value)*RING_SIZE);
	for(int i=0;i<RING_SIZE;i++)	node->buff[i]=EMPTY_VALUE;
	atomic_init(&node->push_idx,0);
	atomic_init(&node->pop_idx,0);
	return node;
}

void RingsQueueNode_delete(RingsQueueNode* node){
	free(node->buff);
	free(node);
}

RingsQueue* RingsQueue_new(void)
{
    RingsQueue* queue = (RingsQueue*)malloc(sizeof(RingsQueue));
    queue->head=NULL;
    queue->tail=NULL;
    pthread_mutex_init(&queue->pop_mtx,NULL);
    pthread_mutex_init(&queue->push_mtx,NULL);

    return queue;
}


bool RingFull(RingsQueueNode* node){
	if(node->buff[atomic_load(&node->push_idx)]==EMPTY_VALUE)	return false;	
	return true;
}

bool RingEmpty(RingsQueueNode* node){
	if(node==NULL)	return true;
	if(node->buff[atomic_load(&node->pop_idx)]==EMPTY_VALUE)	return true;
	return false;
}

void RingsQueue_delete(RingsQueue* queue)
{
	while(queue->tail!=NULL){
		RingsQueueNode* prev_tail=queue->tail;
		queue->tail=queue->tail->next;
		RingsQueueNode_delete(prev_tail);
	}
    free(queue);
}

void RingsQueueNode_push(RingsQueueNode* node, Value item){
	
	node->buff[atomic_load(&node->push_idx)]=item;
	size_t idx=(atomic_load(&node->push_idx)+1)%RING_SIZE;
	atomic_store(&node->push_idx,idx);
}

void RingsQueue_push(RingsQueue* queue, Value item)
{

	pthread_mutex_lock(&queue->push_mtx);
	if(queue->head==NULL){
		RingsQueueNode* node=RingsQueueNode_new();
		queue->head=node;
		queue->tail=node;
	}

	if(RingFull(queue->head)){
		RingsQueueNode* node=RingsQueueNode_new();
		queue->head->next=node;
		queue->head=node;
	}
	RingsQueueNode_push(queue->head,item);
	pthread_mutex_unlock(&queue->push_mtx);
}

Value RingsQueueNode_pop(RingsQueueNode* node){
	Value ret=node->buff[atomic_load(&node->pop_idx)];
	node->buff[atomic_load(&node->pop_idx)]=EMPTY_VALUE;
	atomic_store(&node->pop_idx,(atomic_load(&node->pop_idx)+1)%RING_SIZE);

	return ret;
}

Value RingsQueue_pop(RingsQueue* queue)
{	

	if(queue->tail==NULL)	return EMPTY_VALUE;
	pthread_mutex_lock(&queue->pop_mtx);
	if(RingEmpty(queue->tail)){
		RingsQueueNode* prev_tail=queue->tail;
		queue->tail=queue->tail->next;
		if(queue->tail==NULL)	queue->head=NULL;
		RingsQueueNode_delete(prev_tail);
	}
	if(RingsQueue_is_empty(queue)){
		pthread_mutex_unlock(&queue->pop_mtx);
		return EMPTY_VALUE; 
	}
	Value ret=RingsQueueNode_pop(queue->tail);
	pthread_mutex_unlock(&queue->pop_mtx);
	return ret;

}

bool RingsQueue_is_empty(RingsQueue* queue)
{
	if(queue->tail==NULL)	return true;
	if(RingEmpty(queue->tail))	return true;
    return false;
}
