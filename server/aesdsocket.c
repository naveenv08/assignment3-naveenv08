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

#define DATAFILE "/var/tmp/aesdsocketdata"

volatile sig_atomic_t exit_requested = 0;

int listenfd = -1;


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



void cleanup(void)
{
    //if(listenfd != -1)
    //{
    //    close(listenfd);
    //   listenfd = -1;
    //}

    remove(DATAFILE);
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
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    listenfd = create_server_socket();
    
    if(listenfd == -1)
        return -1;

    while(!exit_requested)
    {
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

        size_t packet_size;

        char *packet = receive_packet(connfd, &packet_size);

        if(packet != NULL)
        {
            append_to_file(packet, packet_size);

            send_file_contents(connfd);

            free(packet);
        }

	
	syslog(LOG_DEBUG, "Closed connection from %s", inet_ntoa(client_addr.sin_addr) );

        close(connfd);
    }

    syslog(LOG_DEBUG, "Caught signal, exiting");
    cleanup();
    
    closelog();

    return 0;
}
