#include "mimpi_common.h"
#include "mimpi.h"
#include <stdlib.h>
#include <unistd.h>
#include "channel.h"
#include <stdio.h>
#include <sys/wait.h>



void open_channel(int fd){
	int tmp[2];
	
	ASSERT_SYS_OK(channel(tmp));
	ASSERT_SYS_OK(dup2(tmp[0],1020));
	ASSERT_SYS_OK(close(tmp[0]));
	ASSERT_SYS_OK(dup2(tmp[1],1021));
	ASSERT_SYS_OK(close(tmp[1]));
	ASSERT_SYS_OK(dup2(1020,fd));
	ASSERT_SYS_OK(close(1020));
	ASSERT_SYS_OK(dup2(1021,fd+1));
	ASSERT_SYS_OK(close(1021));
	
	return;
}

int main(int argc, char* argv[]){
	if(argc<3){
		//za malo argumentow
		return 0;
	}
	//przygotuj srodowisko
	int n = atoi(argv[1]);
	MIMPIRUN_set_worldsize(n);
	open_channel(20);
	

	//group messages
	for(int i=0;i<=30;i++){

		open_channel(MIMPI_GROUP_FD_OFFSET+i*2);
	}
	//ptp communication pipes
	for(int i=0;i<n;i++){
		for(int j=0;j<n;j++){
			if(i==j)	continue;

			int fd=MIMPI_ptp_fd(i,j);
			open_channel(fd);
			
		}
	}

	for(int i=0;i<n;i++){
		char dsc[16];
		snprintf(dsc,16,"%d",n);
		setenv("MIMPI_WORLD_SIZE",dsc,true);
		pid_t pid=fork();
		if(!pid){
		 	ASSERT_SYS_OK(execvp(argv[2],&argv[2]));
		 }
		
	}
	close(20);
	close(21);
	for(int i=0;i<(n-1)*n*2;i++){
		ASSERT_SYS_OK(close(i+MIMPI_PTP_FD_OFFSET));
	}
	for(int i=0;i<=30;i++){
		close(MIMPI_GROUP_FD_OFFSET+i*2);
		close(MIMPI_GROUP_FD_OFFSET+i*2+1);

	}
	for(int i=0;i<n;i++){
		ASSERT_SYS_OK(wait(NULL));
	}
	return 0;
}
