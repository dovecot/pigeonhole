#ifndef REALPATH_H
#define REALPATH_H

/* Returns path as the normalized absolute path, which means that './'
   and '../' components are resolved, and that duplicate and trailing
   slashes are removed. If it's not already the absolute path, it's
   assumed to be relative to the current working directory.

   NOTE: Be careful with this function. The resolution of '../' components
   with the parent component as if it were a normal directory is not valid
   if the path contains symbolic links.
 */
int t_normpath(const char *path, const char **npath_r);
/* Like t_normpath(), but path is relative to given root. */
int t_normpath_to(const char *path, const char *root,
	const char **npath_r);

/* Returns path as the real normalized absolute path, which means that all
   symbolic links in the path are resolved, that './' and '../' components
   are resolved, and that duplicate and trailing slashes are removed. If it's
   not already the absolute path, it's assumed to be relative to the current
   working directory.

   NOTE: This function calls stat() for each path component and more when
   there are symbolic links (just like POSIX realpath()).
 */
int t_realpath(const char *path, const char **npath_r);
/* Like t_realpath(), but path is relative to given root. */
int t_realpath_to(const char *path, const char *root,
	const char **npath_r);

#endif

