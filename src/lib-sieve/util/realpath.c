/* Copyright (c) 2009-2018 Pigeonhole authors, see the included COPYING file */

#include "lib.h"
#include "str.h"

#include "realpath.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

// FIXME: move/merge to Dovecot

#define REALPATH_MAX_PATH      8*1024
#define REALPATH_MAX_SYMLINKS  80

static int t_getcwd_alloc(char **dir_r, size_t *asize_r)
{
	/* @UNSAFE */
	char *dir;
	size_t asize = 128;

	dir = t_buffer_get(asize);
	while (getcwd(dir, asize) == NULL) {
		if (errno != ERANGE)
			return -1;
		asize = nearest_power(asize+1);
		dir = t_buffer_get(asize);
	}
	*asize_r = asize;
	*dir_r = dir;
	return 0;
}

static int path_normalize(const char *path, bool resolve_links,
	const char **npath_r)
{
	/* @UNSAFE */
	unsigned int link_count = 0;
	char *npath, *npath_pos;
	const char *p;
	size_t asize;

	if (path[0] != '/') {
		/* relative; initialize npath with current directory */
		if (t_getcwd_alloc(&npath, &asize) < 0)
			return -1;
		npath_pos = npath + strlen(npath);
		i_assert(npath[0] == '/');
	} else {
		/* absolute; initialize npath with root */
		asize = 128;
		npath = t_buffer_get(asize);
		npath[0] = '/';
		npath_pos = npath + 1;
	}

	p = path;
	while (*p != '\0') {
		struct stat st;
		ptrdiff_t seglen;
		const char *segend;

		/* skip duplicate slashes */
		while (*p == '/')
			p++;

		/* find end of path segment */
		for (segend = p; *segend != '\0' && *segend != '/'; segend++);

		if (segend == p)
			break; /* '\0' */
		seglen = segend - p;
		if (seglen == 1 && p[0] == '.') {
			/* a reference to this segment; nothing to do */
		} else if (seglen == 2 && p[0] == '.' && p[1] == '.') {
			/* a reference to parent segment; back up to previous slash */
			i_assert(npath_pos >= npath);
			if ((npath_pos - npath) > 1) {
				if (*(npath_pos-1) == '/')
					npath_pos--;
				for (; *(npath_pos-1) != '/'; npath_pos--);
			}
		} else {
			/* allocate space if necessary */
			i_assert(npath_pos >= npath);
			if ((size_t)((npath_pos - npath) + seglen + 1) >= asize) {
				ptrdiff_t npath_offset = npath_pos - npath;
				asize = nearest_power(npath_offset + seglen + 2);
				npath = t_buffer_reget(npath, asize);
				npath_pos = npath + npath_offset;
			}

			/* make sure npath now ends in slash */
			i_assert(npath_pos > npath);
			if (*(npath_pos-1) != '/') {
				i_assert((size_t)((npath_pos - npath) + 1) < asize);
				*(npath_pos++) = '/';
			}

			/* copy segment to normalized path */
			i_assert(npath_pos >= npath);
			i_assert((size_t)((npath_pos - npath) + seglen) < asize);
			memmove(npath_pos, p, seglen);
			npath_pos += seglen;
		}

		if (resolve_links) {
			/* stat path up to here (segend points to tail) */
			*npath_pos = '\0';
			if (lstat(npath, &st) < 0)
				return -1;

			if (S_ISLNK (st.st_mode)) {
				/* symlink */
				char *npath_link;
				size_t lsize = 128, tlen = strlen(segend), espace;
				size_t ltlen = (link_count == 0 ? 0 : tlen);
				ssize_t ret;

				/* limit link dereferences */
				if (++link_count > REALPATH_MAX_SYMLINKS) {
					errno = ELOOP;
					return -1;
				}

				/* allocate space for preserving tail of previous symlink and
				   first attempt at reading symlink with room for the tail

				   buffer will look like this:
				   [npath][0][preserved tail][link buffer][room for tail][0]
				 */
				espace = ltlen + tlen + 2;
				i_assert(npath_pos >= npath);
				if ((size_t)((npath_pos - npath) + espace + lsize) >= asize) {
					ptrdiff_t npath_offset = npath_pos - npath;
					asize = nearest_power((npath_offset + espace + lsize) + 1);
					lsize = asize - (npath_offset + espace);
					npath = t_buffer_reget(npath, asize);
					npath_pos = npath + npath_offset;
				}

				if (ltlen > 0) {
					/* preserve tail just after end of npath */
					i_assert(npath_pos >= npath);
					i_assert((size_t)((npath_pos + 1 - npath) + ltlen) < asize);
					memmove(npath_pos + 1, segend, ltlen);
				}

				/* read the symlink after the preserved tail */
				for (;;) {
					npath_link = (npath_pos + 1) + ltlen;

					i_assert(npath_link >= npath_pos);
					i_assert((size_t)((npath_link - npath) + lsize) < asize);

					/* attempt to read the link */
					if ((ret=readlink(npath, npath_link, lsize)) < 0)
						return -1;
					if ((size_t)ret < lsize) {
						/* POSIX doesn't guarantee the presence of a NIL */
						npath_link[ret] = '\0';
						break;
					}

					/* sum of new symlink content length and path tail length may not
					   exceed maximum */
					if ((size_t)(ret + tlen) >= REALPATH_MAX_PATH) {
						errno = ENAMETOOLONG;
						return -1;
					}

					/* try again with bigger buffer,
					   we need to allocate more space as well if lsize == ret,
					   because the returned link may have gotten truncated */
					espace = ltlen + tlen + 2;
					i_assert(npath_pos >= npath);
					if ((size_t)((npath_pos - npath) + espace + lsize) >= asize ||
					    lsize == (size_t)ret) {
						ptrdiff_t npath_offset = npath_pos - npath;
						asize = nearest_power((npath_offset + espace + lsize) + 1);
						lsize = asize - (npath_offset + espace);
						npath = t_buffer_reget(npath, asize);
						npath_pos = npath + npath_offset;
					}
				}

				/* add tail of previous path at end of symlink */
				i_assert(npath_link >= npath);
				if (ltlen > 0) {
					i_assert(npath_pos >= npath);
					i_assert((size_t)((npath_pos - npath) + 1 + tlen) < asize);
					i_assert((size_t)((npath_link - npath) + ret + tlen) < asize);
					memcpy(npath_link + ret, npath_pos + 1, tlen);
				} else {
					i_assert((size_t)((npath_link - npath) + ret + tlen) < asize);
					memcpy(npath_link + ret, segend, tlen);
				}
				*(npath_link+ret+tlen) = '\0';

				/* use as new source path */
				path = segend = npath_link;

				if (path[0] == '/') {
					/* absolute symlink; start over at root */
					npath_pos = npath + 1;
				} else {
					/* relative symlink; back up to previous segment */
					i_assert(npath_pos >= npath);
					if ((npath_pos - npath) > 1) {
						if (*(npath_pos-1) == '/')
							npath_pos--;
						for (; *(npath_pos-1) != '/'; npath_pos--);
					}
				}

			} else if (*segend != '\0' && !S_ISDIR (st.st_mode)) {
				/* not last segment, but not a directory either */
				errno = ENOTDIR;
				return -1;
			}
		}

		p = segend;
	}

	i_assert(npath_pos >= npath);
	i_assert((size_t)(npath_pos - npath) < asize);

	/* remove any trailing slash */
	if ((npath_pos - npath) > 1 && *(npath_pos-1) == '/')
		npath_pos--;

	*npath_pos = '\0';

	t_buffer_alloc(npath_pos - npath + 1);
	*npath_r = npath;
	return 0;
}

int t_normpath(const char *path, const char **npath_r)
{
	return path_normalize(path, FALSE, npath_r);
}

int t_normpath_to(const char *path, const char *root,
	const char **npath_r)
{
	if (*path == '/')
		return t_normpath(path, npath_r);

	return t_normpath(t_strconcat(root, "/", path, NULL), npath_r);
}

int t_realpath(const char *path, const char **npath_r)
{
	return path_normalize(path, TRUE, npath_r);
}

int t_realpath_to(const char *path, const char *root,
	const char **npath_r)
{
	if (*path == '/')
		return t_realpath(path, npath_r);

	return t_realpath(t_strconcat(root, "/", path, NULL), npath_r);
}
