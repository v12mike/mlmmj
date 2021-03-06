/* Copyright (C) 2002, 2003, 2005 Mads Martin Joergensen <mmj at mmj.dk>
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

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "mylocking.h"

int myexcllock(int fd)
{
	int excllock;
	struct flock locktype;

	locktype.l_type = F_WRLCK;
	locktype.l_whence = SEEK_SET;
	locktype.l_start = 0;
	locktype.l_len = 0;
	do {
		excllock = fcntl(fd, F_SETLKW, &locktype);
	} while(excllock < 0 && errno == EINTR);

	return excllock;
}

int myunlock(int fd)
{
	int unlock;
	struct flock locktype;

	locktype.l_type = F_UNLCK;
	locktype.l_whence = SEEK_SET;
	locktype.l_start = 0;
	locktype.l_len = 0;
	do {
		unlock = fcntl(fd, F_SETLKW, &locktype);
	} while(unlock < 0 && errno == EINTR);

	return unlock;
}
