#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h> 
#define PORT 5000
#define TRAIN "./db/train"
#define BOOKING "./db/booking"
#define PASS_LENGTH 30
struct account{
	int id;
	char name[10];
	char pass[PASS_LENGTH];
};

struct train{
	int tid;
	char train_name[20];
	int train_no;
	int av_seats;
	int last_seatno_used;
};

struct bookings{
	int bid;
	int type;
	int acc_no;
	int tr_id;
	char trainname[20];
	int seat_start;
	int seat_end;
	int cancelled;
};

char *ACC[3] = {"./db/accounts/customer", "./db/accounts/agent", "./db/accounts/admin"};

void talk_to_client(int sock);
int login(int sock);
int signup(int sock);
int menu2(int sock, int id);
int menu1(int sock, int id, int type);
void view_booking(int sock, int id, int type);
void view_booking2(int sock, int id, int type, int fd);
void sighandler(int sig);

void sighandler(int sig) {
	exit(0);
	return;
}

int main(){
	
	signal(SIGTSTP, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGQUIT, sighandler);
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd==-1) {
		printf("socket creation failed\n");
		exit(0);
	}
	int optval = 1;
	int optlen = sizeof(optval);
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval, optlen)==-1){
		printf("set socket options failed\n");
		exit(0);
	}
	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(PORT);

	if(bind(sockfd, (struct sockaddr *)&sa, sizeof(sa))==-1){
		printf("binding port failed\n");
		exit(0);
	}
	if(listen(sockfd, 100)==-1){
		printf("listen failed\n");
		exit(0);
	}
	while(1){ 
		int connectedfd;
		if((connectedfd = accept(sockfd, (struct sockaddr *)NULL, NULL))==-1){
			printf("connection error\n");
			exit(0);
		}
		pthread_t cli;
		if(fork()==0)
			talk_to_client(connectedfd);
	}

	close(sockfd);
	return 0;
}

void talk_to_client(int sock){
	int func_id;
	read(sock, &func_id, sizeof(int));
	printf("Client [%d] connected\n", sock);
	while(1){		
		if(func_id==1) {login(sock);read(sock, &func_id, sizeof(int));}
		if(func_id==2) {signup(sock);read(sock, &func_id, sizeof(int));}
		if(func_id==3) break;
	}
	close(sock);
	printf("Client [%d] disconnected\n", sock);
}

int login(int sock){
	int type, acc_no, fd, valid=1, invalid=0, login_success=0;
	char password[PASS_LENGTH];
	struct account temp;
	read(sock, &type, sizeof(type));
	read(sock, &acc_no, sizeof(acc_no));
	read(sock, &password, sizeof(password));

	if((fd = open(ACC[type-1], O_RDWR))==-1)printf("File Error\n");
	struct flock lock;
	
	lock.l_start = (acc_no-1)*sizeof(struct account);
	lock.l_len = sizeof(struct account);
	lock.l_whence = SEEK_SET;
	lock.l_pid = getpid();

	if(type == 1){// User
		lock.l_type = F_WRLCK;
		fcntl(fd,F_SETLK, &lock);
		lseek(fd, (acc_no - 1)*sizeof(struct account), SEEK_CUR);
		read(fd, &temp, sizeof(struct account));
		if(temp.id == acc_no){
			if(!strcmp(temp.pass, password)){
				write(sock, &valid, sizeof(valid));
				while(-1!=menu1(sock, temp.id, type));
				login_success = 1;
			}
		}
		lock.l_type = F_UNLCK;
		fcntl(fd, F_SETLK, &lock);
		close(fd);
		if(login_success)
		return 3;
	}
	else if(type == 2){// Agent
		//sem_t mutex; //counting semaphore for Agent login
		//sem_init(&mutex, 0, 5);
		
		lock.l_type = F_RDLCK;
		//sem_wait(&mutex);
		fcntl(fd,F_SETLK, &lock);
		lseek(fd, (acc_no - 1)*sizeof(struct account), SEEK_SET);
		read(fd, &temp, sizeof(struct account));
		if(temp.id == acc_no){
			if(!strcmp(temp.pass, password)){
				write(sock, &valid, sizeof(valid));
				while(-1!=menu1(sock, temp.id, type));
				login_success = 1;
			}
		}
		lock.l_type = F_UNLCK;
		fcntl(fd, F_SETLK, &lock);
		close(fd);
		//sem_post(&mutex);
		if(login_success) return 3;
		
/*		
		read(fd, &temp, sizeof(struct account));
		if(temp.id == acc_no){
			if(!strcmp(temp.pass, password)){
				write(sock, &valid, sizeof(valid));
				close(fd);
				return 3;
			}
		}
		close(fd);
*/	
	}
	else if(type == 3){
		// Admin
		lock.l_type = F_WRLCK;
		int ret = fcntl(fd,F_SETLK, &lock);
		if(ret != -1){
			
			lseek(fd, (acc_no - 1)*sizeof(struct account), SEEK_SET);///////CUR
			read(fd, &temp, sizeof(struct account));
			if(temp.id == acc_no){
				if(!strcmp(temp.pass, password)){
					write(sock, &valid, sizeof(valid));
					while(-1!=menu2(sock, temp.id));
					login_success = 1;
				}
			}
			lock.l_type = F_UNLCK;
			fcntl(fd, F_SETLK, &lock);
		}
		close(fd);
		if(login_success)
		return 3;
	}
	write(sock, &invalid, sizeof(invalid));
	return 3;
}

int signup(int sock){
	int type, fd, acc_no=0;
	char password[PASS_LENGTH], name[10];
	struct account temp;

	read(sock, &type, sizeof(type));
	read(sock, &name, sizeof(name));
	read(sock, &password, sizeof(password));

	if((fd = open(ACC[type-1], O_RDWR))==-1)printf("File Error\n");
	struct flock lock;
	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_whence = SEEK_SET;
	lock.l_pid = getpid();

	fcntl(fd,F_SETLKW, &lock);

	int fp = lseek(fd, 0, SEEK_END);

	if(fp==0){//1st signup
		temp.id = 1;
		strcpy(temp.name, name);
		strcpy(temp.pass, password);
		write(fd, &temp, sizeof(temp));
		write(sock, &temp.id, sizeof(temp.id));
	}
	else{
		fp = lseek(fd, -1 * sizeof(struct account), SEEK_END);
		read(fd, &temp, sizeof(temp));
		temp.id++;
		strcpy(temp.name, name);
		strcpy(temp.pass, password);
		write(fd, &temp, sizeof(temp));
		write(sock, &temp.id, sizeof(temp.id));
	}

	lock.l_type = F_UNLCK;
	fcntl(fd, F_SETLK, &lock);

	close(fd);
	return 3;
}

int menu2(int sock, int id){
	int op_id;
	read(sock, &op_id, sizeof(op_id));
	if(op_id == 1){
		//add a train
		int tid = 0;
		int tno; 
		char tname[20];
		read(sock, &tname, sizeof(tname));
		read(sock, &tno, sizeof(tno));
		struct train temp, temp2;

		temp.tid = tid;
		temp.train_no = tno;
		strcpy(temp.train_name, tname);
		temp.av_seats = 15;
		temp.last_seatno_used = 0;

		int fd = open(TRAIN, O_RDWR);
		struct flock lock;
		lock.l_type = F_WRLCK;
		lock.l_start = 0;
		lock.l_len = 0;
		lock.l_whence = SEEK_SET;
		lock.l_pid = getpid();

		fcntl(fd, F_SETLKW, &lock);

		int fp = lseek(fd, 0, SEEK_END);
		if(fp == 0){
			write(fd, &temp, sizeof(temp));
			lock.l_type = F_UNLCK;
			fcntl(fd, F_SETLK, &lock);
			close(fd);
			write(sock, &op_id, sizeof(op_id));
		}
		else{
			lseek(fd, -1 * sizeof(struct train), SEEK_CUR);
			read(fd, &temp2, sizeof(temp2));
			temp.tid = temp2.tid + 1;
			write(fd, &temp, sizeof(temp));
			write(sock, &op_id, sizeof(op_id));	
			lock.l_type = F_UNLCK;
			fcntl(fd, F_SETLK, &lock);
			close(fd);
		}
		return op_id;
	}
	if(op_id == 2){
		int fd = open(TRAIN, O_RDWR);

		struct flock lock;
		lock.l_type = F_WRLCK;
		lock.l_start = 0;
		lock.l_len = 0;
		lock.l_whence = SEEK_SET;
		lock.l_pid = getpid();
		
		fcntl(fd, F_SETLKW, &lock);

		int fp = lseek(fd, 0, SEEK_END);
		int no_of_trains = fp / sizeof(struct train);
		printf("no of train:%d\n",no_of_trains);
		write(sock, &no_of_trains, sizeof(int));
		lseek(fd, 0, SEEK_SET);
		struct train temp;
		while(fp != lseek(fd, 0, SEEK_CUR)){
			printf("FP :%d  FD :%ld\n",fp,lseek(fd, 0, SEEK_CUR));
			read(fd, &temp, sizeof(struct train));
			write(sock, &temp.tid, sizeof(int));
			write(sock, &temp.train_name, sizeof(temp.train_name));
			write(sock, &temp.train_no, sizeof(int));			
		}
		//int train_id=-1;
		read(sock, &no_of_trains, sizeof(int));
		if(no_of_trains != -2) //write(sock, &no_of_trains, sizeof(int));
		{
			struct train temp;
			//lseek(fd, 0, SEEK_SET);
			lseek(fd, (no_of_trains)*sizeof(struct train), SEEK_SET);
			read(fd, &temp, sizeof(struct train));			
			printf("%s is deleted\n", temp.train_name);
			strcpy(temp.train_name,"deleted");
			lseek(fd, -1*sizeof(struct train), SEEK_CUR);
			write(fd, &temp, sizeof(struct train));
			//write(sock, &no_of_trains, sizeof(int));
		}
		write(sock, &no_of_trains, sizeof(int));
		lock.l_type = F_UNLCK;
		fcntl(fd, F_SETLK, &lock);
		close(fd);
	}
	if(op_id == 3){
		int fd = open(TRAIN, O_RDWR);

		struct flock lock;
		lock.l_type = F_WRLCK;
		lock.l_start = 0;
		lock.l_len = 0;
		lock.l_whence = SEEK_SET;
		lock.l_pid = getpid();
		
		fcntl(fd, F_SETLKW, &lock);

		int fp = lseek(fd, 0, SEEK_END);
		int no_of_trains = fp / sizeof(struct train);
		write(sock, &no_of_trains, sizeof(int));
		lseek(fd, 0, SEEK_SET);
		while(fp != lseek(fd, 0, SEEK_CUR)){
			struct train temp;
			read(fd, &temp, sizeof(struct train));
			write(sock, &temp.tid, sizeof(int));
			write(sock, &temp.train_name, sizeof(temp.train_name));
			write(sock, &temp.train_no, sizeof(int));			
		}
		read(sock, &no_of_trains, sizeof(int));

		struct train temp;
		lseek(fd, 0, SEEK_SET);
		lseek(fd, (no_of_trains-1)*sizeof(struct train), SEEK_CUR);
		read(fd, &temp, sizeof(struct train));			

		read(sock, &no_of_trains,sizeof(int));
		if(no_of_trains == 1){
			char name[20];
			write(sock, &temp.train_name, sizeof(temp.train_name));
			read(sock, &name, sizeof(name));
			strcpy(temp.train_name, name);
		}
		else if(no_of_trains == 2){
			write(sock, &temp.train_no, sizeof(temp.train_no));
			read(sock, &temp.train_no, sizeof(temp.train_no));
		}
		else{
			write(sock, &temp.av_seats, sizeof(temp.av_seats));
			read(sock, &temp.av_seats, sizeof(temp.av_seats));
		}

		no_of_trains = 3;
		printf("%s\t%d\t%d\n", temp.train_name, temp.train_no, temp.av_seats);
		lseek(fd, -1*sizeof(struct train), SEEK_CUR);
		write(fd, &temp, sizeof(struct train));
		write(sock, &no_of_trains, sizeof(int));

		lock.l_type = F_UNLCK;
		fcntl(fd, F_SETLK, &lock);
		close(fd);
		return op_id;
	}
	if(op_id == 4){
		int type=3, fd, acc_no=0;
		char password[PASS_LENGTH], name[10];
		struct account temp;
		read(sock, &name, sizeof(name));
		read(sock, &password, sizeof(password));

		if((fd = open(ACC[type-1], O_RDWR))==-1)printf("File Error\n");
		struct flock lock;
		lock.l_type = F_WRLCK;
		lock.l_start = 0;
		lock.l_len = 0;
		lock.l_whence = SEEK_SET;
		lock.l_pid = getpid();

		fcntl(fd,F_SETLKW, &lock);
		int fp = lseek(fd, 0, SEEK_END);
		fp = lseek(fd, -1 * sizeof(struct account), SEEK_CUR);
		read(fd, &temp, sizeof(temp));
		temp.id++;
		strcpy(temp.name, name);
		strcpy(temp.pass, password);
		write(fd, &temp, sizeof(temp));
		write(sock, &temp.id, sizeof(temp.id));
		lock.l_type = F_UNLCK;
		fcntl(fd, F_SETLK, &lock);

		close(fd);
		op_id=4;
		write(sock, &op_id, sizeof(op_id));
		return op_id;
	}
	if(op_id == 5){
		int type, id;
		struct account var;
		read(sock, &type, sizeof(type));

		int fd = open(ACC[type - 1], O_RDWR);
		struct flock lock;
		lock.l_type = F_WRLCK;
		lock.l_start = 0;
		lock.l_whence = SEEK_SET;
		lock.l_len = 0;
		lock.l_pid = getpid();

		fcntl(fd, F_SETLKW, &lock);

		int fp = lseek(fd, 0 , SEEK_END);
		int users = fp/ sizeof(struct account);
		write(sock, &users, sizeof(int));

		lseek(fd, 0, SEEK_SET);
		while(fp != lseek(fd, 0, SEEK_CUR)){
			read(fd, &var, sizeof(struct account));
			write(sock, &var.id, sizeof(var.id));
			write(sock, &var.name, sizeof(var.name));
		}

		read(sock, &id, sizeof(id));
		if(id == 0){write(sock, &op_id, sizeof(op_id));}
		else{
			lseek(fd, 0, SEEK_SET);
			lseek(fd, (id-1)*sizeof(struct account), SEEK_CUR);
			read(fd, &var, sizeof(struct account));
			lseek(fd, -1*sizeof(struct account), SEEK_CUR);
			strcpy(var.name,"deleted");
			strcpy(var.pass, "");
			write(fd, &var, sizeof(struct account));
			write(sock, &op_id, sizeof(op_id));
		}

		lock.l_type = F_UNLCK;
		fcntl(fd, F_SETLK, &lock);

		close(fd);

		return op_id;
	}
	if(op_id == 6) {
		write(sock,&op_id, sizeof(op_id));
		return -1;
	}
}

int menu1(int sock, int id, int type){
	int op_id;
	read(sock, &op_id, sizeof(op_id));
	if(op_id == 1){
		//book a ticket
		int fd = open(TRAIN, O_RDWR);

		struct flock lock;
		lock.l_type = F_WRLCK;
		lock.l_start = 0;
		lock.l_len = 0;
		lock.l_whence = SEEK_SET;
		lock.l_pid = getpid();
		
		fcntl(fd, F_SETLKW, &lock);

		struct train temp;
		int fp = lseek(fd, 0, SEEK_END);
		int no_of_trains = fp / sizeof(struct train);
		write(sock, &no_of_trains, sizeof(int));
		lseek(fd, 0, SEEK_SET);
		while(fp != lseek(fd, 0, SEEK_CUR)){
			read(fd, &temp, sizeof(struct train));
			write(sock, &temp.tid, sizeof(int));
			write(sock, &temp.train_no, sizeof(int));	
			write(sock, &temp.av_seats, sizeof(int));	
			write(sock, &temp.train_name, sizeof(temp.train_name));		
		}
		//struct train temp1;
		memset(&temp,0,sizeof(struct train));
		int trainid, seats;
		read(sock, &trainid, sizeof(trainid));
		//lseek(fd, 0, SEEK_SET);
		lseek(fd, trainid*sizeof(struct train), SEEK_SET);
		read(fd, &temp, sizeof(struct train));
		write(sock, &temp.av_seats, sizeof(int));
		read(sock, &seats, sizeof(seats));
		if(seats>0){
			temp.av_seats -= seats;
			int fd2 = open(BOOKING, O_RDWR);
			fcntl(fd2, F_SETLKW, &lock);
			struct bookings bk;
			int fp2 = lseek(fd2, 0, SEEK_END);
			if(fp2 > 0)
			{
				lseek(fd2, -1*sizeof(struct bookings), SEEK_CUR);
				read(fd2, &bk, sizeof(struct bookings));
				bk.bid++;
			}
			else 
				bk.bid = 0;
			bk.type = type;
			bk.acc_no = id;
			bk.tr_id = trainid;
			bk.cancelled = 0;
			strcpy(bk.trainname, temp.train_name);
			bk.seat_start = temp.last_seatno_used + 1;
			bk.seat_end = temp.last_seatno_used + seats;
			temp.last_seatno_used = bk.seat_end;
			write(fd2, &bk, sizeof(bk));
			lock.l_type = F_UNLCK;
			fcntl(fd2, F_SETLK, &lock);
		 	close(fd2);
			lseek(fd, -1*sizeof(struct train), SEEK_CUR);
			write(fd, &temp, sizeof(temp));
		}
		fcntl(fd, F_SETLK, &lock);
	 	close(fd);

		if(seats<=0)
			op_id = -1;
		write(sock, &op_id, sizeof(op_id));
		return 1;
	}
	if(op_id == 2){
		view_booking(sock, id, type);
		write(sock, &op_id, sizeof(op_id));
		return 2;
	}
	if(op_id == 3){
		//update booking
		view_booking(sock, id, type);

		int fd1 = open(TRAIN, O_RDWR);
		int fd2 = open(BOOKING, O_RDWR);
		struct flock lock;
		lock.l_type = F_WRLCK;
		lock.l_start = 0;
		lock.l_len = 0;
		lock.l_whence = SEEK_SET;
		lock.l_pid = getpid();

		fcntl(fd1, F_SETLKW, &lock);
		fcntl(fd2, F_SETLKW, &lock);

		int val;
		struct train temp1;
		struct bookings temp2;
		read(sock, &val, sizeof(int));	
		lseek(fd2, 0, SEEK_SET);
		lseek(fd2, val*sizeof(struct bookings), SEEK_CUR);
		read(fd2, &temp2, sizeof(temp2));
		lseek(fd2, -1*sizeof(struct bookings), SEEK_CUR);
		printf("%d %s %d\n", temp2.tr_id, temp2.trainname, temp2.seat_end);
	
		lseek(fd1, 0, SEEK_SET);
		lseek(fd1, (temp2.tr_id-1)*sizeof(struct train), SEEK_CUR);
		read(fd1, &temp1, sizeof(temp1));
		lseek(fd1, -1*sizeof(struct train), SEEK_CUR);
		printf("%d %s %d\n", temp1.tid, temp1.train_name, temp1.av_seats);


		read(sock, &val, sizeof(int));	


		if(val==1){
			read(sock, &val, sizeof(int)); 
			if(temp1.av_seats>= val){
				temp2.cancelled = 1;
				temp1.av_seats += val;
				write(fd2, &temp2, sizeof(temp2));

				int tot_seats = temp2.seat_end - temp2.seat_start + 1 + val;
				struct bookings bk;

				int fp2 = lseek(fd2, 0, SEEK_END);
				lseek(fd2, -1*sizeof(struct bookings), SEEK_CUR);
				read(fd2, &bk, sizeof(struct bookings));

				bk.bid++;
				bk.type = temp2.type;
				bk.acc_no = temp2.acc_no;
				bk.tr_id = temp2.tr_id;
				bk.cancelled = 0;
				strcpy(bk.trainname, temp2.trainname);
				bk.seat_start = temp1.last_seatno_used + 1;
				bk.seat_end = temp1.last_seatno_used + tot_seats;

				temp1.av_seats -= tot_seats;
				temp1.last_seatno_used = bk.seat_end;

				write(fd2, &bk, sizeof(bk));
				write(fd1, &temp1, sizeof(temp1));
			}
			else{
				op_id = -2;
				write(sock, &op_id, sizeof(op_id));
			}
		}
		else{//decrease			
			read(sock, &val, sizeof(int)); //No of Seats
			if(temp2.seat_end - val < temp2.seat_start){
				temp2.cancelled = 1;
				temp1.av_seats += val;
			}
			else{
				temp2.seat_end -= val;
				temp1.av_seats += val;
			}
			write(fd2, &temp2, sizeof(temp2));
			write(fd1, &temp1, sizeof(temp1));
		}
		lock.l_type = F_UNLCK;
		fcntl(fd1, F_SETLK, &lock);
		fcntl(fd2, F_SETLK, &lock);
		close(fd1);
		close(fd2);
		if(op_id>0)
		write(sock, &op_id, sizeof(op_id));
		return 3;

	}
	if(op_id == 4) {
		//cancel booking
		view_booking(sock, id, type);


		struct flock lock;
		lock.l_type = F_WRLCK;
		lock.l_start = 0;
		lock.l_len = 0;
		lock.l_whence = SEEK_SET;
		lock.l_pid = getpid();

		int fd1 = open(TRAIN, O_RDWR);
		int fd2 = open(BOOKING, O_RDWR);
		fcntl(fd1, F_SETLKW, &lock);


		int val;
		struct train temp1;
		struct bookings temp2;
		read(sock, &val, sizeof(int));	
		lseek(fd2, val*sizeof(struct bookings), SEEK_SET);
		

		lock.l_start = val*sizeof(struct bookings);
		lock.l_len = sizeof(struct bookings);
		fcntl(fd2, F_SETLKW, &lock);

		
		read(fd2, &temp2, sizeof(temp2));
		lseek(fd2, -1*sizeof(struct bookings), SEEK_CUR);
		printf("%d %s %d\n", temp2.tr_id, temp2.trainname, temp2.seat_end);


		lseek(fd1, (temp2.tr_id)*sizeof(struct train), SEEK_SET); 
		lock.l_start = (temp2.tr_id)*sizeof(struct train);
		lock.l_len = sizeof(struct train);
		fcntl(fd1, F_SETLKW, &lock);
		read(fd1, &temp1, sizeof(temp1));
		lseek(fd1, -1*sizeof(struct train), SEEK_CUR);
		printf("%d %s %d\n", temp1.tid, temp1.train_name, temp1.av_seats);
		temp1.av_seats += temp2.seat_end - temp2.seat_start + 1;
		temp2.cancelled=1;
		write(fd2, &temp2, sizeof(temp2));
		write(fd1, &temp1, sizeof(temp1));
		
		lock.l_type = F_UNLCK;
		fcntl(fd1, F_SETLK, &lock);
		fcntl(fd2, F_SETLK, &lock);
		close(fd1);
		close(fd2);
		write(sock, &op_id, sizeof(op_id));
		return 4;
	}
	if(op_id == 5) {
		write(sock, &op_id, sizeof(op_id));
		return -1;
	}
	
	return 0;
}

void view_booking(int sock, int id, int type){
	int fd = open(BOOKING, O_RDONLY);
	struct flock lock;
	lock.l_type = F_RDLCK;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_whence = SEEK_SET;
	lock.l_pid = getpid();
	
	fcntl(fd, F_SETLKW, &lock);

	int fp = lseek(fd, 0, SEEK_END);
	int entries = 0;
	if(fp == 0)
		write(sock, &entries, sizeof(entries));
	else{
		struct bookings bk[10];
		while(fp>0 && entries<10){
			struct bookings temp;
			fp = lseek(fd, -1*sizeof(struct bookings), SEEK_CUR);
			read(fd, &temp, sizeof(struct bookings));
			if(temp.acc_no == id && temp.type == type)
				bk[entries++] = temp;
			fp = lseek(fd, -1*sizeof(struct bookings), SEEK_CUR);
		}
		write(sock, &entries, sizeof(entries));
		for(fp=0;fp<entries;fp++){
			write(sock, &bk[fp].bid, sizeof(bk[fp].bid));
			write(sock, &bk[fp].trainname, sizeof(bk[fp].trainname));
			write(sock, &bk[fp].seat_start, sizeof(int));
			write(sock, &bk[fp].seat_end, sizeof(int));
			write(sock, &bk[fp].cancelled, sizeof(int));
		}
	}
	lock.l_type = F_UNLCK;
	fcntl(fd, F_SETLK, &lock);
	close(fd);
}

