#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <math.h>
#include <vector>
#include <sstream>
#include "Parse.h"

using namespace std;

#define SOCKET_ERROR        -1
#define BUFFER_SIZE         100
#define HOST_NAME_SIZE      255
#define MAX_GET             1000
#define NCONNECTIONS	    20

int main(int argc, char *argv[])
{
	struct hostent *pHostInfo;   /* holds info about a machine */
	struct sockaddr_in Address;  /* Internet socket address stuct */
	long nHostAddress;
	char pBuffer[BUFFER_SIZE];
	unsigned nReadAmount;
	char strHostName[HOST_NAME_SIZE];	
	int* contentLength;
	int nHostPort;
	int headerNum;
	int count =0;
	int c;
	bool dFlag = false;
	char *path;
	char contentType[MAX_MSG_SZ];
	vector<char *> headerLines;
	
	if (argc < 5 || argc > 6)
	{
		printf("\nUsage: webtest host port path [-d(for downloads)] <count>\n");
		return 0;
	}
	else
	{
		while ((c = getopt(argc, argv, "d")) != -1)
		{
			switch (c)
			{
				case 'd':
					dFlag = true;
					break;
				default:
					perror("\nUsage: webtest host port path [-d(for downloads)] <count>\n");
					return -1;
			}
		}
		//if there aren't 4 arguments after the option there is an error
		if (argc - optind != 4)
		{
			perror("\nUsage: webtest host port path [-d(for downloads)] <count>\n");
		}
		
		//copy hostname from arugments
		strcpy(strHostName, argv[optind++]);
		//copy portname from arguments
		std::string port = argv[optind++];
		//make sure port is only digits and then copy
		if (port.find_first_not_of("0123456789") != string::npos)
		{
			perror("Port must only contain numbers.\n");
			return -1;
		}
		else
		{
			nHostPort = atoi(&port[0]);
		}
		
		//copy path from arugments
		path = argv[optind++];
		count = atoi(argv[optind++]);
	}

	int hSockets[count];
	double times[count];
	struct timeval oldtime[count];
	struct timeval newtime[count];
	struct epoll_event event[count];
	struct epoll_event events[count];

	int epollFD = epoll_create(count);

	//
	for (int i = 0; i < count; i++)
	{
		/* make a socket */
		hSockets[i]= socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (hSockets[i] == SOCKET_ERROR)
		{
			perror("\nCould not make a socket.\n");
			return -1;
		}
		/* get IP address from name */
		pHostInfo = gethostbyname(strHostName);
		if (pHostInfo == NULL)
		{
			perror("\nCouldn't connect to host, because of IP address.\n");
			return -1;
		}
		/* copy address into long */
		memcpy(&nHostAddress, pHostInfo->h_addr, pHostInfo->h_length);
		/* fill address struct */	
		Address.sin_addr.s_addr = nHostAddress;
		Address.sin_port = htons(nHostPort);
		Address.sin_family = AF_INET;
		/* connect to host */
		if (connect(hSockets[i], (struct sockaddr *) &Address, sizeof(Address)) == SOCKET_ERROR)
		{
			perror("\nCould not connect to host, because of Socket error\n");
			return -1;
		}
	
		// Create HTTP Message
		char *message = (char *) malloc(MAX_GET);
		sprintf(message, "GET %s HTTP/1.1\r\nHost:%s:%d\r\n\r\n", path, strHostName, nHostPort);
		//write a request to server
		write(hSockets[i], message, strlen(message));
		free(message);
		//Keep track of the time when we sent the request
		gettimeofday(&oldtime[i], NULL);
		
		//Tell epoll that we want to know when this socket has data	
		event[i].events = EPOLLIN;
		event[i].data.fd = hSockets[i];
		int ret = epoll_ctl(epollFD, EPOLL_CTL_ADD,hSockets[i], &event[i]);
		if (ret)
		{
			perror("epoll_ctl failed\n");
			return 0;
		}
		
	}
	int actual_read =0;
	while (actual_read < count)
	{
		int rval = epoll_wait(epollFD, events, count, -1);

		for (int i =0; i < rval; i++)
		{
			// Read the header lines	
			GetHeaderLines(headerLines, events[i].data.fd, false);
			char* responseBuffer;
			for (int j=0; j < headerLines.size();j++)
			{
				if (strstr(headerLines[j], "Content-Length"))
				{
					contentLength = (int*)malloc(64);
					sscanf(headerLines[j], "Content-Length: %d", contentLength);
					responseBuffer = (char*)malloc(*contentLength * sizeof(char));
				}
			}
			int mainrval;
			if ((mainrval = read(events[i].data.fd, responseBuffer, *contentLength)) > 0)
			{
				int timePosition = event[count-1].data.fd-events[i].data.fd;
				//Get the current time and subtract the starting time for this request.
				gettimeofday(&newtime[timePosition],NULL);
				//Got the time differences
				double usec = (newtime[timePosition].tv_sec - oldtime[timePosition].tv_sec)*(double)1000000+(newtime[timePosition].tv_usec-oldtime[timePosition].tv_usec);
				//Put the differences in seconds
				times[timePosition]=usec/1000000;
				if (dFlag)
				{
					std::cout << "Response time is: " << times[timePosition] << " seconds" << std::endl;
				}
			}
			else
			{
				perror("Error in reading response");
				return -1;
			}
			
			// Now read and print the rest of the file
		
	
			if (close(events[i].data.fd) == SOCKET_ERROR)
			{
				perror("\nCould not close socket.\n");
				return -1;
			}
	        	free(responseBuffer);
			headerNum = headerLines.size();
			for (int j = 0; j < headerNum; j++)
			{
				free(headerLines[j]);
			}
			for (int j = 0; j < headerNum; j++)
			{
				headerLines.erase(headerLines.begin());
			}
			actual_read++;	
		}
	}
	double totaltime =0;
	for (int i =0; i < count; i++)
	{
		totaltime = totaltime+times[i];
	}
	double avgResp = totaltime/count;
	double variance =0;
	for (int i = 0; i < count; i++)
	{
		double difference = (times[i] - avgResp);
		variance = variance+difference*difference;
	}
	variance = variance/count;
	double stdDev = sqrt(variance);
	std::cout << "Average Response Time: " << avgResp << " seconds" << std::endl;	
	std::cout << "Standard Deviation: " << stdDev << " seconds" << std::endl;
	return 0;
}
