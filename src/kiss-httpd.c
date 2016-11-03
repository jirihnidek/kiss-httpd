/*
 *
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 * Contributor(s): Jiri Hnidek <jiri.hnidek@tul.cz>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8081
#define BUF_SIZE 65536
#define HTTP_200_RESPONSE_HEADER "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n"
#define DEFAULT_HTML_PAGE "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n<!doctype html>\r\n<html lang=\"en\">\r\n<head>\r\n<title>KISS Httpd Test Page</title>\r\n</head>\r\n<body><h1>KISS Httpd Test Page</h1><p>KISS is in this case acronym: Keep It Simple, Stupid!</p></body></html>"
#define ERROR_404_PAGE "HTTP/1.1 404 Not Found\r\nContent-Type: text/html; charset=utf-8\r\n\r\n<!doctype html>\r\n<html lang=\"en\">\r\n<head>\r\n<title>KISS Httpd Error 404: Not Found</title>\r\n</head>\r\n<body><h1>KISS Httpd Error 404: Not Found</h1></body></html>"

static int running = 0;
static int queue_len = 10;
static char *conf_file_name = NULL;
static char *pid_file_name = NULL;
static int pid_fd = -1;
static char *app_name = NULL;
static FILE *log_stream;
static char *html_file_name = NULL;
static char *log_file_name = NULL;
static char *html_page = NULL;
static unsigned int html_page_size = 0;

/**
 * \brief Read configuration from config file
 */
int read_conf_file(int reload)
{
	FILE *conf_file = NULL;
	int ret = -1;

	if (conf_file_name == NULL) return 0;

	conf_file = fopen(conf_file_name, "r");

	if(conf_file == NULL) {
		syslog(LOG_ERR, "Can not open config file: %s, error: %s",
				conf_file_name, strerror(errno));
		return -1;
	}

	ret = fscanf(conf_file, "%d", &queue_len);

	if(ret > 0) {
		if(reload == 1) {
			syslog(LOG_INFO, "Reloaded configuration file %s of %s",
				conf_file_name,
				app_name);
		} else {
			syslog(LOG_INFO, "Configuration of %s read from file %s",
				app_name,
				conf_file_name);
		}
	}

	fclose(conf_file);

	return ret;
}

/**
 * \brief This function tries to test config file
 */
int test_conf_file(char *_conf_file_name)
{
	FILE *conf_file = NULL;
	int ret = -1;

	conf_file = fopen(_conf_file_name, "r");

	if(conf_file == NULL) {
		fprintf(stderr, "Can't read config file %s\n",
			_conf_file_name);
		return EXIT_FAILURE;
	}

	ret = fscanf(conf_file, "%d", &queue_len);

	if(ret <= 0) {
		fprintf(stderr, "Wrong config file %s\n",
			_conf_file_name);
	}

	fclose(conf_file);

	if(ret > 0)
		return EXIT_SUCCESS;
	else
		return EXIT_FAILURE;
}

/**
 * \brief Callback function for handling signals.
 * \param	sig	identifier of signal
 */
void handle_signal(int sig)
{
	if(sig == SIGINT) {
		fprintf(log_stream, "Debug: stopping daemon ...\n");
		/* Unlock and close lockfile */
		if(pid_fd != -1) {
			lockf(pid_fd, F_ULOCK, 0);
			close(pid_fd);
		}
		/* Try to delete lockfile */
		if(pid_file_name != NULL) {
			unlink(pid_file_name);
		}
		running = 0;
		/* Reset signal handling to default behavior */
		signal(SIGINT, SIG_DFL);
	} else if(sig == SIGHUP) {
		fprintf(log_stream, "Debug: reloading daemon config file ...\n");
		read_conf_file(1);
	} else if(sig == SIGCHLD) {
		fprintf(log_stream, "Debug: received SIGCHLD signal\n");
	}
}

/**
 * \brief This function will daemonize this app
 */
static void daemonize()
{
	pid_t pid = 0;
	int fd;

	/* Fork off the parent process */
	pid = fork();

	/* An error occurred */
	if(pid < 0) {
		exit(EXIT_FAILURE);
	}

	/* Success: Let the parent terminate */
	if(pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* On success: The child process becomes session leader */
	if(setsid() < 0) {
		exit(EXIT_FAILURE);
	}

	/* Ignore signal sent from child to parent process */
	signal(SIGCHLD, SIG_IGN);

	/* Fork off for the second time*/
	pid = fork();

	/* An error occurred */
	if(pid < 0) {
		exit(EXIT_FAILURE);
	}

	/* Success: Let the parent terminate */
	if(pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* Set new file permissions */
	umask(0);

	/* Change the working directory to the root directory */
	/* or another appropriated directory */
	chdir("/");

	/* Close all open file descriptors */
	for(fd = sysconf(_SC_OPEN_MAX); fd > 0; fd--)
	{
		close(fd);
	}

	/* Reopen stdin (fd = 0), stdout (fd = 1), stderr (fd = 2) */
	stdin = fopen("/dev/null", "r");
	stdout = fopen("/dev/null", "w+");
	stderr = fopen("/dev/null", "w+");

	/* Try to write PID of daemon to lockfile */
	if(pid_file_name != NULL)
	{
		char str[256];
		pid_fd = open(pid_file_name, O_RDWR|O_CREAT, 0640);
		if(pid_fd < 0)
		{
			/* Can't open lockfile */
			exit(EXIT_FAILURE);
		}
		if(lockf(pid_fd, F_TLOCK, 0) < 0)
		{
			/* Can't lock file */
			exit(EXIT_FAILURE);
		}
		/* Get current PID */
		sprintf(str, "%d\n", getpid());
		/* Write PID to lockfile */
		write(pid_fd, str, strlen(str));
	}
}

/**
 * \brief Print help for this application
 */
void print_help(void)
{
	printf("\n Usage: %s [OPTIONS]\n\n", app_name);
	printf("  Options:\n");
	printf("   -h --help                 Print this help\n");
	printf("   -c --conf_file filename   Read configuration from the file\n");
	printf("   -t --test_conf filename   Test configuration file\n");
	printf("   -l --log_file  filename   Write logs to the file\n");
	printf("   -d --daemon               Daemonize this application\n");
	printf("   -p --pid_file  filename   PID file used by daemonized app\n");
	printf("   -f --file_html filename   HTML file\n");
	printf("\n");
}

/**
 * \brief Parse HTTP request
 */
int parse_http_request(char *buffer, int buffer_len)
{
	if(buffer_len > 5) {
		if(buffer[4] == '/' && buffer[5] == ' ') {
			return 1;
		} else {
			return 0;
		}
	}
	return 0;
}

/**
 * \brief Try to read html file.
 */
int read_html_file(int reload)
{
	FILE *html_page_file = NULL;
	struct stat st;
	unsigned int size = 0, buf_pos = 0;

	if (html_file_name == NULL) return 0;

	html_page_file = fopen(html_file_name, "r");

	if(html_page_file == NULL) {
		syslog(LOG_ERR, "Can not open html file: %s, error: %s",
				html_file_name, strerror(errno));
		if(html_page != NULL) {
			free(html_page);
			html_page = NULL;
		}
		html_page_size = 0;
		return -1;
	}

	if (stat(html_file_name, &st) == 0)
		size = st.st_size;

	/* Free previous buffer with html page */
	if(html_page != NULL) {
		free(html_page);
		html_page = NULL;
	}

	html_page = (char*)malloc((strlen(HTTP_200_RESPONSE_HEADER) + size) * sizeof(char));

	if(html_page == NULL) {
		fclose(html_page_file);
		html_page_size = 0;
		return -1;
	}

	strncpy(html_page, HTTP_200_RESPONSE_HEADER, strlen(HTTP_200_RESPONSE_HEADER));
	buf_pos = strlen(HTTP_200_RESPONSE_HEADER);

	while(feof(html_page_file) == 0) {
		buf_pos += fread(&html_page[buf_pos], 1, 128, html_page_file);
	};

	if(size > 0 && buf_pos > 0) {
		if(reload == 1) {
			syslog(LOG_INFO, "Reloaded html file %s",
				html_file_name);
		} else {
			syslog(LOG_INFO, "Loaded html file: %s",
				html_file_name);
		}
	}

	html_page_size = buf_pos;

	fclose(html_page_file);

	return buf_pos;
}

int main_httpd_loop(void)
{
	int listen_sock_fd, conn_sock_fd = -1;
	struct sockaddr_in6 server_address;
	struct sockaddr_in6 client_address;
	unsigned int len = sizeof(client_address);
	char str_addr[INET6_ADDRSTRLEN];
	fd_set set, test_set;
	struct timeval tv;
	char buffer[BUF_SIZE];
	int ret, flag;

	listen_sock_fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

    /* Set listen socket non-blocking */
    flag = fcntl(listen_sock_fd, F_GETFL, 0);
    fcntl(listen_sock_fd, F_SETFL, flag | O_NONBLOCK);

	server_address.sin6_family = AF_INET6;
	server_address.sin6_addr = in6addr_any;
	server_address.sin6_port = htons(PORT);

	ret = bind(listen_sock_fd, (struct sockaddr *)&server_address,
			sizeof(server_address));
	if(ret != 0) {
		syslog(LOG_ERR, "Can not bind() socket: %d to address: [::1]:%d, error: %s",
				listen_sock_fd, PORT, strerror(errno));
		return 0;
	}

	ret = listen(listen_sock_fd, queue_len);
	if(ret != 0) {
		syslog(LOG_ERR, "Can not listen() on socket: %d queu_len: %d, error: %s",
				listen_sock_fd, queue_len, strerror(errno));
		return 0;
	}

	/* This global variable can be changed in function handling signal */
	running = 1;

	FD_ZERO(&set);
	FD_SET(listen_sock_fd, &set);

	/* Never ending loop of server */
	while(running == 1) {
		/* Debug print */
		/* fprintf(log_stream, "Server waiting\n"); */

		test_set = set;
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		ret = select(FD_SETSIZE, &test_set, NULL, NULL, &tv);

		if(ret == -1) {
			perror("select()");
			return 0;
		} else if(ret > 0) {
			int sock_fd;
			for(sock_fd = 0; sock_fd < FD_SETSIZE; sock_fd++) {
				if(FD_ISSET(sock_fd, &test_set)) {
					if (sock_fd == listen_sock_fd) {
						conn_sock_fd = accept(listen_sock_fd,
								(struct sockaddr*)&client_address, &len);
						if(conn_sock_fd == -1) {
							perror("accept()");
							return 0;
						}
						inet_ntop(AF_INET6, &client_address.sin6_addr,
								str_addr, sizeof(client_address));
						fprintf(log_stream, "New connection from: %s:%d\n",
								str_addr, ntohs(client_address.sin6_port));
					    /* Set listen socket non-blocking */
					    flag = fcntl(conn_sock_fd, F_GETFL, 0);
					    fcntl(conn_sock_fd, F_SETFL, flag | O_NONBLOCK);
						FD_SET(conn_sock_fd, &set);
					} else {
						int num_read = read(conn_sock_fd, buffer, BUF_SIZE);
						buffer[num_read] = '\0';
						/* fprintf(log_stream, "Received: %s\n", buffer); */
						ret = parse_http_request(buffer, num_read);
						if(ret == 1) {
							if(html_page == NULL) {
								write(conn_sock_fd, DEFAULT_HTML_PAGE,
										strlen(DEFAULT_HTML_PAGE));
							} else {
								write(conn_sock_fd, html_page, html_page_size);
							}
						} else {
							write(conn_sock_fd, ERROR_404_PAGE,
									strlen(ERROR_404_PAGE));
						}
						close(conn_sock_fd);
						FD_CLR(sock_fd, &set);
					}
				}
			}
		} else {
			/* Timeout */
		}
	}

	return 1;
}

/* Main function */
int main(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"conf_file", required_argument, 0, 'c'},
		{"test_conf", required_argument, 0, 't'},
		{"log_file", required_argument, 0, 'l'},
		{"help", no_argument, 0, 'h'},
		{"daemon", no_argument, 0, 'd'},
		{"pid_file", required_argument, 0, 'p'},
		{"file_html", required_argument, 0, 'f'},
		{NULL, 0, 0, 0}
	};
	int value, option_index = 0;
	int start_daemonized = 0;

	app_name = argv[0];

	/* Try to process all command line arguments */
	while( (value = getopt_long(argc, argv, "c:l:t:p:f:dh", long_options, &option_index)) != -1) {
		switch(value) {
			case 'c':
				conf_file_name = strdup(optarg);
				break;
			case 'l':
				log_file_name = strdup(optarg);
				break;
			case 'p':
				pid_file_name = strdup(optarg);
				break;
			case 't':
				return test_conf_file(optarg);
			case 'd':
				start_daemonized = 1;
				break;
			case 'f':
				html_file_name = strdup(optarg);
				break;
			case 'h':
				print_help();
				return EXIT_SUCCESS;
			case '?':
				print_help();
				return EXIT_FAILURE;
			default:
				break;
		}
	}

	/* When daemonizing is requested at command line. */
	if(start_daemonized == 1) {
		/* It is also possible to use glibc function deamon()
		 * at this point, but it is useful to customize your daemon. */
		daemonize();
	}

	/* Open system log and write message to it */
	openlog(argv[0], LOG_PID|LOG_CONS, LOG_DAEMON);
	syslog(LOG_INFO, "Started %s", app_name);

	/* Daemon will handle two signals */
	signal(SIGINT, handle_signal);
	signal(SIGHUP, handle_signal);

	/* Try to open log file to this daemon */
	if(log_file_name != NULL) {
		log_stream = fopen(log_file_name, "a+");
		if (log_stream == NULL)
		{
			syslog(LOG_ERR, "Can not open log file: %s, error: %s",
				log_file_name, strerror(errno));
			log_stream = stdout;
		}
	} else {
		log_stream = stdout;
	}

	/* Read configuration from config file */
	read_conf_file(0);

	if(html_file_name != NULL) {
		read_html_file(0);
	}

	/* printf("HTML page (%s): \n%s\n", html_file_name, html_page); */

	/* Start main loop of this server */
	main_httpd_loop();

	/* Close log file, when it is used. */
	if (log_stream != stdout)
	{
		fclose(log_stream);
	}

	/* Write system log and close it. */
	syslog(LOG_INFO, "Stopped %s", app_name);
	closelog();

	/* Free allocated memory */
	if(conf_file_name != NULL) free(conf_file_name);
	if(log_file_name != NULL) free(log_file_name);
	if(pid_file_name != NULL) free(pid_file_name);

	return EXIT_SUCCESS;
}
