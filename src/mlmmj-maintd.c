/* Copyright (C) 2004 Mads Martin Joergensen <mmj at mmj.dk>
 *
 * $Id$
 *
 * This file is redistributable under version 2 of the GNU General
 * Public License as described at http://www.gnu.org/licenses/gpl.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "mlmmj-maintd.h"
#include "mlmmj.h"
#include "strgen.h"
#include "chomp.h"
#include "log_error.h"
#include "mygetline.h"
#include "wrappers.h"

static int maintdlogfd = -1;

static void print_help(const char *prg)
{
	printf("Usage: %s -L /path/to/listdir [-F]\n"
	       " -L: Full path to list directory\n"
	       " -F: Don't fork, performing one maintenance run only.\n"
	       "     This option should be used when one wants to\n"
	       "     avoid running another daemon, and use e.g."
	       "     cron to control it instead.\n", prg);
	exit(EXIT_SUCCESS);
}

int delolder(const char *dirname, time_t than)
{
	DIR *dir;
	struct dirent *dp;
	struct stat st;
	time_t t;

	if(chdir(dirname) < 0) {
		log_error(LOG_ARGS, "Could not chdir(%s)", dirname);
		return -1;
	}
	if((dir = opendir(dirname)) == NULL) {
		log_error(LOG_ARGS, "Could not opendir(%s)", dirname);
		return -1;
	}

	while((dp = readdir(dir)) != NULL) {
		if(stat(dp->d_name, &st) < 0) {
			log_error(LOG_ARGS, "Could not stat(%s)",dp->d_name);
			continue;
		}
		if(!S_ISREG(st.st_mode))
			continue;
		t = time(NULL);
		if(t - st.st_mtime > than)
			unlink(dp->d_name);
	}
	closedir(dir);

	return 0;
}


int clean_moderation(const char *listdir)
{
	char *moddirname = concatstr(2, listdir, "/moderation");
	int ret = delolder(moddirname, MODREQLIFE);	
		
	free(moddirname);

	return ret;
}

int clean_discarded(const char *listdir)
{
	char *discardeddirname = concatstr(2, listdir, "/queue/discarded");
	int ret = delolder(discardeddirname, DISCARDEDLIFE);

	free(discardeddirname);

	return ret;
}

int discardmail(const char *old, const char *new, time_t age)
{
	struct stat st;
	time_t t;

	stat(old, &st);
	t = time(NULL);

	if(t - st.st_mtime > age) {
		rename(old, new);
		return 1;
	}

	return 0;
}

int resend_queue(const char *listdir, const char *mlmmjsend)
{
	DIR *queuedir;
	struct dirent *dp;
	char *mailname, *fromname, *toname, *reptoname, *from, *to, *repto;
	char *discardedname = NULL, *ch;
	char *dirname = concatstr(2, listdir, "/queue/");
	pid_t childpid, pid;
	struct stat st;
	int fromfd, tofd, fd, discarded = 0, status;

	if(chdir(dirname) < 0) {
		log_error(LOG_ARGS, "Could not chdir(%s)", dirname);
		free(dirname);
		return 1;
	}
		
	if((queuedir = opendir(dirname)) == NULL) {
		log_error(LOG_ARGS, "Could not opendir(%s)", dirname);
		free(dirname);
		return 1;
	}
	free(dirname);

	while((dp = readdir(queuedir)) != NULL) {
		if(stat(dp->d_name, &st) < 0) {
			log_error(LOG_ARGS, "Could not stat(%s)",dp->d_name);
			continue;
		}

		if(!S_ISREG(st.st_mode))
			continue;

		if(strchr(dp->d_name, '.')) {
			mailname = strdup(dp->d_name);
			ch = strchr(mailname, '.');
			*ch = '\0';
			if(stat(mailname, &st) < 0)
				if(errno == ENOENT)
					unlink(dp->d_name);
			free(mailname);
			continue;
		}

		mailname = concatstr(3, listdir, "/queue/", dp->d_name);

		fromname = concatstr(2, mailname, ".mailfrom");
		if(stat(fromname, &st) < 0) {
			if(errno == ENOENT) {
				discardedname = concatstr(3,
						listdir, "/queue/discarded/",
						dp->d_name);
				discarded = discardmail(mailname,
							discardedname,
							3600);
			} else {
				log_error(LOG_ARGS, "Could not stat(%s)",
						dp->d_name);
			}
		}

		toname = concatstr(2, mailname, ".reciptto");
		if(!discarded && stat(toname, &st) < 0) {
			if(errno == ENOENT) {
				discardedname = concatstr(3,
						listdir, "/queue/discarded/",
						dp->d_name);
				discarded = discardmail(mailname,
							discardedname,
							3600);
			} else {
				log_error(LOG_ARGS, "Could not stat(%s)",
						dp->d_name);
			}
		}
		
		reptoname = concatstr(2, mailname, ".reply-to");

		fromfd = open(fromname, O_RDONLY);
		tofd = open(toname, O_RDONLY);

		if(fromfd < 0 || tofd < 0) {
			if(discarded) {
				unlink(fromname);
				unlink(toname);
				unlink(reptoname);
			}
			free(mailname);
			free(fromname);
			free(toname);
			free(reptoname);
			if(fromfd >= 0)
				close(fromfd);
			continue;
		}

		from = mygetline(fromfd);
		chomp(from);
		close(fromfd);
		unlink(fromname);
		free(fromname);
		to = mygetline(tofd);
		chomp(to);
		close(tofd);
		unlink(toname);
		free(toname);
		fd = open(reptoname, O_RDONLY);
		if(fd < 0) {
			free(reptoname);
			repto = NULL;
		} else {
			repto = mygetline(fd);
			chomp(repto);
			close(fd);
			unlink(reptoname);
			free(reptoname);
		}

		childpid = fork();

		if(childpid < 0) {
			log_error(LOG_ARGS, "Could not fork");
			continue;
		}

		if(childpid > 0) {
			do /* Parent waits for the child */
			      pid = waitpid(childpid, &status, 0);
			while(pid == -1 && errno == EINTR);
		} else {
			if(repto) {
				execlp(mlmmjsend, mlmmjsend,
						"-l", "1",
						"-m", mailname,
						"-F", from,
						"-T", to,
						"-R", repto,
						"-a", 0);
			} else {
				execlp(mlmmjsend, mlmmjsend,
						"-l", "1",
						"-m", mailname,
						"-F", from,
						"-T", to,
						"-a", 0);
			}
		}
		free(mailname);
		free(from);
		free(to);
		free(repto);
	}

	closedir(queuedir);

	return 0;
}

int resend_requeue(const char *listdir, const char *mlmmjsend)
{
	DIR *queuedir;
	struct dirent *dp;
	char *dirname = concatstr(2, listdir, "/requeue/");
	char *archivefilename, *subfilename, *subnewname;
	struct stat st;
	pid_t childpid, pid;
	time_t t;
	int status;

	if(chdir(dirname) < 0) {
		log_error(LOG_ARGS, "Could not chdir(%s)", dirname);
		free(dirname);
		return 1;
	}
		
	if((queuedir = opendir(dirname)) == NULL) {
		log_error(LOG_ARGS, "Could not opendir(%s)", dirname);
		free(dirname);
		return 1;
	}

	while((dp = readdir(queuedir)) != NULL) {
		if((strcmp(dp->d_name, "..") == 0) ||
			(strcmp(dp->d_name, ".") == 0))
				continue;

		if(stat(dp->d_name, &st) < 0) {
			log_error(LOG_ARGS, "Could not stat(%s)",dp->d_name);
			continue;
		}

		if(!S_ISDIR(st.st_mode))
			continue;

		/* Remove old empty directories */
		t = time(NULL);
		if(t - st.st_mtime > (time_t)3600)
			if(rmdir(dp->d_name) == 0)
				continue;

		archivefilename = concatstr(3, listdir, "/archive/",
						dp->d_name);
		if(stat(archivefilename, &st) < 0) {
			/* Might be it's just not moved to the archive
			 * yet because it's still getting sent, so just
			 * continue
			 */
			free(archivefilename);
			continue;
		}
		subfilename = concatstr(3, dirname, dp->d_name, "/subscribers");
		if(stat(subfilename, &st) < 0) {
			log_error(LOG_ARGS, "Could not stat(%s)", subfilename);
			free(archivefilename);
			free(subfilename);
			continue;
		}

		subnewname = concatstr(2, subfilename, ".resending");

		if(rename(subfilename, subnewname) < 0) {
			log_error(LOG_ARGS, "Could not rename(%s, %s)",
						subfilename, subnewname);
			free(archivefilename);
			free(subfilename);
			free(subnewname);
			continue;
		}
		free(subfilename);
		
		childpid = fork();

		if(childpid < 0) {
			log_error(LOG_ARGS, "Could not fork");
			continue;
		}

		if(childpid > 0) {
			do /* Parent waits for the child */
			      pid = waitpid(childpid, &status, 0);
			while(pid == -1 && errno == EINTR);
		} else {
			execlp(mlmmjsend, mlmmjsend,
					"-l", "3",
					"-L", listdir,
					"-m", archivefilename,
					"-s", subnewname,
					"-a",
					"-D", 0);
		}
		free(archivefilename);
		free(subnewname);
	}

	closedir(queuedir);

	return 0;
}

int clean_nolongerbouncing(const char *listdir)
{
	DIR *bouncedir;
	char *dirname = concatstr(2, listdir, "/bounce/");
	char *filename, *probetimestr, *s;
	int probefd;
	time_t probetime, t;
	struct dirent *dp;
	struct stat st;
	
	if(chdir(dirname) < 0) {
		log_error(LOG_ARGS, "Could not chdir(%s)", dirname);
		free(dirname);
		return 1;
	}
		
	if((bouncedir = opendir(dirname)) == NULL) {
		log_error(LOG_ARGS, "Could not opendir(%s)", dirname);
		free(dirname);
		return 1;
	}

	free(dirname);

	while((dp = readdir(bouncedir)) != NULL) {
		if((strcmp(dp->d_name, "..") == 0) ||
		   (strcmp(dp->d_name, ".") == 0))
				continue;

		if(stat(dp->d_name, &st) < 0) {
			log_error(LOG_ARGS, "Could not stat(%s)", dp->d_name);
			continue;
		}

		filename = strdup(dp->d_name);

		if((s = strstr(filename, "-probe"))) {
			probefd = open(filename, O_RDONLY);
			if(probefd < 0)
				continue;
			probetimestr = mygetline(probefd);
			if(probetimestr == NULL)
				continue;
			close(probefd);
			chomp(probetimestr);
			probetime = (time_t)strtol(probetimestr, NULL, 10);
			t = time(NULL);
			if(t - probetime > WAITPROBE) {
				unlink(filename);
				*s = '\0';
				unlink(filename);
			}
		}
		free(filename);
	}

	closedir(bouncedir);

	return 0;
}

int probe_bouncers(const char *listdir, const char *mlmmjbounce)
{
	DIR *bouncedir;
	char *dirname = concatstr(2, listdir, "/bounce/");
	char *probefile;
	struct dirent *dp;
	struct stat st;
	pid_t pid, childpid;
	int status;
	
	if(chdir(dirname) < 0) {
		log_error(LOG_ARGS, "Could not chdir(%s)", dirname);
		free(dirname);
		return 1;
	}
		
	if((bouncedir = opendir(dirname)) == NULL) {
		log_error(LOG_ARGS, "Could not opendir(%s)", dirname);
		free(dirname);
		return 1;
	}

	free(dirname);

	while((dp = readdir(bouncedir)) != NULL) {
		if((strcmp(dp->d_name, "..") == 0) ||
		   (strcmp(dp->d_name, ".") == 0))
				continue;

		if(stat(dp->d_name, &st) < 0) {
			log_error(LOG_ARGS, "Could not stat(%s)", dp->d_name);
			continue;
		}
		
		if(strstr(dp->d_name, "-probe"))
			continue;
		
		probefile = concatstr(2, dp->d_name, "-probe");
		
		/* Skip files which already have a probe out */
		if(stat(probefile, &st) == 0) {
			free(probefile);
			continue;
		}
		free(probefile);

		childpid = fork();
		
		if(childpid < 0) {
			log_error(LOG_ARGS, "Could not fork");
			continue;
		}

		if(childpid > 0) {
			do /* Parent waits for the child */
				pid = waitpid(childpid, &status, 0);
			while(pid == -1 && errno == EINTR);
		} else {
			probefile = strdup(dp->d_name);
			execlp(mlmmjbounce, mlmmjbounce,
					"-L", listdir,
					"-a", probefile,
					"-p", 0);
			log_error(LOG_ARGS, "Could not execlp %s",
						mlmmjbounce);
			return 1;
		}
	}
	closedir(bouncedir);

	return 0;
}

int unsub_bouncers(const char *listdir, const char *mlmmjunsub)
{
	DIR *bouncedir;
	char *dirname = concatstr(2, listdir, "/bounce/");
	char *probefile, *address, *a, *firstbounce, *bouncedata;
	char *logstr;
	struct dirent *dp;
	struct stat st;
	pid_t pid, childpid;
	int status, fd;
	time_t bouncetime, t;
	
	if(chdir(dirname) < 0) {
		log_error(LOG_ARGS, "Could not chdir(%s)", dirname);
		free(dirname);
		return 1;
	}
		
	if((bouncedir = opendir(dirname)) == NULL) {
		log_error(LOG_ARGS, "Could not opendir(%s)", dirname);
		free(dirname);
		return 1;
	}

	free(dirname);

	while((dp = readdir(bouncedir)) != NULL) {
		if((strcmp(dp->d_name, "..") == 0) ||
		   (strcmp(dp->d_name, ".") == 0))
				continue;

		if(stat(dp->d_name, &st) < 0) {
			log_error(LOG_ARGS, "Could not stat(%s)", dp->d_name);
			continue;
		}
		
		if(strstr(dp->d_name, "-probe"))
			continue;
		
		probefile = concatstr(2, dp->d_name, "-probe");
		
		/* Skip files which already have a probe out */
		if(stat(probefile, &st) == 0) {
			free(probefile);
			continue;
		}
		free(probefile);

		/* Get the first line of the bounce file to check if it's
		 * been bouncing for long enough
		 */
		fd = open(dp->d_name, O_RDONLY);
		if(fd < 0) {
			log_error(LOG_ARGS, "Could not open %s", dp->d_name);
			continue;
		}
		firstbounce = mygetline(fd);
		close(fd);
		if(firstbounce == NULL)
			continue;

		/* End the string at the comment */
		a = strchr(firstbounce, '#');
		if(a == NULL) {
			free(firstbounce);
			continue;
		}
		*a = '\0';
		bouncedata = strdup(a+1); /* Save for the log */
		chomp(bouncedata);
		a = strchr(firstbounce, ':');
		if(a == NULL) {
			free(firstbounce);
			continue;
		}

		a++; /* Increase to first digit */
		bouncetime = (time_t)strtol(a, NULL, 10);
		free(firstbounce);
		t = time(NULL);
		if(t - bouncetime < BOUNCELIFE + WAITPROBE)
			continue; /* ok, don't unsub this one */
		
		/* Ok, go ahead and unsubscribe the address */
		address = strdup(dp->d_name);
		a = strchr(address, '=');
		if(a == NULL) { /* skip malformed */
			free(address);
			continue;
		}
		*a = '@';

		childpid = fork();
		
		if(childpid < 0) {
			log_error(LOG_ARGS, "Could not fork");
			continue;
		}

		if(childpid > 0) {
			WRITEMAINTLOG6(5, "UNSUB: ", address, ". Bounced since",
					bouncedata, ".\n");
			free(bouncedata);
			do /* Parent waits for the child */
				pid = waitpid(childpid, &status, 0);
			while(pid == -1 && errno == EINTR);
			unlink(dp->d_name);
		} else {
			execlp(mlmmjunsub, mlmmjunsub,
					"-L", listdir,
					"-a", address, 0);
			log_error(LOG_ARGS, "Could not execlp %s",
						mlmmjunsub);
			return 1;
		}
		free(address);
	}
	closedir(bouncedir);

	return 0;
}

int main(int argc, char **argv)
{
	int opt, daemonize = 1;
	char *bindir, *listdir = NULL, *mlmmjsend, *mlmmjbounce, *mlmmjunsub;
	char *logstr, *logname, *random = random_str();
	char uidstr[16];
	struct stat st;

	CHECKFULLPATH(argv[0]);

	log_set_name(argv[0]);

	while ((opt = getopt(argc, argv, "hFVL:")) != -1) {
		switch(opt) {
		case 'F':
			daemonize = 0;
			break;
		case 'L':
			listdir = optarg;
			break;
		case 'h':
			print_help(argv[0]);
			break;
		case 'V':
			print_version(argv[0]);
			exit(0);
		}
	}

	if(listdir == NULL) {
		fprintf(stderr, "You have to specify -L\n");
		fprintf(stderr, "%s -h for help\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if(chdir(listdir) < 0) {
		log_error(LOG_ARGS, "Could not chdir(%s), exiting. "
				    "No maintenance performed.", listdir);
		exit(EXIT_FAILURE);
	}

	if(stat(listdir, &st) < 0) {
		log_error(LOG_ARGS, "Could not stat listdir '%s'", listdir);
		exit(EXIT_FAILURE);
	}

	chown(logname, st.st_uid, st.st_gid);

	snprintf(uidstr, sizeof(uidstr), "%d", (int)st.st_uid);
	if(setuid(st.st_uid) < 0) {
		log_error(LOG_ARGS, "Could not setuid listdir owner");
		exit(EXIT_FAILURE);
	}
	
	bindir = mydirname(argv[0]);
	mlmmjsend = concatstr(2, bindir, "/mlmmj-send");
	mlmmjbounce = concatstr(2, bindir, "/mlmmj-bounce");
	mlmmjunsub = concatstr(2, bindir, "/mlmmj-unsub");
	free(bindir);

	if(daemonize && daemon(1,0) < 0) {
		log_error(LOG_ARGS, "Could not daemonize. Only one "
				"maintenance run will be done.");
		daemonize = 0;
	}

	for(;;) {
		logname = concatstr(3, listdir, "maintdlog-", random);
		maintdlogfd = open(logname, O_WRONLY|O_EXCL|O_CREAT,
					S_IRUSR|S_IWUSR);
		if(maintdlogfd < 0) {
			log_error(LOG_ARGS, "Could not open maintenance logfile");
			exit(EXIT_FAILURE);
		}
	
		WRITEMAINTLOG4(3, "clean_moderation(", listdir, ");\n");
		clean_moderation(listdir);

		WRITEMAINTLOG4(3, "clean_discarded(", listdir, ");\n");
		clean_discarded(listdir);

		WRITEMAINTLOG6(5, "resend_queue(", listdir, ", ", mlmmjsend,
							");\n");
		resend_queue(listdir, mlmmjsend);

		WRITEMAINTLOG6(5, "resend_requeue(", listdir, ", ", mlmmjsend,
							");\n");
		resend_requeue(listdir, mlmmjsend);

		WRITEMAINTLOG4(3, "clean_nolongerbouncing(", listdir, ");\n");
		clean_nolongerbouncing(listdir);

		WRITEMAINTLOG6(5, "unsub_bouncers(", listdir, ", ",
							mlmmjunsub, ");\n");
		unsub_bouncers(listdir, mlmmjunsub);
		
		WRITEMAINTLOG6(5, "probe_bouncers(", listdir, ", ",
							mlmmjbounce, ");\n");
		probe_bouncers(listdir, mlmmjbounce);

		close(maintdlogfd);
		logstr = concatstr(3, listdir, "/", MAINTD_LOGFILE);
		if(rename(logname, logstr) < 0)
			log_error(LOG_ARGS, "Could not rename(%s,%s)",
						logname, logstr);

		free(logname);
		free(logstr);

		if(daemonize == 0)
			break;
		else
			sleep(MAINTD_SLEEP);
	}
		
	exit(EXIT_SUCCESS);
}
