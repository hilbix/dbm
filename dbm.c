/* $Header$
 *
 * A little general DBM wrapper
 * This source shall be independent of others.  Therefor no tinolib.
 *
 * Copyright (C)2004 by Valentin Hilbig
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Log$
 * Revision 1.3  2004-07-23 19:17:34  tino
 * cosmetic changes
 *
 * Revision 1.2  2004/07/23 18:48:25  tino
 * minor changes, diagnostic dump and batch key insert/replace added
 *
 * Revision 1.1  2004/07/21 20:07:34  tino
 * first version, should do
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
#include <sys/stat.h>

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

static void *
my_realloc(void *ptr, size_t len)
{
  void	*tmp;

  tmp	= (ptr ? realloc(ptr, len) : malloc(len));
  if (!tmp)
    ex("out of memory");
  return tmp;
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
  struct stat	st;

  if (!type)
    type	= "open";
  if (mode!=GDBM_WRCREAT && lstat(name, &st))
    ex("database missing: %s", name);
  if ((db=gdbm_open(name, BUFSIZ, mode, 0775, fatal_func))==0)
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
    {
      gdbm_sync(db);
      gdbm_close(db);
    }
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
	  data.dptr	= my_realloc(data.dptr, m);
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
  struct stat	st;

  if (!lstat(argv[0], &st))
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
  char		*end;

  n	= 1;
  if (argc)
    {
      n	= strtoul(argv[1], &end, 0);
      if (!end || *end)
	ex("wrong value: %s", argv[1]);
    }
  db_open(argv[0], GDBM_READER, NULL);
  if (!db_first())
    exit(1);
  for (i=0;
       fwrite(key.dptr, key.dsize, 1, stdout), (!n || ++i<n) && db_next();
       putchar('\n'));
}

#define	DUMPWIDTH	65	/* 32 for HEX, 64 for ascii	*/

static int
dumpline(const void *ptr, size_t len, int i)
{
  int	mode, c, pos;

  mode	= 0;
  pos	= 0;
  do
    {
      c	= ((unsigned char *)ptr)[i];
      if (isprint(c))
	{
	  if (!mode)
	    {
	      if (++pos>DUMPWIDTH-1)
		break;
	      if ((c=='\"' || c=='\\') && pos>=DUMPWIDTH-1)
		break;
	      putchar('"');
	      mode	= 1;
	    }
	  if (++pos>DUMPWIDTH)
	    break;
	  if (c=='\"' || c=='\\')
	    {
	      if (++pos>DUMPWIDTH)
		break;
	      putchar('\\');
	    }
	  putchar(c);
	  continue;
	}
      if (mode)
	{
	  putchar('"');
	  mode	= 0;
	  pos++;
	}
      if ((pos+=2)>DUMPWIDTH)
	break;
      putchar("0123456789abcdef"[(c>>4)&15]);
      putchar("0123456789abcdef"[c&15]);
    } while (++i<len);
  if (mode)
    putchar('"');
  return i;
}

static void
dump(const void *ptr, size_t len, const char *title)
{
  int	i;

  i	= 0;
  do
    {
      printf("%s %04x ", title, i);
      i	= dumpline(ptr, len, i);
      printf("\n");
    } while (i<len);
}

static void
c_dump(int argc, char **argv)
{
  unsigned long	n, i;
  char		*end;

  n	= 0;
  if (argc)
    {
      n	= strtoul(argv[1], &end, 0);
      if (!end || *end)
	ex("wrong value: %s", argv[1]);
    }
  db_open(argv[0], GDBM_READER, NULL);
  if (!db_first())
    exit(1);
  for (i=0;; )
    {
      data	= gdbm_fetch(db, key);
      dump(key.dptr,  key.dsize, "key");
      dump(data.dptr, data.dsize, "  data");
      if ((n && ++i>=n) || !db_next())
	break;
    }
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

static void
batch_store(int argc, char **argv)
{
  db_data(0, argv);
  if (!gdbm_store(db, key, data, GDBM_INSERT))
    return;
  if (argc)
    {
      db_data(0, argv+1);
      if (!gdbm_store(db, key, data, GDBM_REPLACE))
	return;
    }
  ex("cannot store %s into %s", argv[1], argv[0]);
}

static void
batcher(int argc, char **argv, int term)
{
  char		*buf;
  size_t	len;
  int		c;

  db_open(argv[0], GDBM_WRITER, NULL);
  argc--;
  argv++;
  len	= 0;
  buf	= 0;
  do
    {
      int	i;

      for (i=0; (c=getchar())!=EOF && c!=term; i++)
	{
	  if (i>=len)
	    buf	= my_realloc(buf, len+=BUFSIZ);
	  buf[i]	= c;
	}
      key.dsize	= i;
      key.dptr	= buf;
      batch_store(argc, argv);
    } while (c!=EOF);
}  

static void
c_batch(int argc, char **argv)
{
  batcher(argc, argv, '\n');
}

static void
c_batch0(int argc, char **argv)
{
  batcher(argc, argv, 0);
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
    { "dump",	c_dump,		0, 1	},
    { "insert",	c_insert,	1, 2	},
    { "replace",c_replace,	1, 2	},
    { "delete",	c_delete,	1, 1	},
    { "get",	c_get,		1, 1	},
    { "batch",	c_batch,	1, 2	},
    { "batch0",	c_batch0,	1, 2	},
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
	     "\tlist	[n]	write n keys to stdout, default 1, 0=all\n"
	     "\tdump	[n]	diagnostic dump, default 0=all\n"
	     "\tinsert	key [d]	insert d(ata) under key, must not exist\n"
	     "\t		If d is missing, data is read from stdin\n"
	     "\treplace	key [d]	insert d(ata) under key, must not exist\n"
	     "\t		If d is missing, data is read from stdin\n"
	     "\tdelete	key	delete entry with key\n"
	     "\tget	key	print data under key to stdout\n"
	     "\tbatch	i [r]	read lines for insert/update keys\n"
	     "\t		i is the data to insert.  If r is present,\n"
	     "\t		it is the data to use for replace.\n"
	     "\t		If r is missing, existing keys are ignored.\n"
	     "\tbatch0	i [r]	like batch, but keys are terminated by NUL\n"
#if 0
	     "\tfind	data	hunt for key (slow)\n"
#endif
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
