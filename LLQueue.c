#include <malloc.h>
#include <stdatomic.h>
#include <stdbool.h>

#include "HazardPointer.h"
#include "LLQueue.h"

struct LLNode;
typedef struct LLNode LLNode;
typedef _Atomic(LLNode*) AtomicLLNodePtr;

struct LLNode {
    AtomicLLNodePtr next;
    Value value;
};

LLNode* LLNode_new(Value item)
{
    LLNode* node = (LLNode*)malloc(sizeof(LLNode));
    atomic_init(&node->next,NULL);
    node->value=TAKEN_VALUE;
    return node;
}

struct LLQueue {
    AtomicLLNodePtr head;
    AtomicLLNodePtr tail;
    HazardPointer hp;
};

LLQueue* LLQueue_new(void)
{
    LLQueue* queue = (LLQueue*)malloc(sizeof(LLQueue));
    atomic_init(&queue->head,NULL);
    atomic_init(&queue->tail,NULL);
    return queue;
}

void LLQueue_delete(LLQueue* queue)
{
	LLNode* prev;
    while(atomic_load(&queue->tail)!=NULL){
    	prev=atomic_load(&queue->tail);
    	atomic_store(&queue->tail,atomic_load(&prev->next));
    	free(prev);
    }
    free(queue);
}

void LLQueue_push(LLQueue* queue, Value item)
{
	LLNode* node= LLNode_new(item);
    LLNode* prev=atomic_load(&queue->head);
    do{
    	if(atomic_compare_exchange_strong(&queue->head,&prev,node)){
    		if(prev!=NULL)	prev->next=node;
    		return;
    	}else{
    		prev=atomic_load(&queue->head);
    	}
    }while(true);

}

Value LLQueue_pop(LLQueue* queue)
{
	LLNode* tail=atomic_load(&queue->tail);
	Value ret=EMPTY_VALUE;
	do{
		if(tail==NULL)	return EMPTY_VALUE;
		if(atomic_compare_exchange_strong(&tail->value,&ret,EMPTY_VALUE)){
			tail=atomic_load(&queue->tail);
		}else{
			atomic_store(&queue->tail,(atomic_load(&tail->next)));
			HazardPointer_retire(NULL,tail);
			return ret;
		}
	}while(true);
}

bool LLQueue_is_empty(LLQueue* queue)
{
	if(atomic_load(&queue->tail)==NULL)	return true;
    return false;
}
