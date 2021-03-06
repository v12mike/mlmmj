#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include "mlmmj.h"
#include "log_error.h"
#include "memory.h"
#include "strgen.h"
#include "getaddrsfromfd.h"

off_t getaddrsfromfd(struct strlist *slist, int fd, int max)
{
	off_t offset = lseek(fd, 0, SEEK_CUR);
	char *start, *cur, *next;
	struct stat st;
	size_t len;

	if(fstat(fd, &st) < 0) {
		log_error(LOG_ARGS, "Could not fstat fd");
		return -1;
	}
	/* mmap of 0-bytes is invalid */
	if(st.st_size == 0) {
		return 0;
	}

	start = mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if(start == MAP_FAILED) {
		log_error(LOG_ARGS, "Could not mmap fd");
		return -1;
	}
	
	for(next = cur = (start + offset); next < start + st.st_size; next++) {
		if(*next == '\n' || next == start + st.st_size - 1) {
			slist->count++;
			len = next - cur;
			if(next == start + st.st_size - 1 && *next != '\n')
				len++;
			slist->strs = (char **)myrealloc(slist->strs,
					sizeof(char *) * slist->count);
			slist->strs[slist->count - 1] = mymalloc(len + 1);
			strncpy(slist->strs[slist->count - 1], cur, len);
			slist->strs[slist->count - 1][len] = '\0';
			cur = next + 1;
		} else {
			continue;
		}
		if(slist->count >= max) {
			offset = (off_t)(cur - start);
			goto donegetting;
		}
	}
	offset = st.st_size;
donegetting:			
	munmap(start, st.st_size);
	lseek(fd, offset, SEEK_SET);
	return st.st_size - offset;
}
