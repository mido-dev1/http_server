#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <zlib.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include <pthread.h>

typedef struct
{
	char *endpoint;
	char *user_agent;
	char *str;
	char *filename;
	char *post_data;
	unsigned char method;	// 0 => get // 1 => post
	unsigned char compress; // 0 => no // 1 => yes
} data_t;

void signal_handler(int sig);
void *handle_response(void *arg);
data_t *parse_request(char *buf);
int send_compress(int clientfd, char *response, unsigned char compress, char *input);

int sockfd;
char fullpath[PATH_MAX];
char *basepath = 0;

int main(int argc, char *argv[])
{
	if (argc == 3 && !strcmp(argv[1], "--directory") && *argv[2] != 0)
	{
		// printf("Usage: %s --directory path\n", argv[0]);
		// exit(EXIT_FAILURE);

		// Check path valid
		strcpy(fullpath, argv[2]); // TODO: handle error
		DIR *dir;
		if (!(dir = opendir(fullpath)))
		{
			perror("Open directory failed");
			exit(EXIT_FAILURE);
		}
		closedir(dir);

		// check if path end with /
		{
			int pathlen = strlen(fullpath);
			if (strrchr(fullpath, '/') != fullpath + pathlen - 1)
			{
				fullpath[pathlen] = '/';
				fullpath[pathlen + 1] = 0;
			}
		}

		// save base path for later
		if (!(basepath = (char *)malloc(strlen(fullpath) + 1)))
		{
			perror("Memory Alloction faileds");
			exit(EXIT_FAILURE);
		}
		strcpy(basepath, fullpath);
		printf("Directory: %s\n", basepath);
	}

	// create a socket
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("Socket creation failed");
		free(basepath);
		exit(EXIT_FAILURE);
	}

	// Setup signals
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGABRT, signal_handler);

	// make port reusable
	int reuse = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
	{
		perror("Set socket opt failed");
		signal_handler(0);
	}

	// Client info
	struct sockaddr_in client_addr, addr = {
										AF_INET,
										htons(4221),
										0};
	socklen_t client_addr_len = sizeof(client_addr);

	// bind port to socket
	if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
	{
		perror("Bind failed");
		signal_handler(0);
	}

	// listen for connections
	int backlog = 6;
	if (listen(sockfd, backlog) == -1)
	{
		perror("Listen failed");
		signal_handler(0);
	}

	printf("Waiting for a client to connect...(port: 4221)\n");

	int clientfd;

	// main loop
	while (1)
	{
		// accept connections
		if ((clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len)) == -1)
		{
			perror("Accept failed");
			continue;
		}
		printf("Client connected %d\n", clientfd);

		// store the clientfd in arg to pass it to the thread
		int *arg = (int *)malloc(sizeof(int));
		if (arg == NULL)
		{
			perror("Memory Alloction faileds");
			close(clientfd);
			continue;
		}
		*arg = clientfd;

		// create the thread
		pthread_t tId;
		if (pthread_create(&tId, NULL, handle_response, (void *)arg) != 0)
		{
			perror("Create thread failed");
			close(clientfd);
			free(arg);
			break;
		}
		else
		{
			pthread_detach(tId);
		}
	}

	signal_handler(1);
}

void signal_handler(int sig)
{
	printf("Exiting the program\n");

	free(basepath);
	close(sockfd);
	if (sig)
		exit(EXIT_SUCCESS);
	exit(EXIT_FAILURE);
}

void *handle_response(void *arg)
{
	// free arg for next client
	int clientfd = *(int *)arg;
	free(arg);
	data_t *data;

	char buf[BUFSIZ] = {0};
	if ((recv(clientfd, buf, BUFSIZ - 1, 0)) < 0)
	{
		perror("Failed to receive request");
		close(clientfd);
		return 0;
	}
	if (!strlen(buf))
	{
		printf("Failed to receive request\n");
		close(clientfd);
		return 0;
	}

	// parse the request
	if (!(data = parse_request(buf)))
	{
		close(clientfd);
		return 0;
	}
	char response[BUFSIZ] = {0};
	int e;

	if (!data->endpoint) // invalid
	{
		strcpy(response, "HTTP/1.1 404 Not Found\r\n\r\n");
		// send
		send_compress(clientfd, response, data->compress, data->str);
	}
	else if (data->user_agent) // /user-agent
	{
		e = snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s", strlen(data->user_agent), data->user_agent);
		if (e >= sizeof(response))
		{
			strcpy(response, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
		}
		// send
		send_compress(clientfd, response, data->compress, data->str);
	}
	else if (data->str)
	{
		// TODO:compression
		e = snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s", strlen(data->str), data->str);
		if (e >= sizeof(response))
		{
			strcpy(response, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
		}
		// send
		send_compress(clientfd, response, data->compress, data->str);
	}
	else if (!strcmp(data->endpoint, "/")) // /
	{
		strcpy(response, "HTTP/1.1 200 OK\r\n\r\n");
		send_compress(clientfd, response, data->compress, data->str);
	}
	else if (data->filename && basepath) // /files
	{
		FILE *file;
		strcpy(fullpath, basepath);
		strncat(fullpath, data->filename, PATH_MAX - strlen(fullpath) - 1);
		if (data->post_data) // post file data
		{
			if (!(file = fopen((char *)fullpath, "w")))
			{
				perror("Failed open file");
				snprintf(
					response,
					sizeof(response),
					"HTTP/1.1 500 Internal Server Error\r\n\r\n");
			}
			else
			{
				// fprintf(file, data->post_data);
				fwrite(data->post_data, 1, strlen(data->post_data), file);
				// printf("%s\n", data->post_data);
				snprintf(
					response,
					sizeof(response),
					"HTTP/1.1 201 Created\r\n\r\n");
				fclose(file);
			}
			send_compress(clientfd, response, 0, 0);
		}
		else // get file data
		{
			if (!(file = fopen((char *)fullpath, "r")))
			{
				perror("Failed open file");
				snprintf(
					response,
					sizeof(response),
					"HTTP/1.1 404 Not Found\r\n\r\n");
				send_compress(clientfd, response, 0, 0);
			}
			else
			{
				struct stat file_status;
				if (stat(fullpath, &file_status) < 0)
				{
					perror("get stat of file failed");
					snprintf(
						response,
						sizeof(response),
						"HTTP/1.1 500 Internal Server Error\r\n\r\n");
					fclose(file);
				}
				else
				{
					snprintf(
						response,
						sizeof(response),
						"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %zu\r\n\r\n",
						file_status.st_size);
					send_compress(clientfd, response, 0, 0);
					while (fread(response, 1, BUFSIZ, file) > 0)
					{
						send_compress(clientfd, response, 0, 0);
					}
					fclose(file);
				}
			}
		}
	}
	else
	{
		strcpy(response, "HTTP/1.1 404 Not Found\r\n\r\n");
		// send
		send_compress(clientfd, response, data->compress, data->str);
	}
	free(data);
	close(clientfd);
}

data_t *parse_request(char *buf)
{
	data_t *data = (data_t *)calloc(1, sizeof(data_t));
	char *token = 0, *post_data = 0;
	if (!data)
	{
		perror("Memory Alloction faileds");
		return 0;
	}

	data->post_data = !(strncmp(buf, "POST", 4)) ? strrchr(buf, '\n') + 1 : 0;

	while (token = strsep(&buf, "\r\n"))
	{
		if (data->post_data && !strncmp(token, "POST /files/", 12)) // POST case
		{															// POST /files/test HTTP/1.1
			data->endpoint = token + 6;
			data->filename = token + 12;
			if (*data->filename == ' ' || !*data->filename) // if filename is space
			{
				// invalid request missing file name
				data->endpoint = 0;
				data->post_data = 0;
				break;
			}
			// extract the file name
			*strchr(data->endpoint, '/') = 0;
			*strchr(data->filename, ' ') = 0;
			data->method = 1;
			break;
		}
		if (!strncmp(token, "GET ", 4)) // GET / version
		{
			// endpoint
			data->endpoint = token + 4;
			if (*data->endpoint != '/' || !strchr(data->endpoint, ' '))
			{
				data->endpoint = 0; // invalid
				break;
			}
			*strchr(data->endpoint, ' ') = 0;

			if (!strcmp(data->endpoint, "/"))
			{
				break;
			}
			else if (!strncmp(data->endpoint, "/echo/", 6))
			{ // echo endpt
				data->str = data->endpoint + 6;
				if (!data->str)
				{
					data->endpoint = 0; // invalid
					break;
				}
			}
			else if (!strncmp(data->endpoint, "/files/", 7))
			{
				data->filename = data->endpoint + 7;
				if (!*data->filename) // if no filename
				{
					// invalid request missing file name
					data->endpoint = 0;
				}
				break;
			}
			else if (!(!strcmp(data->endpoint, "/user-agent/") || !strcmp(data->endpoint, "/user-agent")))
			{
				data->endpoint = 0;
				break;
			}
		}
		else if (!strncasecmp(token, "User-Agent: ", 12) && !strncmp(data->endpoint, "/user-agent", 11))
		{
			data->user_agent = token + 12;
			break;
		}
		else if (!strncmp(token, "Accept-Encoding: ", 17))
		{
			data->compress = strstr(token + 17, "gzip") ? 1 : 0; // true
			break;
		}
	}

	return data;
}

int send_compress(int clientfd, char *response, unsigned char is_compress, char *input)
{
	int ret;
	if (!is_compress)
	{
		ret = send(clientfd, response, strlen(response), 0);
		if (ret == -1)
		{
			perror("Failed to send response");
			return -1;
		}
		return Z_OK;
	}
	// compression
	z_stream strm;
	char buf[BUFSIZ];

	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);
	if (ret != Z_OK)
	{
		perror("Compression init failed");
		return ret;
	}
	strm.avail_in = (uInt)strlen(input);
	strm.next_in = (Bytef *)input;

	strm.avail_out = (uInt)BUFSIZ;
	strm.next_out = (Bytef *)buf;
	ret = deflate(&strm, Z_FINISH);
	if (ret == Z_STREAM_ERROR)
	{
		perror("Compression failed");
		deflateEnd(&strm);
		return ret;
	}

	unsigned int length = BUFSIZ - strm.avail_out;

	deflateEnd(&strm);

	snprintf(response, BUFSIZ, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Encoding: gzip\r\nContent-Length: %zu\r\n\r\n", length);
	ret = send(clientfd, response, strlen(response), 0);
	if (ret == -1)
	{
		perror("Failed to send response");
		return -1;
	}
	ret = send(clientfd, buf, length, 0);
	if (ret == -1)
	{
		perror("Failed to send response");
		return -1;
	}

	return Z_OK;
}