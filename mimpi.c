/**
 * This file is for implementation of MIMPI library.
 * */

#include "mimpi.h"
#include <pthread.h>
#include "mimpi_common.h"
#include <string.h>
#include <stdint.h>
#include "channel.h"
#include "unistd.h"
#include <stdlib.h>


int MIMPI_world_size;


int MIMPI_id=0;
pthread_mutex_t MIMPI_mutex;
pthread_mutex_t MIMPI_received_list_mutex;
pthread_cond_t MIMPI_message_received;
pthread_t MIMPI_barrier_thread;
pthread_t MIMPI_reader[16];
int MIMPI_finished_proccesses[16];
int MIMPI_any_finished;
int MIMPI_main_thread_finished;

typedef struct node {
	int source;
	int tag;
	int size;
	void *data;
	struct node* next;
}MIMPI_node_t;

MIMPI_node_t* first_node;
MIMPI_node_t* last_node;

void add_new_message(int source,int tag,int size,void *data){
	pthread_mutex_lock(&MIMPI_received_list_mutex);
	if(MIMPI_main_thread_finished==1){
		free(data);
		pthread_mutex_unlock(&MIMPI_received_list_mutex);
		return;
	}
	MIMPI_node_t* new_node;
	new_node=(void*)malloc(sizeof(MIMPI_node_t));
	new_node->source=source;
	new_node->tag=tag;
	new_node->size=size;
	new_node->data=data;
	new_node->next=NULL;
	if(last_node==NULL){
		first_node=new_node;
		last_node=new_node;
	}else{
		last_node->next=new_node;
		last_node=new_node;
	}
	pthread_mutex_unlock(&MIMPI_received_list_mutex);
	pthread_cond_signal(&MIMPI_message_received);
}
	



//returns 1 if file descriptor fd is used by this proccess
int MIMPI_fd_used(int id, int fd){
	
	fd-=MIMPI_PTP_FD_OFFSET;
	if(fd%2==1){
		if(fd>MIMPI_id*(MIMPI_world_size-1)*2 && fd<(MIMPI_id+1)*(MIMPI_world_size-1)*2)	return 1;
		return 0;
	}
	if(fd%2==0){
		if(fd<MIMPI_id*(MIMPI_world_size-1)*2){
			if(fd % ((MIMPI_world_size-1)*2) == (MIMPI_id-1)*2)	return 1;
		}
		if(fd>=(MIMPI_id+1)*(MIMPI_world_size-1)*2){
			if(fd % ((MIMPI_world_size-1)*2) == MIMPI_id*2)	return 1;
		}
		return 0;
	}
	return 0;
}



int MIMPI_ptp(int sender,int receiver){
	return ((MIMPI_world_size-1)*sender+receiver+(receiver>sender ? -1 : 0))*2+MIMPI_PTP_FD_OFFSET;
}

void signal_remote_finished(int n){
	MIMPI_finished_proccesses[n]=1;
	pthread_cond_signal(&MIMPI_message_received);
}

void* concurrent_reader(void* arg){
	int n;
	memcpy(&n,arg,sizeof(int));
	free(arg);
	int fd=MIMPI_ptp(n,MIMPI_id);
	int tag;
	int mess_size;
	void *meta_data;
	void *data;
	while(true){
		if(MIMPI_main_thread_finished==1)	return NULL;
		meta_data=(void*)malloc(sizeof(int)*2);
		int res=chrecv(fd,meta_data,2*sizeof(int));
		if(res<0){
			signal_remote_finished(n);
			free(meta_data);
			return NULL;
		}
		memcpy(&tag,meta_data,sizeof(int));
		memcpy(&mess_size,meta_data+sizeof(int),sizeof(int));
		free(meta_data);
		data=(void*)malloc(mess_size);
		if(mess_size==0 && tag==0){
			free(data);
			return NULL;
		}
		if(tag==-1){
			signal_remote_finished(n);
			free(data);
			return NULL;
		}
		int bytes_received=0;
		while(bytes_received<mess_size){
			if(MIMPI_main_thread_finished==1)	return NULL;
			int res=chrecv(fd,data+bytes_received,mess_size-bytes_received);
			if(res<0){
				signal_remote_finished(n);
				free(data);
				return NULL;
			}	
			bytes_received+=res;
		}
		add_new_message(n,tag,mess_size,data);
		pthread_cond_signal(&MIMPI_message_received);
	}
}

int send_group_message_to(){
	int i=16-MIMPI_id;
	int x=16;
	
	while(i%x!=0){
		x/=2;
	}
	return x/2;
}

void Calc_mimpi_op(uint8_t* a, uint8_t* b, int count, MIMPI_Op op){
	for(int i=0;i<count;i++){
		if(op==MIMPI_MAX){
			if(b[i]>a[i])	a[i]=b[i];
		}
		if(op==MIMPI_MIN){
			if(b[i]<a[i])	a[i]=b[i];
		}
		if(op==MIMPI_SUM){
			a[i]+=b[i];
		}
		if(op==MIMPI_PROD){
			a[i]*=b[i];
		}
	}
}


/// @brief Initialises MIMPI framework in MIMPI programs.
///
/// Opens an _MPI block_, permitting use of other MIMPI procedures.
/// @param enable_deadlock_detection - a flag whether deadlock detection
///        should be enabled or not. Note that this adds a considerable
///        overhead, so should only be used when needed.
///
void MIMPI_Init(bool enable_deadlock_detection){
	channels_init();
	first_node=NULL;
	last_node=NULL;
	pthread_cond_init(&MIMPI_message_received,NULL);
	
	char* tmp_ws=getenv("MIMPI_WORLD_SIZE");
	MIMPI_world_size=atoi(tmp_ws);
	int* give_ranks=(void*)malloc(sizeof(int)*16);
	for(int i=0;i<16;i++){
		memcpy(give_ranks+i,&i,sizeof(int));
	}
	chsend(21,give_ranks,sizeof(int)*16);
	chrecv(20,&MIMPI_id,sizeof(int));
	free(give_ranks);
	ASSERT_SYS_OK(close(20));
	ASSERT_SYS_OK(close(21));
	ASSERT_ZERO(pthread_mutex_init(&MIMPI_received_list_mutex,NULL));
	//tworzenie wątkow do wspolbieznego przetwarzanie przychodzących wiadomosci
	void* thread_arg[16];
	for(int i=0;i<MIMPI_world_size;i++){
		if(i!=MIMPI_id){
			pthread_attr_t attr;
			ASSERT_ZERO(pthread_attr_init(&attr));
			ASSERT_ZERO(pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_JOINABLE));
	   		thread_arg[i]=(void*)malloc(sizeof(int));
	   		memcpy(thread_arg[i],&i,sizeof(int));
			ASSERT_ZERO(pthread_create(&(MIMPI_reader[i]),&attr,concurrent_reader,thread_arg[i]));
			//stworz wątek
			MIMPI_finished_proccesses[i]=0;
		}
	}
}



/// @brief Finalises MIMPI framework in MIMPI programs.
///
/// Closes an _MPI block_, freeing all MIMPI-related resources.
/// After a process has called this function, all MIMPI interaction with it
/// (e.g. sending data to it) should return `MIMPI_ERROR_REMOTE_FINISHED`.
///
void MIMPI_Finalize(){
	MIMPI_main_thread_finished=1;
	pthread_mutex_lock(&MIMPI_mutex);
	pthread_mutex_unlock(&MIMPI_mutex);
	for(int i=0;i<MIMPI_world_size;i++){
		if(i!=MIMPI_id){
			MIMPI_Send(NULL,0,i,-1);
		}
	}
	
	for(int i=0;i<(MIMPI_world_size-1)*MIMPI_world_size*2;i++){
		if(MIMPI_fd_used(MIMPI_id,i+MIMPI_PTP_FD_OFFSET))	ASSERT_SYS_OK(close(i+MIMPI_PTP_FD_OFFSET));
	}
	int message=-1;
	if(MIMPI_id!=0)	ASSERT_SYS_OK(chsend(MIMPI_GROUP_FD_OFFSET+MIMPI_id*2+1,&message,sizeof(int)));
	int x=send_group_message_to();
	while(x>0){
		ASSERT_SYS_OK(chsend(MIMPI_GROUP_FD_OFFSET+30+(MIMPI_id+x)*2+1,&message,sizeof(int)));
		x/=2;
	}
	for(int i=0;i<=30;i++){
		close(MIMPI_GROUP_FD_OFFSET+i*2);
		close(MIMPI_GROUP_FD_OFFSET+i*2+1);
	}
	for(int i=0;i<MIMPI_world_size;i++){
		if(i!=MIMPI_id){
			pthread_join(MIMPI_reader[i],NULL);
		}
	}
	for(int i=20;i<=1020;i++)	close(i);
	pthread_mutex_lock(&MIMPI_received_list_mutex);
	MIMPI_node_t* cur_node=first_node;
	MIMPI_node_t* next_node=NULL;
	while(cur_node!=NULL){
		next_node=cur_node->next;
		free(cur_node->data);
		free(cur_node);
		cur_node=next_node;
	}
	
	pthread_mutex_unlock(&MIMPI_received_list_mutex);
}


/// @brief Returns the number of processes launched by `mimpirun`.
int MIMPI_World_size(){
	return MIMPI_world_size;
}

/// @brief Returns the identifier of this process.
///
/// The identifier is unique processes launched by `mimpirun`.
/// Identifiers should be consecutive natural numbers.
///
int MIMPI_World_rank(){
	return MIMPI_id;
}

/// @brief Sends data to the specified process.
///
/// Sends @ref count bytes of @ref data to the process with rank @ref destination.
/// Data is tagged with @ref tag.
///
/// @param data - data to be sent.
/// @param count - number of bytes of data to be sent.
/// @param destination - rank of the process who is to receive the data. 
/// @param tag - a discriminant of the data, which can be used
///              to distinguish between messages.
/// @return MIMPI return code:
///         - `MIMPI_SUCCESS` if operation ended successfully.
///         - `MIMPI_ERROR_ATTEMPTED_SELF_OP` if process attempted to send to itself
///         - `MIMPI_ERROR_NO_SUCH_RANK` if there is no process with rank
///           @ref destination in the world.
///         - `MIMPI_ERROR_REMOTE_FINISHED` if the process with rank
///         - @ref destination has already escaped _MPI block_.
///
MIMPI_Retcode MIMPI_Send(
    void const *data,
    int count,
    int destination,
    int tag
){
	if(destination==MIMPI_id)	return MIMPI_ERROR_ATTEMPTED_SELF_OP;
	if(destination>=MIMPI_world_size || destination<0)	return MIMPI_ERROR_NO_SUCH_RANK;
	if(count==0 && tag!=-1)	return MIMPI_SUCCESS;
	void *data_with_info=(void*)malloc(count+2*sizeof(int));
	memcpy(data_with_info,&tag,sizeof(int));
	memcpy(data_with_info+sizeof(int),&count,sizeof(int));	
	memcpy(data_with_info+2*sizeof(int),data,count);
	int bytes_send=0;
	while(bytes_send<count+2*sizeof(int)){
		int res=chsend(MIMPI_ptp(MIMPI_id,destination)+1,data_with_info+bytes_send,count+2*sizeof(int)-bytes_send);
		if(res<0){
			free(data_with_info);
			return MIMPI_ERROR_REMOTE_FINISHED;
		}
		bytes_send+=res;
	}
	free(data_with_info);
	return MIMPI_SUCCESS;
}

/// @brief Receives data from the specified process.
///
/// Blocks until @ref count bytes of @ref data tagged with @ref tag arrives
/// from the process with rank @ref destination. Then the data is put in @ref data.
///
/// @param data - place where received data is to be put.
/// @param count - number of bytes of data to be received.
/// @param source - rank of the process for data from we are waiting. 
/// @param tag - a discriminant of the data, which can be used
///              to distinguish between messages.
/// @return MIMPI return code:
///         - `MIMPI_SUCCESS` if operation ended successfully.
///         - `MIMPI_ERROR_ATTEMPTED_SELF_OP` if process attempted to receive from itself
///         - `MIMPI_ERROR_NO_SUCH_RANK` if there is no process with rank
///           @ref source in the world.
///         - `MIMPI_ERROR_REMOTE_FINISHED` if the process with rank
///         - @ref source has already escaped _MPI block_.
///         - `MIMPI_ERROR_DEADLOCK_DETECTED` if a deadlock has been detected
///           and therefore this call would else never return.
///
MIMPI_Retcode MIMPI_Recv(
    void *data,
    int count,
    int source,
    int tag
){
	if(source==MIMPI_id)	return MIMPI_ERROR_ATTEMPTED_SELF_OP;
	if(source>=MIMPI_world_size || source<0)	return MIMPI_ERROR_NO_SUCH_RANK;
	pthread_mutex_lock(&MIMPI_received_list_mutex);
	MIMPI_node_t* cur_node=first_node;
	MIMPI_node_t* prev_node=NULL;
	do{
		while(cur_node!=NULL){
			if(cur_node->source==source && cur_node->size==count){
				if(cur_node->tag==tag || tag==0){
				if(first_node==cur_node)	first_node=cur_node->next;
				if(last_node==cur_node)		last_node=prev_node;
				memcpy(data,cur_node->data,count);
				if(prev_node!=NULL)	prev_node->next=cur_node->next;
				free(cur_node->data);
				free(cur_node);
				pthread_mutex_unlock(&MIMPI_received_list_mutex);
				return MIMPI_SUCCESS;
				}
			}
			prev_node=cur_node;
			cur_node=cur_node->next;
		}
		if(MIMPI_finished_proccesses[source]==1){
			pthread_mutex_unlock(&MIMPI_received_list_mutex);
			return MIMPI_ERROR_REMOTE_FINISHED;
		}
		if(prev_node!=NULL)	cur_node=prev_node;
		ASSERT_SYS_OK(pthread_cond_wait(&MIMPI_message_received,&MIMPI_received_list_mutex));
		if(cur_node==NULL)	cur_node=first_node;
	}while(true);

	
	return MIMPI_SUCCESS;
}



/// @brief Synchronises all processes.
///
/// Blocks execution of the calling process until all processes execute
/// this function. In particular, every process executes all instructions
/// preceding the call before any process executes any instruction
/// following the call. 
///
/// @return MIMPI return code:
///         - `MIMPI_SUCCESS` if operation ended successfully.
///         - `MIMPI_ERROR_REMOTE_FINISHED` if any process in the world
///            has already escaped _MPI block_.
///         - `MIMPI_ERROR_DEADLOCK_DETECTED` if a deadlock has been detected
///           and therefore this call would else never return.
///
MIMPI_Retcode MIMPI_Barrier(){
	static int remote_finished=0;
	if(remote_finished==1 || MIMPI_any_finished)	return MIMPI_ERROR_REMOTE_FINISHED;
	int information_received;
	int waiting_on_barrier_message=1;


	int x=send_group_message_to();
	//receive message from son
	while(x>0){
		if(MIMPI_id+x<MIMPI_world_size){
			ASSERT_SYS_OK(chrecv(MIMPI_GROUP_FD_OFFSET+(MIMPI_id+x)*2,&information_received,sizeof(int)));
			if(information_received==-1)	return MIMPI_ERROR_REMOTE_FINISHED;
		}		
		x/=2;
	}
	x=send_group_message_to();
	if(x==0){
		x=1;
	}else{
		x*=2;
	}
	//send message to father
	if(MIMPI_id!=0){
		ASSERT_SYS_OK(chsend(MIMPI_GROUP_FD_OFFSET+MIMPI_id*2+1,&waiting_on_barrier_message,sizeof(int)));
	}

	//receive message from father
	if(MIMPI_id!=0){
		ASSERT_SYS_OK(chrecv(MIMPI_GROUP_FD_OFFSET+30+MIMPI_id*2,&information_received,sizeof(int)));
		if(information_received==-1)	return MIMPI_ERROR_REMOTE_FINISHED;
	}
	//send message to son
	x=send_group_message_to();
	while(x>0){
		if(MIMPI_id+x<MIMPI_world_size){
			ASSERT_SYS_OK(chsend(MIMPI_GROUP_FD_OFFSET+30+(MIMPI_id+x)*2+1,&waiting_on_barrier_message,sizeof(int)));
		}
		x/=2;
	}
	
	return MIMPI_SUCCESS;
}	

/// @brief Broadcasts data to all processes.
///
/// Makes @ref count bytes of data at address @ref data in process @ref root
/// available among all processes at address @ref data.
/// Additionally, is a synchronisation point similarly to @ref MIMPI_Barrier.
///
/// @param data - for @ref root, data to be broadcast; for other processes,
///               place where data are to be put.
/// @param count - number of bytes of data to be broadcast.
/// @param root - rank of the process whose data are to be broadcast.
///
/// @return MIMPI return code:
///         - `MIMPI_SUCCESS` if operation ended successfully.
///         - `MIMPI_ERROR_NO_SUCH_RANK` if there is no process with rank
///           @ref root in the world.
///         - `MIMPI_ERROR_REMOTE_FINISHED` if any process in the world
///            has already escaped _MPI block_.
///         - `MIMPI_ERROR_DEADLOCK_DETECTED` if a deadlock has been detected
///           and therefore this call would else never return.
///
MIMPI_Retcode MIMPI_Bcast(
    void *data,
    int count,
    int root
){
	if(MIMPI_any_finished==1)	return MIMPI_ERROR_REMOTE_FINISHED;
	int x=send_group_message_to();
	int message_size;
	int MIMPI_message_received=0;
	while(x>0){
		if(MIMPI_id+x<MIMPI_world_size){
			ASSERT_SYS_OK(chrecv(MIMPI_GROUP_FD_OFFSET+(MIMPI_id+x)*2,&message_size,sizeof(int)));
			if(message_size==-1)	return MIMPI_ERROR_REMOTE_FINISHED;
			if(message_size>0){
				int bytes_received=0;
				MIMPI_message_received=1;
				while(bytes_received<count){
					int res=chrecv(MIMPI_GROUP_FD_OFFSET+(MIMPI_id+x)*2,data+bytes_received,count-bytes_received);
					ASSERT_SYS_OK(res);
					bytes_received+=res;
				}
			}
		}		
		x/=2;
	}
	x=send_group_message_to();
	if(x==0){
		x=1;
	}else{
		x*=2;
	}
	if(root==MIMPI_id)	MIMPI_message_received=1;
	//send message to father
	if(MIMPI_id!=0){
		if(MIMPI_message_received==1){
			message_size=count;
		}else{
			message_size=0;
		}
		ASSERT_SYS_OK(chsend(MIMPI_GROUP_FD_OFFSET+MIMPI_id*2+1,&message_size,sizeof(int)));
		if(MIMPI_message_received==1){
			int bytes_send=0;
			while(bytes_send<count){
				bytes_send+=chsend(MIMPI_GROUP_FD_OFFSET+MIMPI_id*2+1,data+bytes_send,count-bytes_send);
			}
		}
	}
	//receive message from father
	if(MIMPI_id!=0){
		int bytes_received=0;
		while(bytes_received<count)	bytes_received+=chrecv(MIMPI_GROUP_FD_OFFSET+30+MIMPI_id*2,data+bytes_received,count-bytes_received);

	}
	//send message to son
	x=send_group_message_to();
	while(x>0){
		if(MIMPI_id+x<MIMPI_world_size){
			int bytes_send=0;
			while(bytes_send<count)	bytes_send+=chsend(MIMPI_GROUP_FD_OFFSET+30+(MIMPI_id+x)*2+1,data+bytes_send,count-bytes_send);
		}
		x/=2;
	}
	return MIMPI_SUCCESS;
}


/// @brief Reduces data from all processes to one.
///
/// Performs reduction of kind @ref op over @ref count bytes of data
/// stored at address @ref send_data in every process. The reduction's result
/// is put at @ref recv_data *ONLY* in the process with rank @ref root.
/// Additionally, is a synchronisation point similarly to @ref MIMPI_Barrier.
///
/// @param send_data - data to be reduced.
/// @param recv_data - place where reduction's result is to be put.
/// @param count - number of bytes of data to be reduced.
/// @param op - a particular operation to be performed for reduction.
/// @param root - rank of the process who is to hold the result of reduction.
///
/// @return MIMPI return code:
///         - `MIMPI_SUCCESS` if operation ended successfully.
///         - `MIMPI_ERROR_NO_SUCH_RANK` if there is no process with rank
///           @ref root in the world.
///         - `MIMPI_ERROR_REMOTE_FINISHED` if any process in the world
///            has already escaped _MPI block_.
///         - `MIMPI_ERROR_DEADLOCK_DETECTED` if a deadlock has been detected
///           and therefore this call would else never return.
///
MIMPI_Retcode MIMPI_Reduce(
    void const *send_data,
    void *recv_data,
    int count,
    MIMPI_Op op,
    int root
){

	static int remote_finished=0;
	if(remote_finished==1 || MIMPI_any_finished)	return MIMPI_ERROR_REMOTE_FINISHED;
	uint8_t* data;
	uint8_t* my_data;
	my_data=(void*)malloc(sizeof(uint8_t)*count);
	memcpy(my_data,send_data,sizeof(uint8_t)*count);
	


	int x=send_group_message_to();
	//receive message from son
	while(x>0){
		if(MIMPI_id+x<MIMPI_world_size){
			data=(void*)malloc(sizeof(uint8_t ) * count );
			int bytes_received=0;
			while(bytes_received<count)	bytes_received+=chrecv(MIMPI_GROUP_FD_OFFSET+(MIMPI_id+x)*2,data+bytes_received,sizeof(uint8_t)*count-bytes_received);
			Calc_mimpi_op(my_data,data,count,op);
			free(data);
		}		

		x/=2;
	}
	x=send_group_message_to();
	if(x==0){
		x=1;
	}else{
		x*=2;
	}
	//send message to father
	if(MIMPI_id!=0){
		int bytes_send=0;
		while(bytes_send<count)	bytes_send+=chsend(MIMPI_GROUP_FD_OFFSET+MIMPI_id*2+1,my_data+bytes_send,sizeof(uint8_t)*count-bytes_send);
	}
	if(MIMPI_id!=0){
		int bytes_received=0;
		while(bytes_received<count)	bytes_received+=chrecv(MIMPI_GROUP_FD_OFFSET+30+MIMPI_id*2,my_data+bytes_received,sizeof(uint8_t)*count-bytes_received);
	}
	x=send_group_message_to();
	while(x>0){
		if(MIMPI_id+x<MIMPI_world_size){
			int bytes_received=0;
			while(bytes_received<count)	bytes_received+=chsend(MIMPI_GROUP_FD_OFFSET+30+(MIMPI_id+x)*2+1,my_data+bytes_received,sizeof(uint8_t)*count-bytes_received);
		}
		x/=2;
	}
	if(MIMPI_id==root){
		memcpy(recv_data,my_data,sizeof(uint8_t)*count);
	}
	free(my_data);
	return MIMPI_SUCCESS;
}
