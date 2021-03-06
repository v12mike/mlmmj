/* Copyright (C) 2004 Morten K. Poulsen <morten at afdelingp.dk>
 *
 * $Id$
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
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
#include <dirent.h>
#include <sys/mman.h>
#include <string.h>
#include <ctype.h>

#include "getlistaddr.h"
#include "getlistdelim.h"
#include "mlmmj.h"
#include "strgen.h"
#include "wrappers.h"
#include "log_error.h"
#include "subscriberfuncs.h"
#include "mygetline.h"
#include "prepstdreply.h"
#include "memory.h"
#include "find_email_adr.h"
#include "gethdrline.h"


void do_probe(const char *listdir, const char *mlmmjsend, const char *addr)
{
	text *txt;
	file_lines_state *fls;
	char *myaddr, *from, *a, *queuefilename, *listaddr;
	char *listfqdn, *listname, *probefile, *listdelim=getlistdelim(listdir);
	int fd;
	time_t t;

	myaddr = mystrdup(addr);

	listaddr = getlistaddr(listdir);
	listname = genlistname(listaddr);
	listfqdn = genlistfqdn(listaddr);

	from = concatstr(6, listname, listdelim, "bounces-probe-", myaddr, "@",
			 listfqdn);

	myfree(listaddr);
	myfree(listdelim);
	myfree(listfqdn);
	myfree(listname);

	a = strrchr(myaddr, '=');
	if (!a) {
		myfree(myaddr);
		myfree(from);
		log_error(LOG_ARGS, "do_probe(): malformed address");
		exit(EXIT_FAILURE);

	}
	*a = '@';

	txt = open_text(listdir, "probe", NULL, NULL, NULL, "bounce-probe");
	MY_ASSERT(txt);
	register_unformatted(txt, "bouncenumbers", "%bouncenumbers%"); /* DEPRECATED */
	fls = init_truncated_file_lines(addr, 0, ':');
	register_formatted(txt, "bouncenumbers",
			rewind_file_lines, get_file_line, fls);
	queuefilename = prepstdreply(txt, listdir, "$listowner$", myaddr, NULL);
	MY_ASSERT(queuefilename);
	close_text(txt);

	finish_file_lines(fls);

	probefile = concatstr(4, listdir, "/bounce/", addr, "-probe");
	MY_ASSERT(probefile);
	t = time(NULL);
	a = mymalloc(32);
	snprintf(a, 31, "%ld", (long int)t);
	a[31] = '\0';
	unlink(probefile);
	fd = open(probefile, O_WRONLY|O_TRUNC|O_CREAT, S_IRUSR|S_IWUSR);
	if(fd < 0)
		log_error(LOG_ARGS, "Could not open %s", probefile);
	else
		if(writen(fd, a, strlen(a)) < 0)
			log_error(LOG_ARGS, "Could not write time in probe");

	myfree(probefile);

	execlp(mlmmjsend, mlmmjsend,
				"-l", "5",
				"-L", listdir,
				"-T", myaddr,
				"-F", from,
				"-m", queuefilename, (char *)NULL);

	log_error(LOG_ARGS, "execlp() of '%s' failed", mlmmjsend);

	exit(EXIT_FAILURE);
}

char *dsnparseaddr(const char *mailname)
{
	int fd, indsn = 0, i;
	char *line, *linedup, *search, *addr = NULL;
	struct email_container emails = { 0, NULL };

	fd = open(mailname, O_RDONLY);
	if(fd < 0) {
		log_error(LOG_ARGS, "Could not open bounceindexfile %s",
				mailname);
		return NULL;
	}

	while((line = gethdrline(fd, NULL))) {
		linedup = mystrdup(line);
		for(i = 0; line[i]; i++)
			linedup[i] = tolower(line[i]);
		search = strstr(linedup, "message/delivery-status");
		myfree(linedup);
		if(search) {
			indsn = 1;
			myfree(line);
			continue;
		}
		if(indsn) {
			/* TODO: this parsing could be greatly improved */
			if(strncasecmp(line, "Final-Recipient:", 16)) {
				search = strstr(line, ";");
				if (search) {
					find_email_adr(search+1, &emails);
					if(emails.emailcount > 0) {
						addr = mystrdup(emails.emaillist[0]);
						for(i = 0; i < emails.emailcount; i++)
							myfree(emails.emaillist[i]);
						myfree(emails.emaillist);
					}
				}
				myfree(line);
				break;
			}
		}
		myfree(line);
	}

	return addr;
}

static void print_help(const char *prg)
{
	printf("Usage: %s -L /path/to/list\n"
	       "       [-a john=doe.org | -d] [-n num | -p]\n"
	       " -a: Address string that bounces\n"
	       " -h: This help\n"
	       " -L: Full path to list directory\n"
	       " -n: Message number in the archive\n"
	       " -d: Attempt to parse DSN to determine address\n"
	       " -p: Send out a probe\n"
	       " -V: Print version\n", prg);

	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	int opt, fd, dsnbounce = 0, i = 0;
	char *listdir = NULL, *address = NULL, *number = NULL;
	char *bindir, *mlmmjsend, *savename;
	char *mailname = NULL, *bfilename, *a, *buf, *lowcaseaddr;
	size_t len;
	time_t t;
	int probe = 0;
	struct stat st;
	uid_t uid;

	log_set_name(argv[0]);

	CHECKFULLPATH(argv[0]);

	bindir = mydirname(argv[0]);
	mlmmjsend = concatstr(2, bindir, "/mlmmj-send");
	myfree(bindir);

	while ((opt = getopt(argc, argv, "hdVL:a:n:m:p")) != -1) {
		switch(opt) {
		case 'L':
			listdir = optarg;
			break;
		case 'a':
			address = optarg;
			break;
		case 'd':
			dsnbounce = 1;
			break;
		case 'm':
			mailname = optarg;
			break;
		case 'n':
			number = optarg;
			break;
		case 'p':
			probe = 1;
			break;
		case 'h':
			print_help(argv[0]);
			break;
		case 'V':
			print_version(argv[0]);
			exit(0);
		}
	}

	if(listdir == NULL || (address == NULL && dsnbounce == 0)
				|| (number == NULL && probe == 0)) {
		fprintf(stderr,
			"You have to specify -L, -a or -d and -n or -p\n");
		fprintf(stderr, "%s -h for help\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	/* Lets make sure no random user tries to do bouncehandling */
	if(listdir) {
		if(stat(listdir, &st) == 0) {
			uid = getuid();
			if(uid && uid != st.st_uid) {
				log_error(LOG_ARGS,
					"Have to invoke either as root "
					"or as the user owning listdir");
				writen(STDERR_FILENO,
					"Have to invoke either as root "
					"or as the user owning listdir\n", 60);
				exit(EXIT_FAILURE);
			}
		} else {
			log_error(LOG_ARGS, "Could not stat %s", listdir);
			exit(EXIT_FAILURE);
		}
	}

	if(dsnbounce) {
		address = dsnparseaddr(mailname);

		/* Delete the mailfile, no need for it anymore */
		if(mailname)
			unlink(mailname);

		if(address == NULL)
			exit(EXIT_SUCCESS);

		a = strrchr(address, '@');
		MY_ASSERT(a);
		*a = '=';
	}

	/* Make the address lowercase */
	lowcaseaddr = mystrdup(address);
	i = 0;
	while(lowcaseaddr[i]) {
		lowcaseaddr[i] = tolower(lowcaseaddr[i]);
		i++;
	}
	address = lowcaseaddr;

	if(number != NULL && probe != 0) {
		fprintf(stderr, "You can only specify one of -n or -p\n");
		fprintf(stderr, "%s -h for help\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (probe) {
		/* send out a probe */
		do_probe(listdir, mlmmjsend, address);
		/* do_probe() will never return */
		exit(EXIT_FAILURE);
	}

#if 0
	log_error(LOG_ARGS, "listdir = [%s] address = [%s] number = [%s]", listdir, address, number);
#endif

	/* check if it's sub/unsub requests bouncing, and in that case
	 * simply remove the confirmation file. Variablenames address and
	 * number are a bit misleading in this case due to the different
	 * construction of the sub/unsub confirmation From header.
	 */
	if(strcmp(number, "confsub") == 0) {
		a = concatstr(3, listdir, "/subconf/", address);
		unlink(a);
		myfree(a);
		if(mailname)
			unlink(mailname);
		exit(EXIT_SUCCESS);
	}
	if(strcmp(number, "confunsub") == 0) {
		a = concatstr(3, listdir, "/unsubconf/", address);
		unlink(a);
		myfree(a);
		if(mailname)
			unlink(mailname);
		exit(EXIT_SUCCESS);
	}
	/* Below checks for bounce probes bouncing. If they do, simply remove
	 * the probe file and exit successfully. Yes, I know the variables
	 * have horrible names, but please bear with me.
	 */
	if(strcmp(number, "probe") == 0) {
		a = concatstr(4, listdir, "/bounce/", address, "-probe");
		unlink(a);
		unlink(mailname);
		myfree(a);
		exit(EXIT_SUCCESS);
	}

	/* save the filename with '=' before replacing it with '@' */
	bfilename = concatstr(3, listdir, "/bounce/", address);

	a = strrchr(address, '=');
	if (!a) {
		if(mailname)
			unlink(mailname);
		exit(EXIT_SUCCESS);  /* ignore malformed address */
	}
	*a = '@';

	/* make sure it's a subscribed address */
	if(is_subbed(listdir, address, 0) == SUB_NONE) {
		log_error(LOG_ARGS, "%s is bouncing but not subscribed?",
				address);
		if(mailname)
			unlink(mailname);
		myfree(bfilename);
		exit(EXIT_SUCCESS); /* Not subbed, so exit silently */
	}

	if(lstat(bfilename, &st) == 0) {
		if((st.st_mode & S_IFLNK) == S_IFLNK) {
			log_error(LOG_ARGS, "%s is a symbolic link",
					bfilename);
			exit(EXIT_FAILURE);
		}
	}

	if ((fd = open(bfilename, O_WRONLY|O_APPEND|O_CREAT,
			S_IRUSR|S_IWUSR)) < 0) {
		log_error(LOG_ARGS, "Could not open '%s'", bfilename);
		myfree(bfilename);
		exit(EXIT_FAILURE);
	}

	/* TODO check that the message is not already bounced */

	/* XXX How long can the string representation of an integer be?
	 * It is not a security issue (we use snprintf()), but it would be
	 * bad mojo to cut the timestamp field  -- mortenp 20040427 */

	/* int + ":" + int + " # Wed Jun 30 21:49:08 1993\n" + NUL */
	len = 20 + 1 + 20 + 28 + 1;

	buf = mymalloc(len);
	if (!buf) exit(EXIT_FAILURE);

	t = time(NULL);
	snprintf(buf, len-26, "%s:%ld # ", number, (long int)t);
	ctime_r(&t, buf+strlen(buf));
	writen(fd, buf, strlen(buf));
	close(fd);

	if(mailname) {
		savename = concatstr(2, bfilename, ".lastmsg");
		rename(mailname, savename);
		myfree(savename);
	}

	myfree(bfilename);
	if(dsnbounce && address)
		myfree(address);

	return EXIT_SUCCESS;
}
