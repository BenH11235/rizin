// SPDX-FileCopyrightText: 2013-2018 pancake <pancake@nopcode.org>
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <rz_util/rz_sys.h>
#include <rz_util/rz_utf8.h>
#include "sdb.h"

RZ_API bool sdb_disk_create(Sdb *s) {
	int nlen;
	char *str;
	const char *dir;
	if (!s || s->fdump >= 0) {
		return false; // cannot re-create
	}
	if (!s->dir && s->name) {
		s->dir = strdup(s->name);
	}
	dir = s->dir ? s->dir : "./";
	RZ_FREE(s->ndump);
	nlen = strlen(dir);
	str = malloc(nlen + 5);
	if (!str) {
		return false;
	}
	memcpy(str, dir, nlen + 1);
	rz_sys_mkdirp(str);
	memcpy(str + nlen, ".tmp", 5);
	if (s->fdump != -1) {
		close(s->fdump);
	}
#if __WINDOWS__ && UNICODE
	wchar_t *wstr = rz_utf8_to_utf16(str);
	if (wstr) {
		s->fdump = _wopen(wstr, O_BINARY | O_RDWR | O_CREAT | O_TRUNC, SDB_MODE);
		free(wstr);
	} else {
		s->fdump = -1;
	}
#else
	s->fdump = open(str, O_BINARY | O_RDWR | O_CREAT | O_TRUNC, SDB_MODE);
#endif
	if (s->fdump == -1) {
		eprintf("sdb: Cannot open '%s' for writing.\n", str);
		free(str);
		return false;
	}
	cdb_make_start(&s->m, s->fdump);
	s->ndump = str;
	return true;
}

RZ_API bool sdb_disk_insert(Sdb *s, const char *key, const char *val) {
	struct cdb_make *c = &s->m;
	if (!key || !val) {
		return false;
	}
	// if (!*val) return 0; //undefine variable if no value
	return cdb_make_add(c, key, strlen(key), val, strlen(val));
}

#define IFRET(x) \
	if (x) \
	ret = 0
RZ_API bool sdb_disk_finish(Sdb *s) {
	bool reopen = false, ret = true;
	IFRET(!cdb_make_finish(&s->m));
#if HAVE_HEADER_SYS_MMAN_H
	IFRET(fsync(s->fdump));
#endif
	IFRET(close(s->fdump));
	s->fdump = -1;
	// close current fd to avoid sharing violations
	if (s->fd != -1) {
		close(s->fd);
		s->fd = -1;
		reopen = true;
	}
#if __WINDOWS__
	LPTSTR ndump_ = rz_utf8_to_utf16(s->ndump);
	LPTSTR dir_ = rz_utf8_to_utf16(s->dir);

	if (MoveFileEx(ndump_, dir_, MOVEFILE_REPLACE_EXISTING)) {
		// eprintf ("Error 0x%02x\n", GetLastError ());
	}
	free(ndump_);
	free(dir_);
#else
	if (s->ndump && s->dir) {
		IFRET(rename(s->ndump, s->dir));
	}
#endif
	free(s->ndump);
	s->ndump = NULL;
	// reopen if was open before
	reopen = true; // always reopen if possible
	if (reopen) {
		int rr = sdb_open(s, s->dir);
		if (ret && rr < 0) {
			ret = false;
		}
		cdb_init(&s->db, s->fd);
	}
	return ret;
}

RZ_API bool sdb_disk_unlink(Sdb *s) {
	return (s->dir && *(s->dir) && unlink(s->dir) != -1);
}
