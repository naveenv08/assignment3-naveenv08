#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>

#include <sys/types.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <pthread.h>
#include <stdbool.h>
#include <sys/queue.h>

#include <time.h>
#include <errno.h>

#ifdef USE_AESD_CHAR_DEVICE
#include <sys/ioctl.h>
#include "../aesd-char-driver/aesd_ioctl.h"
#endif

#ifdef USE_AESD_CHAR_DEVICE
#define DATAFILE "/dev/aesdchar"
#else
#define DATAFILE "/var/tmp/aesdsocketdata"
#endif

volatile sig_atomic_t exit_requested = 0;

int listenfd = -1;

struct thread_data
{
    pthread_t thread_id;
    int clientfd;
    struct sockaddr_in client_addr;
    bool thread_complete;

    SLIST_ENTRY(thread_data) entries;
};

SLIST_HEAD(thread_list, thread_data);

struct thread_list thread_head;

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

timer_t timerid;

void *client_thread(void *arg);

void timer_thread(union sigval sigval);

void signal_handler(int signo)
{
	//syslog(LOG_DEBUG, "Caught signal, exiting");
	exit_requested = 1;
	
	if(listenfd != -1)
	{
		close(listenfd);
		listenfd = -1;
    	}
	
	//if(listenfd != -1)
	//	shutdown(listenfd, SHUT_RDWR);
	//	close(listenfd);
}

int create_server_socket(void)
{
    int sockfd;
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if(sockfd == -1)
    {
        perror("socket");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9000);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    
    int enable =1;
    
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1)
    {
    	perror("setsockopt");
    	close(sockfd);
    	return -1;
    }

    if(bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("bind");
        close(sockfd);
        return -1;
    }

    if(listen(sockfd, 5) == -1)
    {
        perror("listen");
        close(sockfd);
        return -1;
    }

    return sockfd;
}


int accept_client(int listenfd, struct sockaddr_in *client_addr)
{
    socklen_t client_len = sizeof(*client_addr);

    return accept(listenfd, (struct sockaddr *)client_addr, &client_len);
}



char *receive_packet(int connfd, size_t *packet_size)
{
    char recv_buffer[1024];

    char *packet_buffer = NULL;

    *packet_size = 0;

    while(1)
    {
        ssize_t bytes_received;

        bytes_received = recv(connfd, recv_buffer, sizeof(recv_buffer), 0);

        if(bytes_received <= 0)
        {
            free(packet_buffer);
            return NULL;
        }

        char *temp = realloc(packet_buffer, *packet_size + bytes_received + 1);

        if(temp == NULL)
        {
            free(packet_buffer);
            return NULL;
        }

        packet_buffer = temp;

        memcpy(packet_buffer + *packet_size, recv_buffer, bytes_received);

        *packet_size += bytes_received;

        packet_buffer[*packet_size] = '\0';

        if(strchr(packet_buffer, '\n') != NULL)
        {
            break;
        }
    }

    return packet_buffer;
}



int append_to_file(const char *data, size_t size)
{
    int fd;

    fd = open(DATAFILE, O_CREAT | O_WRONLY | O_APPEND, 0644);

    if(fd == -1)
    {
        perror("open");
        return -1;
    }

    if(write(fd, data, size) == -1)
    {
        perror("write");
        close(fd);
        return -1;
    }

    close(fd);

    return 0;
}



int send_file_contents(int connfd)
{
    int fd;

    char buffer[1024];

    fd = open(DATAFILE, O_RDONLY);

    if(fd == -1)
    {
        perror("open");
        return -1;
    }

    ssize_t bytes_read;

    while((bytes_read = read(fd, buffer, sizeof(buffer))) > 0)
    {
    	ssize_t byte_sent;
    	
        byte_sent = send(connfd, buffer, bytes_read, 0);
        
        if(byte_sent == -1)
        {
            perror("send");
            return -1;
        }
    }

    close(fd);

    return 0;
}


void *client_thread(void *arg)
{
    struct thread_data *thread_param = (struct thread_data *)arg;

    size_t packet_size;

    char *packet = receive_packet(thread_param->clientfd, &packet_size);

	#ifdef USE_AESD_CHAR_DEVICE
    	struct aesd_seekto seekto;
    	bool ioctl_cmd = false;

if (packet != NULL)
{
    printf("Received packet: [%s]\n", packet);

    int ret = sscanf(packet,
                     "AESDCHAR_IOCSEEKTO:%u,%u",
                     &seekto.write_cmd,
                     &seekto.write_cmd_offset);

    printf("sscanf returned %d, cmd=%u, offset=%u\n",
           ret,
           seekto.write_cmd,
           seekto.write_cmd_offset);

    if (ret == 2)
    {
        ioctl_cmd = true;
    }
}

	#endif

	if(packet != NULL)
	{
    	
		pthread_mutex_lock(&file_mutex);

		#ifdef USE_AESD_CHAR_DEVICE

		if (ioctl_cmd)
		{
    			printf("Executing ioctl\n");

    			int fd = open(DATAFILE, O_RDWR);

    			if (fd >= 0)
    			{
        			int rc = ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto);

        			printf("ioctl returned %d, errno=%d\n", rc, errno);

        			close(fd);
    			}
    			else
    			{
        			printf("Failed to open %s\n", DATAFILE);
    			}
		}
		else
		{
    			append_to_file(packet, packet_size);
		}

		#else

		append_to_file(packet, packet_size);

		#endif

		send_file_contents(thread_param->clientfd);

		pthread_mutex_unlock(&file_mutex);

    		free(packet);
	}

    syslog(LOG_DEBUG, "Closed connection from %s",
           inet_ntoa(thread_param->client_addr.sin_addr));

    close(thread_param->clientfd);

    thread_param->thread_complete = true;

    return NULL;
}


void timer_thread(union sigval sigval)
{
    (void)sigval;

    time_t current_time = time(NULL);

    struct tm *time_info = localtime(&current_time);

    if(time_info == NULL)
    {
        return;
    }

    char timestamp[128];

    if(strftime(timestamp,
                sizeof(timestamp),
                "timestamp:%a, %d %b %Y %H:%M:%S %z\n",
                time_info) == 0)
    {
        return;
    }

    pthread_mutex_lock(&file_mutex);

    append_to_file(timestamp, strlen(timestamp));

    pthread_mutex_unlock(&file_mutex);
}


void cleanup(void)
{
    #ifndef USE_AESD_CHAR_DEVICE
    remove(DATAFILE);
    #endif
}




void daemonize(void)
{
	pid_t pid;
	
	pid = fork();
	
	if (pid < 0)
	{
		perror("fork");
		exit(EXIT_FAILURE);
	}
	
	if(pid > 0)
	{
		exit(EXIT_SUCCESS);
	}
	
	if(setsid() == -1)
	{
		perror("setsid");
		exit(EXIT_FAILURE);
	}
	
	if(chdir("/") == -1)
	{
		perror("chdir");
		exit(EXIT_FAILURE);
	}
	
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
}




int main(int argc, char *argv[])
{

    if(argc == 2 && strcmp(argv[1], "-d") == 0)
    	daemonize();

    openlog(NULL, 0, LOG_USER);

    SLIST_INIT(&thread_head);

    #ifndef USE_AESD_CHAR_DEVICE
    remove(DATAFILE);
    #endif    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    listenfd = create_server_socket();
    
    if(listenfd == -1)
        return -1;

#if 0

struct sigevent sev;
struct itimerspec its;

memset(&sev, 0, sizeof(sev));

sev.sigev_notify = SIGEV_THREAD;
sev.sigev_notify_function = timer_thread;

if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1)
{
    perror("timer_create");

    close(listenfd);

    closelog();

    return -1;
}

its.it_value.tv_sec = 10;
its.it_value.tv_nsec = 0;

its.it_interval.tv_sec = 10;
its.it_interval.tv_nsec = 0;

if (timer_settime(timerid, 0, &its, NULL) == -1)
{
    perror("timer_settime");

    timer_delete(timerid);

    close(listenfd);

    closelog();

    return -1;
}

#endif

    while(!exit_requested)
    {
	struct thread_data *thread_param;

        struct sockaddr_in client_addr;

        int connfd = accept_client(listenfd, &client_addr);

        if(connfd == -1)
        {
            //perror("accept");
            
       	    if(exit_requested)
       	    	break;
       	    	
       	    continue;
        }
        
        syslog(LOG_DEBUG, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr) );


	thread_param = malloc(sizeof(struct thread_data));

	if(thread_param == NULL)
	{
    	close(connfd);
    	continue;
	}

	thread_param->clientfd = connfd;
	thread_param->client_addr = client_addr;
	thread_param->thread_complete = false;

	SLIST_INSERT_HEAD(&thread_head, thread_param, entries);
	
	if(pthread_create(&thread_param->thread_id, NULL, client_thread, thread_param) != 0)
	{
    	syslog(LOG_ERR, "pthread_create failed");

    	SLIST_REMOVE(&thread_head, thread_param, thread_data, entries);

    	close(connfd);
    	free(thread_param);
    	continue;
	}

	
	struct thread_data *curr = SLIST_FIRST(&thread_head);

	while (curr != NULL)
	{
    		struct thread_data *next = SLIST_NEXT(curr, entries);

    		if (curr->thread_complete)
    		{
        		pthread_join(curr->thread_id, NULL);

        		SLIST_REMOVE(&thread_head, curr, thread_data, entries);

        		free(curr);
    		}
	
    		curr = next;
	}
	

    }


	while (!SLIST_EMPTY(&thread_head))
	{
    	struct thread_data *thread = SLIST_FIRST(&thread_head);

    	pthread_join(thread->thread_id, NULL);

    	SLIST_REMOVE_HEAD(&thread_head, entries);

    	free(thread);
	}

	syslog(LOG_DEBUG, "Caught signal, exiting");

	//timer_delete(timerid);

	cleanup();

	pthread_mutex_destroy(&file_mutex);

	closelog();

    return 0;
}
