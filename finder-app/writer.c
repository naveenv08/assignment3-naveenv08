#include<stdio.h>
#include<fcntl.h>
#include<unistd.h>
#include<syslog.h>
#include<string.h>

int main(int argc, char **argv)
{
	if(argc < 3)
	{
		printf("Need Two Parameters\n Usage: %s PATH_TO_FILE STRING", argv[0]);
		return 1;
	}

	openlog(NULL, 0, LOG_USER);

	syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);

	int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
		
	if(fd == -1)
	{
		syslog(LOG_ERR, "Error opening File");

		closelog();

		printf("Open Failed");

		return 1;
	}

	int ret = write(fd, argv[2], strlen(argv[2]) );

	if(ret == -1)
	{
		syslog(LOG_ERR, "Error writing to File");

		close(fd);

		closelog();

		return 1;
	}


	close(fd);

	closelog();

	return 0;

}
