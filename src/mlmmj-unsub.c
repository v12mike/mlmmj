/* Copyright (C) 2002, 2003 Mads Martin Joergensen <mmj at mmj.dk>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <dirent.h>
#include <sys/wait.h>
#include <ctype.h>

#include "mlmmj.h"
#include "mlmmj-unsub.h"
#include "mylocking.h"
#include "wrappers.h"
#include "mygetline.h"
#include "getlistaddr.h"
#include "getlistdelim.h"
#include "subscriberfuncs.h"
#include "strgen.h"
#include "log_error.h"
#include "memory.h"
#include "statctrl.h"
#include "prepstdreply.h"

void confirm_unsub(const char *listdir, const char *listaddr,
		   const char *listdelim, const char *subaddr,
		   const char *mlmmjsend,
		   enum subtype typesub, enum subreason reasonsub)
{
	text *txt;
	char *queuefilename, *fromaddr, *listname, *listfqdn, *listtext;

	listname = genlistname(listaddr);
	listfqdn = genlistfqdn(listaddr);

	fromaddr = concatstr(4, listname, listdelim, "bounces-help@", listfqdn);

	myfree(listname);
	myfree(listfqdn);

	switch(typesub) {
		default:
		case SUB_NORMAL:
			listtext = mystrdup("unsub-ok");
			break;
		case SUB_DIGEST:
			listtext = mystrdup("unsub-ok-digest");
			break;
		case SUB_NOMAIL:
			listtext = mystrdup("unsub-ok-nomail");
			break;
	}

	txt = open_text(listdir, "finish", "unsub",
			subreason_strs[reasonsub], subtype_strs[typesub],
			listtext);
	myfree(listtext);
	MY_ASSERT(txt);
	register_unformatted(txt, "subaddr", subaddr);
	queuefilename = prepstdreply(txt, listdir,
			"$helpaddr$", subaddr, NULL);
	MY_ASSERT(queuefilename);
	close_text(txt);

	execlp(mlmmjsend, mlmmjsend,
				"-l", "1",
				"-L", listdir,
				"-T", subaddr,
				"-F", fromaddr,
				"-m", queuefilename, (char *)NULL);

	log_error(LOG_ARGS, "execlp() of '%s' failed", mlmmjsend);
	exit(EXIT_FAILURE);
}

void notify_unsub(const char *listdir, const char *listaddr,
		  const char *listdelim, const char *subaddr,
		  const char *mlmmjsend,
		  enum subtype typesub, enum subreason reasonsub)
{
        char *listfqdn, *listname, *fromaddr, *tostr;
	text *txt;
        char *queuefilename = NULL, *listtext;

        listname = genlistname(listaddr);
        listfqdn = genlistfqdn(listaddr);

        fromaddr = concatstr(4, listname, listdelim, "bounces-help@", listfqdn);
	tostr = concatstr(4, listname, listdelim, "owner@", listfqdn);

	myfree(listname);
	myfree(listfqdn);

	switch(typesub) {
		default:
		case SUB_NORMAL:
			listtext = mystrdup("notifyunsub");
			break;
		case SUB_DIGEST:
			listtext = mystrdup("notifyunsub-digest");
			break;
		case SUB_NOMAIL:
			listtext = mystrdup("notifyunsub-nomail");
			break;
	}
	
	txt = open_text(listdir, "notify", "unsub",
			subreason_strs[reasonsub], subtype_strs[typesub],
			listtext);
	myfree(listtext);
	MY_ASSERT(txt);
	register_unformatted(txt, "subaddr", subaddr);
	register_unformatted(txt, "oldsub", subaddr); /* DEPRECATED */
	queuefilename = prepstdreply(txt, listdir,
			"$listowner$", "$listowner$", NULL);
	MY_ASSERT(queuefilename);
	close_text(txt);

	execlp(mlmmjsend, mlmmjsend,
			"-l", "1",
			"-L", listdir,
			"-T", tostr,
			"-F", fromaddr,
			"-m", queuefilename, (char *)NULL);

        log_error(LOG_ARGS, "execlp() of '%s' failed", mlmmjsend);
        exit(EXIT_FAILURE);
}


void generate_unsubconfirm(const char *listdir, const char *listaddr,
			   const char *listdelim, const char *subaddr,
			   const char *mlmmjsend,
			   enum subtype typesub, enum subreason reasonsub)
{
	char *confirmaddr, *listname, *listfqdn, *tmpstr;
	text *txt;
	char *queuefilename, *fromaddr;
	char *randomstr = NULL, *confirmfilename = NULL, *listtext;
	int subconffd;

	listname = genlistname(listaddr);
	listfqdn = genlistfqdn(listaddr);

        do {
                myfree(confirmfilename);
                myfree(randomstr);
		randomstr = random_plus_addr(subaddr);
                confirmfilename = concatstr(3, listdir, "/unsubconf/",
					    randomstr);

                subconffd = open(confirmfilename, O_RDWR|O_CREAT|O_EXCL,
						  S_IRUSR|S_IWUSR);

        } while ((subconffd < 0) && (errno == EEXIST));

	if(subconffd < 0) {
		log_error(LOG_ARGS, "Could not open '%s'", confirmfilename);
                myfree(randomstr);
		myfree(confirmfilename);
		exit(EXIT_FAILURE);
	}

	myfree(confirmfilename);

	if(writen(subconffd, subaddr, strlen(subaddr)) < 0) {
		log_error(LOG_ARGS, "Could not write unsubconffile");
                myfree(randomstr);
		myfree(confirmfilename);
		exit(EXIT_FAILURE);
	}

	close(subconffd);

	fromaddr = concatstr(6, listname, listdelim, "bounces-confunsub-",
				randomstr, "@", listfqdn);

	switch(typesub) {
		default:
		case SUB_NORMAL:
			listtext = mystrdup("unsub-confirm");
			tmpstr = mystrdup("confunsub-");
			break;
		case SUB_DIGEST:
			listtext = mystrdup("unsub-confirm-digest");
			tmpstr = mystrdup("confunsub-digest-");
			break;
		case SUB_NOMAIL:
			listtext = mystrdup("unsub-confirm-nomail");
			tmpstr = mystrdup("confunsub-nomail-");
			break;
	}

	confirmaddr = concatstr(6, listname, listdelim, tmpstr, randomstr, "@",
				listfqdn);

	myfree(randomstr);
	myfree(tmpstr);

	txt = open_text(listdir, "confirm", "unsub",
			subreason_strs[reasonsub], subtype_strs[typesub],
			listtext);
	myfree(listtext);
	MY_ASSERT(txt);
	register_unformatted(txt, "subaddr", subaddr);
	register_unformatted(txt, "confaddr", confirmaddr); /* DEPRECATED */
	register_unformatted(txt, "confirmaddr", confirmaddr);
	queuefilename = prepstdreply(txt, listdir,
			"$helpaddr$", subaddr, confirmaddr);
	MY_ASSERT(queuefilename);
	close_text(txt);

	myfree(listname);
	myfree(listfqdn);

	execlp(mlmmjsend, mlmmjsend,
				"-l", "1",
				"-L", listdir,
				"-T", subaddr,
				"-F", fromaddr,
				"-m", queuefilename, (char *)NULL);
	log_error(LOG_ARGS, "execlp() of '%s' failed", mlmmjsend);
	exit(EXIT_FAILURE);
}

ssize_t unsubscribe(int subreadfd, int subwritefd, const char *address)
{
	off_t suboff = find_subscriber(subreadfd, address);
	struct stat st;
	char *inmap;
	size_t len = strlen(address) + 1; /* + 1 for the '\n' */
	ssize_t writeres = 0, written = 0;
	
	if(suboff == -1)
		return -1; /* Did not find subscriber */

	if(fstat(subreadfd, &st) < 0) {
		log_error(LOG_ARGS, "Could not stat subreadfd");
		return -1;
	}

	if((inmap = mmap(0, st.st_size, PROT_READ, MAP_SHARED,
		       subreadfd, 0)) == MAP_FAILED) {
		log_error(LOG_ARGS, "Could not mmap subreadfd");
		return -1;
	}

	if(suboff > 0) {
		writeres = writen(subwritefd, inmap, suboff);
		if(writeres < 0)
			return -1;
	}
	written += writeres;
	
	writeres = writen(subwritefd, inmap + len + suboff,
				st.st_size - len - suboff);
	if(writeres < 0)
		return -1;

	written += writeres;

	munmap(inmap, st.st_size);

	return written;
}

static void print_help(const char *prg)
{
	printf("Usage: %s -L /path/to/list -a john@doe.org\n"
	       "       [-b] [-c | -C] [-h] [-L] [-d | -n | -N] [-q] [-r | -R] [-s] [-V]\n"
	       " -a: Email address to unsubscribe \n"
	       " -b: Behave as if unsubscription is due to bouncing (internal use)\n"
	       " -c: Send goodbye mail\n"
	       " -C: Request mail confirmation\n"
	       " -d: Unsubscribe from digest of list\n"
	       " -h: This help\n"
	       " -L: Full path to list directory\n"
	       " -n: Unsubscribe from no mail version of list\n" 
	       " -N: Unsubscribe from normal version of list\n", prg);
	printf(" -q: Be quiet (don't notify owner about the subscription)\n"
	       " -r: Behave as if request arrived via email (internal use)\n"
	       " -R: Behave as if confirmation arrived via email (internal use)\n"
	       " -s: Don't send a mail to the address if not subscribed\n"
	       " -U: Don't switch to the user id of the listdir owner\n"
	       " -V: Print version\n"
	       "To ensure a silent unsubscription, use -q -s\n");
	exit(EXIT_SUCCESS);
}

static void generate_notsubscribed(const char *listdir, const char *subaddr,
		const char *mlmmjsend, enum subtype typesub)
{
	text *txt;
	char *queuefilename, *fromaddr, *listname, *listfqdn, *listaddr;
	char *listdelim = getlistdelim(listdir);

	listaddr = getlistaddr(listdir);
	listname = genlistname(listaddr);
	listfqdn = genlistfqdn(listaddr);

	fromaddr = concatstr(4, listname, listdelim, "bounces-help@", listfqdn);
	myfree(listdelim);

	txt = open_text(listdir,
			"deny", "unsub", "unsubbed", subtype_strs[typesub],
			"unsub-notsubscribed");
	MY_ASSERT(txt);
	register_unformatted(txt, "subaddr", subaddr);
	queuefilename = prepstdreply(txt, listdir,
			"$helpaddr$", subaddr, NULL);
	MY_ASSERT(queuefilename);
	close_text(txt);

	myfree(listaddr);
	myfree(listname);
	myfree(listfqdn);

	execlp(mlmmjsend, mlmmjsend,
				"-l", "1",
				"-L", listdir,
				"-T", subaddr,
				"-F", fromaddr,
				"-m", queuefilename, (char *)NULL);

	log_error(LOG_ARGS, "execlp() of '%s' failed", mlmmjsend);
	exit(EXIT_FAILURE);
}

static void unsubscribe_type(char *listdir, char *listaddr, char *listdelim,
		char *address, char *mlmmjsend,
		enum subtype typesub, enum subreason reasonsub) {
	char *subdir, *subddirname, *sublockname;
	char *subreadname = NULL, *subwritename;
	int subread, subwrite, rlock, wlock;
	int sublock, sublockfd;
	int groupwritable = 0;
	int unsubres;
	struct stat st;
	DIR *subddir;
	struct dirent *dp;
	off_t suboff;

	switch(typesub) {
		default:
		case SUB_NORMAL:
			subdir = "/subscribers.d/";
			break;
		case SUB_DIGEST:
			subdir = "/digesters.d/";
			break;
		case SUB_NOMAIL:
			subdir = "/nomailsubs.d/";
			break;
	}

	subddirname = concatstr(2, listdir, subdir);
	if (stat(subddirname, &st) == 0) {
		if(st.st_mode & S_IWGRP) {
			groupwritable = S_IRGRP|S_IWGRP;
			umask(S_IWOTH);
		}
	}

	if((subddir = opendir(subddirname)) == NULL) {
		log_error(LOG_ARGS, "Could not opendir(%s)",
				    subddirname);
		myfree(subddirname);
		exit(EXIT_FAILURE);
	}

	myfree(subddirname);

	while((dp = readdir(subddir)) != NULL) {
		if(!strcmp(dp->d_name, "."))
			continue;
		if(!strcmp(dp->d_name, ".."))
			continue;
		
		subreadname = concatstr(3, listdir, subdir, dp->d_name);

		subread = open(subreadname, O_RDWR);
		if(subread == -1) {
			myfree(subreadname);
			continue;
		}

		suboff = find_subscriber(subread, address);
		if(suboff == -1) {
			close(subread);
			myfree(subreadname);
			continue;
		}

		/* create a .name.lock file and aquire the lock */
		sublockname = concatstr(5, listdir, subdir, ".", dp->d_name,
					".lock");
		sublockfd = open(sublockname, O_RDWR | O_CREAT,
						S_IRUSR | S_IWUSR);
		if(sublockfd < 0) {
			log_error(LOG_ARGS, "Error opening lock file %s",
					sublockname);
			myfree(sublockname);
			continue;
		}

		sublock = myexcllock(sublockfd);
		if(sublock < 0) {
			log_error(LOG_ARGS, "Error locking '%s' file",
					sublockname);
			myfree(sublockname);
			close(sublockfd);
			continue;
		}
		
		rlock = myexcllock(subread);
		if(rlock < 0) {
			log_error(LOG_ARGS, "Error locking '%s' file",
					subreadname);
			close(subread);
			close(sublockfd);
			myfree(subreadname);
			myfree(sublockname);
			continue;
		}

		subwritename = concatstr(2, subreadname, ".new");

		subwrite = open(subwritename, O_RDWR | O_CREAT | O_TRUNC,
				S_IRUSR | S_IWUSR | groupwritable);
		if(subwrite == -1){
			log_error(LOG_ARGS, "Could not open '%s'",
					subwritename);
			close(subread);
			close(sublockfd);
			myfree(subreadname);
			myfree(subwritename);
			myfree(sublockname);
			continue;
		}

		wlock = myexcllock(subwrite);
		if(wlock < 0) {
			log_error(LOG_ARGS, "Error locking '%s'",
					subwritename);
			close(subread);
			close(subwrite);
			close(sublockfd);
			myfree(subreadname);
			myfree(subwritename);
			myfree(sublockname);
			continue;
		}

		unsubres = unsubscribe(subread, subwrite, address);
		if(unsubres < 0) {
			close(subread);
			close(subwrite);
			close(sublockfd);
			unlink(subwritename);
			myfree(subreadname);
			myfree(subwritename);
			myfree(sublockname);
			continue;
		}

		if(unsubres > 0) {
			if(rename(subwritename, subreadname) < 0) {
				log_error(LOG_ARGS,
					"Could not rename '%s' to '%s'",
					subwritename, subreadname);
				close(subread);
				close(subwrite);
				myfree(subreadname);
				myfree(subwritename);
				continue;
			}
		} else { /* unsubres == 0, no subscribers left */
			unlink(subwritename);
			unlink(subreadname);
		}

		close(subread);
		close(subwrite);
		close(sublockfd);
		myfree(subreadname);
		myfree(subwritename);
		unlink(sublockname);
		myfree(sublockname);

        }

	closedir(subddir);
}

int main(int argc, char **argv)
{
	int opt;
	int normal = 0, digest = 0, nomail = 0, subbed;
	int confirmunsub = 0, unsubconfirm = 0, notifysub = 0;
	int changeuid = 1, quiet = 0;
	int nogennotsubscribed = 0, i = 0;
	int status;
	char *listaddr, *listdelim, *listdir = NULL, *address = NULL;
	char *mlmmjsend, *bindir, *subdir, *subddirname;
	char *lowcaseaddr;
	enum subtype typesub = SUB_ALL;
	enum subreason reasonsub = SUB_ADMIN;
	uid_t uid;
	struct stat st;
	pid_t pid, childpid;

	CHECKFULLPATH(argv[0]);
	
	log_set_name(argv[0]);

	bindir = mydirname(argv[0]);
	mlmmjsend = concatstr(2, bindir, "/mlmmj-send");
	myfree(bindir);

	while ((opt = getopt(argc, argv, "hcCdenNVUL:a:sbqrR")) != -1) {
		switch(opt) {
		case 'L':
			listdir = optarg;
			break;
		case 'a':
			address = optarg;
			break;
		case 'b':
			reasonsub = SUB_BOUNCING;
			break;
		case 'c':
			confirmunsub = 1;
			break;
		case 'C':
			unsubconfirm = 1;
			break;
		case 'd':
			digest = 1;
			break;
		case 'h':
			print_help(argv[0]);
			break;
		case 'n':
			nomail = 1;
			break;
		case 'N':
			normal = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'r':
			reasonsub = SUB_REQUEST;
			break;
		case 'R':
			reasonsub = SUB_CONFIRM;
			break;
		case 's':
			nogennotsubscribed = 1;
			break;
		case 'U':
			changeuid = 0;
			break;
		case 'V':
			print_version(argv[0]);
			exit(0);
		}
	}
	if(listdir == 0 || address == 0) {
		fprintf(stderr, "You have to specify -L and -a\n");
		fprintf(stderr, "%s -h for help\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if(digest + nomail + normal > 1) {
		fprintf(stderr, "Specify at most one of -d, -n and -N\n");
		fprintf(stderr, "%s -h for help\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if(digest)
		typesub = SUB_DIGEST;
	if(nomail)
		typesub = SUB_NOMAIL;
	if(normal)
		typesub = SUB_NORMAL;

	if(confirmunsub && unsubconfirm) {
		fprintf(stderr, "Cannot specify both -C and -c\n");
		fprintf(stderr, "%s -h for help\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if(reasonsub == SUB_CONFIRM && unsubconfirm) {
		fprintf(stderr, "Cannot specify both -C and -R\n");
		fprintf(stderr, "%s -h for help\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if(reasonsub == SUB_BOUNCING && unsubconfirm) {
		fprintf(stderr, "Cannot specify both -C and -b\n");
		fprintf(stderr, "%s -h for help\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	/* Make the address lowercase */
	lowcaseaddr = mystrdup(address);
	i = 0;
	while(lowcaseaddr[i]) {
		lowcaseaddr[i] = tolower(lowcaseaddr[i]);
		i++;
	}
	address = lowcaseaddr;

	/* get the list address */
	listaddr = getlistaddr(listdir);
	listdelim = getlistdelim(listdir);

	if(changeuid) {
		uid = getuid();
		if(!uid && stat(listdir, &st) == 0) {
			printf("Changing to uid %d, owner of %s.\n",
					(int)st.st_uid, listdir);
			if(setuid(st.st_uid) < 0) {
				perror("setuid");
				fprintf(stderr, "Continuing as uid %d\n",
						(int)uid);
			}
		}
	}

	if (typesub == SUB_ALL) {
		subbed = is_subbed(listdir, address, 0) != SUB_NONE;
	} else {
		switch(typesub) {
			default:
			case SUB_NORMAL:
				subdir = "/subscribers.d/";
				break;
			case SUB_DIGEST:
				subdir = "/digesters.d/";
				break;
			case SUB_NOMAIL:
				subdir = "/nomailsubs.d/";
				break;
		}
		subddirname = concatstr(2, listdir, subdir);
		subbed = is_subbed_in(subddirname, address);
		myfree(subddirname);
	}

	if(!subbed) {
		/* Address is not subscribed */
		myfree(listaddr);
		myfree(listdelim);

		if(!nogennotsubscribed) {
			generate_notsubscribed(listdir, address, mlmmjsend,
					typesub);
		}

		exit(EXIT_SUCCESS);
	}

	if(unsubconfirm)
		generate_unsubconfirm(listdir, listaddr, listdelim, address,
				mlmmjsend, typesub, reasonsub);

	if (typesub == SUB_ALL) {
		unsubscribe_type(listdir, listaddr, listdelim, address,
				mlmmjsend, SUB_NORMAL, reasonsub);
		unsubscribe_type(listdir, listaddr, listdelim, address,
				mlmmjsend, SUB_DIGEST, reasonsub);
		unsubscribe_type(listdir, listaddr, listdelim, address,
				mlmmjsend, SUB_NOMAIL, reasonsub);
	} else {
		unsubscribe_type(listdir, listaddr, listdelim, address,
				mlmmjsend, typesub, reasonsub);
	}

	if(confirmunsub) {
		childpid = fork();

		if(childpid < 0) {
			log_error(LOG_ARGS, "Could not fork");
			confirm_unsub(listdir, listaddr, listdelim,
					address, mlmmjsend,
					typesub, reasonsub);
		}

		if(childpid > 0) {
			do /* Parent waits for the child */
				pid = waitpid(childpid, &status, 0);
			while(pid == -1 && errno == EINTR);
		}

		/* child confirms subscription */
		if(childpid == 0)
			confirm_unsub(listdir, listaddr, listdelim,
					address, mlmmjsend,
					typesub, reasonsub);
	}

        notifysub = !quiet && statctrl(listdir, "notifysub");

        /* Notify list owner about subscription */
        if (notifysub)
                notify_unsub(listdir, listaddr, listdelim, address, mlmmjsend,
				typesub, reasonsub);

	myfree(listaddr);
	myfree(listdelim);

	return EXIT_SUCCESS;
}
