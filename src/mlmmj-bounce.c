/* Copyright (C) 2004 Morten K. Poulsen <morten at afdelingp.dk>
 *
 * $Id$
 *
 * This file is redistributable under version 2 of the GNU General
 * Public License as described at http://www.gnu.org/licenses/gpl.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include "mlmmj.h"
#include "strgen.h"
#include "wrappers.h"
#include "log_error.h"
#include "subscriberfuncs.h"

static void print_help(const char *prg)
{
	printf("Usage: %s [-P] -L /path/to/chat-list\n"
		"          -a address\n"
		"          -n message-number\n", prg);
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int opt, noprocess = 0;
	char *listdir = NULL, *address = NULL, *number = NULL;
	char *filename, *a, *buf;
	size_t len;
	int fd;
	time_t t;
	off_t suboff;

	log_set_name(argv[0]);

	while ((opt = getopt(argc, argv, "hVPL:a:n:")) != -1) {
		switch(opt) {
		case 'L':
			listdir = optarg;
			break;
		case 'a':
			address = optarg;
			break;
		case 'n':
			number = optarg;
			break;
		case 'h':
			print_help(argv[0]);
			break;
		case 'P':
			noprocess = 1;
			break;
		case 'V':
			print_version(argv[0]);
			exit(0);
		}
	}
	if(listdir == NULL || address == NULL || number == NULL) {
		fprintf(stderr, "You have to specify -L, -a and -n\n");
		fprintf(stderr, "%s -h for help\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	log_error(LOG_ARGS, "[%s] [%s] [%s]", listdir, address, number);
	
	/* First make sure it's a subscribed address */
	filename = concatstr(2, listdir, "/subscribers");
	if ((fd = open(filename, O_RDONLY)) < 0) {
		log_error(LOG_ARGS, "Could not open '%s'", filename);
		exit(EXIT_FAILURE);
	}
	suboff = find_subscriber(fd, address);
	if(suboff == -1)
		exit(EXIT_SUCCESS); /* Not subbed, so exit silently */
	free(filename);

	filename = concatstr(3, listdir, "/bounce/", address);

	/* TODO make sure the file we open below is not a symlink */
	if ((fd = open(filename, O_WRONLY|O_APPEND|O_CREAT,
			S_IRUSR|S_IWUSR)) < 0) {
		log_error(LOG_ARGS, "Could not open '%s'", filename);
		exit(EXIT_FAILURE);
	}

	a = strchr(address, '=');
	/* ignore malformed address */
	if (!a) exit(EXIT_FAILURE);
	*a = '@';

	/* TODO check that the message is not already bounced */

	/* XXX How long can the string representation of an integer be?
	 * It is not a security issue (we use snprintf()), but it would be
	 * bad mojo to cut the timestamp field  -- mortenp 20040427 */

	/* int + ":" + int + " # Wed Jun 30 21:49:08 1993\n" + NUL */
	len = 20 + 1 + 20 + 28 + 1;

	buf = malloc(len);
	if (!buf) exit(EXIT_FAILURE);

	t = time(NULL);
	snprintf(buf, len-26, "%s:%d # ", number, (int)t);
	ctime_r(&t, buf+strlen(buf));
	writen(fd, buf, strlen(buf));
	close(fd);

	return EXIT_FAILURE;
}
