int * shared_var;

int main smth smth smth bla bla bla
if ((shmid= shmget(IPC_PRIVATE,sizeof(int),IPC_CREAT| 0700))==-1){
		perror("Error in shamget with IPC_CREAT\n");
		exit(1);
	}
	
	// Attach shared memory
	if((shared_var=shmat(shmid,NULL,0))<=0){
		perror("Error in shmat\n");
		exit(1);
	}

	shmctl(shmid,IPC_RMID,NULL);