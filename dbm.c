/* $Header$
 *
 * Copyright (C)2004 by Valentin Hilbig
 *
 * A little general DBM wrapper
 *
 * This source shall be independent of others.  Therefor no tinolib.
 *
 * $Log$
 * Revision 1.1  2004-07-21 20:07:34  tino
 * first version, should do
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>

#include <gdbm.h>

#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>

#include "dbm_version.h"

static void
ex(const char *s, ...)
{
  va_list	list;
  int		e;

  e	= errno;
  fprintf(stderr, "error: ");
  va_start(list, s);
  vfprintf(stderr, s, list);
  va_end(list);
  fprintf(stderr, ": %s (%s)\n", strerror(e), gdbm_strerror(gdbm_errno));
  exit(-1);
}

static void
fatal_func(const char *s)
{
  ex("gdbm fatal: %s\n", s);
}

static GDBM_FILE	db;
static datum		key, data;

static void
db_open(char *name, int mode, const char *type)
{
  if (!type)
    type	= "open";
  if ((db=gdbm_open(name, BUFSIZ, mode|GDBM_SYNC, 0775, fatal_func))==0)
    ex("cannot %s db: %s", type, name);
}

static int
db_first(void)
{
  key	= gdbm_firstkey(db);
  return key.dptr!=0;
}

static int
db_next(void)
{
  key	= gdbm_nextkey(db, key);
  return key.dptr!=0;
}

static void
db_close(void)
{
  if (db)
    gdbm_close(db);
  db	= 0;
}

static void
db_key(char *s)
{
  key.dptr	= s;
  key.dsize	= strlen(s);
}

static void
db_data(int argc, char **argv)
{
  data.dptr	= 0;
  data.dsize	= 0;
  if (argc>0)
    ex("too many parameters");
  if (argc==0)
    {
      data.dptr	= argv[0];
      data.dsize= strlen(argv[0]);
    }
  else
    {
      do
	{
	  int	n, m;

	  m		= data.dsize+BUFSIZ*16;
	  data.dptr	= (data.dptr ? realloc(data.dptr, m) : malloc(m));
	  if (!data.dptr)
	    ex("out of memory");
	  n = fread(((char *)data.dptr)+data.dsize, 1, m-data.dsize, stdin);
	  if (n<0 || ferror(stdin))
	    ex("read error");
	  data.dsize	+= n;
	} while (!feof(stdin));
    }
}

static void
db_store(int argc, char **argv, int flag)
{
  db_open(argv[0], GDBM_WRITER, NULL);
  db_key(argv[1]);
  db_data(argc-2, argv+2);
  if (gdbm_store(db, key, data, flag))
    ex("cannot store %s into %s", argv[1], argv[0]);
}

static void
c_create(int argc, char **argv)
{
  FILE	*fd;

  if ((fd=fopen(argv[0], "r"))!=0)
    ex("already exists: %s", argv[0]);
  if (errno!=ENOENT)
    ex("not supported error return: %s", argv[0]);
  db_open(argv[0], GDBM_WRCREAT, "create");
}

static void
c_kill(int argc, char **argv)
{
  db_open(argv[0], GDBM_WRITER, NULL);
  if (db_first())
    ex("database not empty: %s", argv[0]);
  if (unlink(argv[0]))
    ex("cannot unlink: %s", argv[0]);
}

static void
c_reorg(int argc, char **argv)
{
  db_open(argv[0], GDBM_WRITER, NULL);
  if (gdbm_reorganize(db))
    ex("error reorganizing: %s", argv[0]);
}

static void
c_list(int argc, char **argv)
{
  unsigned long	n, i;

  n	= 1;
  if (argc)
    n	= strtoul(argv[1], NULL, 0);
  if (!n)
    ex("wrong value: %s", argv[1]);
  db_open(argv[0], GDBM_READER, NULL);
  if (!db_first())
    exit(1);
  for (i=0;
       fwrite(key.dptr, key.dsize, 1, stdout), ++i<n && db_next();
       putchar('\n'));
}

static void
c_insert(int argc, char **argv)
{
  db_store(argc, argv, GDBM_INSERT);
}

static void
c_replace(int argc, char **argv)
{
  db_store(argc, argv, GDBM_REPLACE);
}

static void
c_delete(int argc, char **argv)
{
  db_open(argv[0], GDBM_WRITER, NULL);
  db_key(argv[1]);
  if (gdbm_delete(db, key))
    ex("could not delete '%s': %s", argv[1], argv[0]);
}

static void
c_get(int argc, char **argv)
{
  db_open(argv[0], GDBM_READER, NULL);
  db_key(argv[1]);
  data	= gdbm_fetch(db, key);
  if (!data.dptr)
    exit(1);
  fwrite(data.dptr, data.dsize, 1, stdout);
}

struct
  {
    const char	*command;
    void	(*fn)(int argc, char **argv);
    int		minargs, maxargs;
  } actions[] =
  {
    { "create",	c_create,	0, 0 	},
    { "kill",	c_kill,		0, 0	},
    { "reorg",	c_reorg,	0, 0	},
    { "list",	c_list,		0, 1	},
    { "insert",	c_insert,	1, 2	},
    { "replace",c_replace,	1, 2	},
    { "delete",	c_delete,	1, 1	},
    { "get",	c_get,		1, 1	},
#if 0
    { "find",	c_find,		1, 1	},
#endif
  };

int
main(int argc, char **argv)
{
  int	i;

  if (argc<2)
    {
      printf("Usage: %s action gdbm-file [args...]\n"
	     "\tVersion compiled %s\n"
	     "\n"
	     "\tAction	Args	Description:\n"
	     "\t-------	-------	------------\n"
	     "\tcreate	-	create new DBM file, fails if exists\n"
	     "\tkill	-	erase DBM file if empty\n"
	     "\treorg	-	reorganize DBM\n"
	     "\n"
	     "\tlist	[n]	write first n keys to stdout, default 1\n"
	     "\tinsert	key [d]	insert d(ata) under key, must not exist\n"
	     "\t		If d is missing, data is read from stdin\n"
	     "\treplace	key [d]	insert d(ata) under key, must not exist\n"
	     "\t		If d is missing, data is read from stdin\n"
	     "\tdelete	key	delete entry with key\n"
	     "\tget	key	print data under key to stdout\n"
	     "\tfind	data	hunt for key (slow)\n"
	     , argv[0], __DATE__);
      return 1;
    }
  for (i=sizeof actions/sizeof *actions; --i>=0; )
    if (!strcmp(actions[i].command, argv[1]))
      {
	argc	-= 3;
	argv	+= 2;
	if (argc<0)
	  ex("missing database name for command '%s'", actions[i].command);
	if (argc<actions[i].minargs ||
	    (actions[i].maxargs!=-1 && argc>actions[i].maxargs))
	  ex("wrong number of parameters for command '%s': %d (%d-%d)",
	     actions[i].command, argc, actions[i].minargs, actions[i].maxargs
	     );
	actions[i].fn(argc, argv);
	db_close();
	return 0;
      }
  ex("unknown command: '%s'", argv[1]);
  return 0;
}
