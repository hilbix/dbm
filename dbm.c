/* $Header$
 *
 * A little general DBM wrapper.
 *
 * This source shall be independent of others.  Therefor no tinolib.
 *
 * Copyright (C)2004-2006 by Valentin Hilbig
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 * $Log$
 * Revision 1.12  2006-06-06 20:47:21  tino
 * timeout corrected
 *
 * Revision 1.11  2006/06/06 00:00:21  tino
 * see Changelog
 *
 * Revision 1.10  2006/06/04 13:33:10  tino
 * filter code fixed, see ChangeLog
 *
 * Revision 1.9  2006/04/11 23:26:00  tino
 * commit for dist
 *
 * Revision 1.8  2004/12/14 00:17:29  tino
 * Version now shown in Usage
 *
 * Revision 1.7  2004/12/13 23:35:39  tino
 * filter command added
 *
 * Revision 1.6  2004/11/19 05:21:56  tino
 * More variants: update and b*
 *
 * Revision 1.5  2004/09/04 22:24:29  tino
 * find and search added
 *
 * Revision 1.4  2004/08/22 05:52:30  Administrator
 * Intermediate version for search functionality
 *
 * Revision 1.3  2004/07/23 19:17:34  tino
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

#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "dbm_version.h"
#include "tino_memwild.h"

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
static int		timeout;
static datum		key, data, kbuf, kbuf2;

static void
db_open(char *name, int mode, const char *type)
{
  struct stat		st;
  struct timespec	hold;
  time_t		now=0;

  if (!type)
    type	= "open";
  if (mode!=GDBM_WRCREAT && lstat(name, &st))
    ex("database missing: %s", name);
  while ((db=gdbm_open(name, BUFSIZ, mode, 0775, fatal_func))==0)
    {
      if (timeout && (gdbm_errno==GDBM_CANT_BE_READER || gdbm_errno==GDBM_CANT_BE_WRITER))
	{
	  if (!now)
	    {
	      time(&now);
	      fprintf(stderr, "sleeping: %s: %s\n", type, gdbm_strerror(gdbm_errno));
	      hold.tv_sec	= 0;
	      hold.tv_nsec	= 0;
	      continue;
	    }
	  else if (time(NULL)-now<=timeout)
	    {
	      if (hold.tv_nsec<500000000l)
		hold.tv_nsec	+= 1000000l;
	      nanosleep(&hold, NULL);
	      continue;
	    }
	}
      ex("cannot %s db: %s", type, name);
    }
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
db_store(int argc, char **argv, int flag, int check)
{
  db_data(argc-2, argv+2);

  db_open(argv[0], GDBM_WRITER, NULL);
  db_key(argv[1]);
  if (check && !gdbm_exists(db, key))
    ex("key does not exist: %s", argv[0]);
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
  for (; i<len; i++)
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
    }
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
  db_store(argc, argv, GDBM_INSERT, 0);
}

static void
c_replace(int argc, char **argv)
{
  db_store(argc, argv, GDBM_REPLACE, 0);
}

static void
c_update(int argc, char **argv)
{
  db_store(argc, argv, GDBM_REPLACE, 1);
}

static void
c_alter(int argc, char **argv)
{
  db_data(argc-3, argv+3);

  db_open(argv[0], GDBM_WRITER, NULL);
  db_key(argv[1]);
  data	= gdbm_fetch(db, key);
  if (!data.dptr)
    ex("key does not exist: %s", argv[0]);
  if (data.dsize!=strlen(argv[2]) || memcmp(data.dptr,argv[2],data.dsize))
    ex("key data mismatch");
  if (gdbm_store(db, key, data, GDBM_REPLACE))
    ex("cannot store %s into %s", argv[1], argv[0]);
}

static void
c_delete(int argc, char **argv)
{
  db_open(argv[0], GDBM_WRITER, NULL);
  db_key(argv[1]);
  if (argc==2)
    {
      data	= gdbm_fetch(db, key);
      if (data.dptr && (data.dsize!=strlen(argv[2]) || memcmp(data.dptr,argv[2],data.dsize)))
	ex("key data mismatch");
    }
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
  if (!db)
    db_open(argv[0], GDBM_WRITER, NULL);
  db_data(0, argv+1);
  if (!gdbm_store(db, key, data, GDBM_INSERT))
    return;
  if (argc>1)
    {
      db_data(0, argv+2);
      if (!gdbm_store(db, key, data, GDBM_REPLACE))
	return;
    }
  ex("cannot store %s into %s", argv[1], argv[0]);
}

static __inline__ void
kbuf_set(int i, char c)
{
  if (i>=kbuf.dsize)
    {
      kbuf.dsize	= i+BUFSIZ;
      kbuf.dptr		= my_realloc(kbuf.dptr, kbuf.dsize);
    }
  kbuf.dptr[i]	= c;
}

static int
read_key_term_c(int term)
{
  int	i, c;

  for (i=0; (c=getchar())!=EOF && c!=term; i++)
    kbuf_set(i, c);
  key		= kbuf;
  key.dsize	= i;
  return c;
}

static void
batcher(int argc, char **argv, int term)
{
  int	c;

  do
    {
      c	= read_key_term_c(term);
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

static void
batcher_new(int argc, char **argv, int term)
{
  int	c;

  while ((c=read_key_term_c(term))!=EOF)
    batch_store(argc, argv);
}  

static void
c_nbatch(int argc, char **argv)
{
  batcher_new(argc, argv, '\n');
}

static void
c_nbatch0(int argc, char **argv)
{
  batcher_new(argc, argv, 0);
}

static int
read_key_term_s(const char *term)
{
  int		i, c, l;
  size_t	len;

  if (!*term)
    {
      for (i=0; (c=getchar())!=EOF && !isspace(c); i++)
        kbuf_set(i, c);
      key	= kbuf;
      key.dsize	= i;
      return c;
    }
  len	= strlen(term)-1;
  l	= term[len];
  for (i=0; (c=getchar())!=EOF; i++)
    {
      if (c==l && i>=len && !memcmp(kbuf.dptr+i-len, term, len))
        {
	  i	-= len;
	  break;
        }
      kbuf_set(i, c);
    }
  key		= kbuf;
  key.dsize	= i;
  return c;
}

static int
read_data_term_s(const char *term)
{
  datum	tmp1, tmp2;
  int	c;

  /* ugly hack: switch to kbuf2
   */
  tmp1	= key;
  tmp2	= kbuf;
  kbuf	= kbuf2;

  c	= read_key_term_s(term);

  /* ugly hack:
   * store key value as data
   */
  data	= key;

  /* ugly hack:
   * switch back
   */
  kbuf2	= kbuf;
  kbuf	= tmp2;
  key	= tmp1;

  return c;
}

static void
c_bdel(int argc, char **argv)
{
  const char	*term;
  int		c;

  term	= (argc ? argv[1] : "");
  while ((c=read_key_term_s(term))!=EOF || key.dsize)
    {
      if (!db)
	db_open(argv[0], GDBM_WRITER, NULL);
      if (gdbm_delete(db, key))
        ex("could not delete '%s': %s", argv[1], argv[0]);
      if (c==EOF)
	break;
    }
}

static void
batch_read(int argc, char **argv, int flag, int check)
{
  const char	*term, *eol;
  int		i, c;

  term	= (argc>0 ? argv[1] : "");
  eol	= (argc>1 ? argv[2] : "");
  if (!*eol)
    eol	= "\n";
  for (i=0; (c=read_key_term_s(term))!=EOF || key.dsize; i++)
    {
      data.dsize	= 0;
      if (c!=EOF)
        c	= read_data_term_s(eol);
      if (!db)
	db_open(argv[0], GDBM_WRITER, NULL);
      if (check && !gdbm_exists(db, key))
        ex("key/data pair %d has missing key", i);
      if (gdbm_store(db, key, data, flag))
        ex("cannot store key/data pair %d", i);
      if (c==EOF)
        break;
    }
}

static void
c_bins(int argc, char **argv)
{
  batch_read(argc, argv, GDBM_INSERT, 0);
}

static void
c_brep(int argc, char **argv)
{
  batch_read(argc, argv, GDBM_REPLACE, 0);
}

static void
c_bupd(int argc, char **argv)
{
  batch_read(argc, argv, GDBM_REPLACE, 1);
}

/* Well, there is a funny side effect, which is a feature:
 * Uh .. forgot to note that .. think this is missing now,
 * as tino_memwildcmp is used.
 */
static void
hunt(int match, int wild, int argc, char **argv)
{
  unsigned long	n, i;
  char		*end;
  int		*tot=alloca((argc+1)*sizeof *tot);
  int		j;

  n	= strtoul(argv[1], &end, 0);
  if (!end || *end)
    ex("wrong value: %s", argv[1]);

  db_open(argv[0], GDBM_READER, NULL);
  if (!db_first())
    exit(1);
  i	= 0;
  for (j=2; j<=argc; j++)
    tot[j]	= strlen(argv[j]);
  do
    {
      int	ok;

      data	= gdbm_fetch(db, key);
      ok	= 0;
      for (j=2; j<=argc; j++)
	{
	  if (wild)
	    {
	      if (tino_memwildcmp(data.dptr, data.dsize, argv[j], tot[j]))
		continue;
	    }
	  else if (data.dsize!=tot[j] || strncmp(argv[j], data.dptr, tot[j]))
	    continue;
	  ok	= 1;
	  break;
	}
      if (ok==match)
	{
	  fwrite(key.dptr, key.dsize, 1, stdout);
	  putchar('\n');
	  if (n && ++i>=n)
	    return;
	}
    }
  while (db_next());
}

static void
c_find(int argc, char **argv)
{
  hunt(1, 0, argc, argv);
}

static void
c_search(int argc, char **argv)
{
  hunt(1, 1, argc, argv);
}

static void
c_nfind(int argc, char **argv)
{
  hunt(0, 0, argc, argv);
}

static void
c_nsearch(int argc, char **argv)
{
  hunt(0, 1, argc, argv);
}

#if 1
#define	DP(X)	do { ; } while (0)
#else
#define	DP(X)	do { dprintf X; } while (0)
static void
dprintf(const char *s, ...)
{
  va_list	list;

  fprintf(stderr, "[");
  va_start(list, s);
  vfprintf(stderr, s, list);
  va_end(list);
  fprintf(stderr, "]\n");
}
#endif

static void
c_filter(int argc, char **argv)
{
  const char	*term, *d;
  int		k, m, p, i, c, dl;

  term	= (argc>0 ? argv[1] : "");
  d	= (argc>1 ? argv[2] : NULL);
  dl	= 0;
  if (d)
    dl	= strlen(d);
  k	= *term ? *term++!='0' : 0;
  m	= *term ? *term++!='0' : 0;
  p	= *term ? *term++!='0' : 0;
  if (!*term)
    term	= "\n";
  for (i=0; !feof(stdout) && ((c=read_key_term_s(term))!=EOF || key.dsize); i++)
    {
      if (!db)
	db_open(argv[0], GDBM_READER, NULL);
      DP(("key %.*s", key.dsize, key.dptr));
      data	= gdbm_fetch(db, key);
      DP(("data %*s", data.dsize, data.dptr));
      /* Argh:
       * We have following cases:
       * kdm vars from above, e=key exists, c=datamatch p=print (-=any)
       * kdm ec	p
       *
       * 00- 0-	1
       * 00- 1- 0
       * 010 0-	1
       * 010 10	1
       * 010 11	0
       * 011 0-	1
       * 011 10	0
       * 011 11	1
       *
       * 10- 0- 0
       * 10- 1- 1
       * 110 0-	0
       * 110 10	1
       * 110 11	0
       * 111 0-	0
       * 111 10	0
       * 111 11	1
       *
       * Compressed:
       * 
       * 0-- 0-	1
       * 1-- 0-	0
       *
       * 00- 1- 0
       * 10- 1- 1
       *
       * -10 10	1
       * -11 11	1
       * -10 11	0
       * -11 10	0
       *
       * Latter can be written as (with X := !x):
       *
       * -1x 1x	1
       * -1x 1X	0
       */
      if (!data.dptr)
	{			/* x-- 0- X	*/
	  if (k)
	    continue;		/* 1-- 0- 0	*/
	}			/* 0-- 0- 1	*/
      else if (!d)
	{			/* x0- 1- x	*/
	  if (!k)
	    continue;		/* 00- 1- 0	*/
	}			/* 10- 1- 1	*/
      else
	{			/* -1? 1? ?	*/
	  DP(("check"));
	  if ((p
		? !tino_memwildcmp(data.dptr, data.dsize, d, dl)
		: (data.dsize==dl && !strncmp(d, data.dptr, dl))
	      )!=m)
	   continue;		/* -1x 1X 0	*/
	}			/* -1x 1x 1	*/
      DP(("put"));
      fwrite(key.dptr, key.dsize, 1, stdout);
      fputs(term, stdout);
    }
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
    { "update",	c_update,	1, 2	},
    { "alter",	c_alter,	2, 3	},
    { "delete",	c_delete,	1, 2	},
    { "get",	c_get,		1, 1	},
    { "batch",	c_batch,	1, 2	},
    { "batch0",	c_batch0,	1, 2	},
    { "nbatch",	c_nbatch,	1, 2	},
    { "nbatch0", c_nbatch0,	1, 2	},
    { "bdel",	c_bdel,		0, 1	},
    { "bins",	c_bins,		0, 2	},
    { "brep",	c_brep,		0, 2	},
    { "bupd",	c_bupd,		0, 2	},
    { "filter",	c_filter,	0, 2	},
    { "find",	c_find,		2, -1	},
    { "search",	c_search,	2, -1	},
    { "nfind",	c_nfind,	2, -1	},
    { "nsearch",c_nsearch,	2, -1	},
  };

int
main(int argc, char **argv)
{
  int	i;
  char	*arg0;

  arg0	= argv[0];

  if (argc>1 && argv[1][0]=='-' && argv[1][1]=='t')	/* actually a hack	*/
    argc--, timeout=atoi(*++argv+2);			/* set timeout	*/
  if (argc<2)
    {
      printf("Usage: %s [-tSEC] action gdbm-file [args...]\n"
	     "\tVersion %s compiled %s\n"
	     "\n"
	     "\t-tSEC	timeout in seconds, -1=unlimited, 0=none (default)\n"
	     "\n"
	     "\tAction	Args	Description:\n"
	     "\t-------	-------	------------\n"
	     "\tcreate	-	create new DBM file, fails if exists\n"
	     "\tkill	-	erase DBM file if empty\n"
	     "\treorg	-	reorganize DBM\n"
	     "\n"
	     "\tlist	[n]	write n keys to stdout, default 1, 0=all\n"
	     "\tdump	[n]	diagnostic dump, default 0=all\n"
	     "\n"
	     "\tinsert	key [d]	insert d(ata) under key, must not exist\n"
	     "\t		If d is missing, data is read from stdin\n"
	     "\tupdate	key [d]	as before, but key must exist.\n"
	     "\treplace	key [d]	as before, but replace (insert+update)\n"
	     "\tdelete	key [d]	delete entry with key given and data matches\n"
	     "\talter	k o [d]	update key with original(o) data\n"
	     "\n"
	     "\tget	key	print data under key to stdout\n"
	     "\n"
	     "\tbatch	i [r]	read lines for insert/update keys\n"
	     "\t		i is the data to insert.  If r is present,\n"
	     "\t		it is the data to use for replace.\n"
	     "\tbatch0	i [r]	like batch, but keys are terminated by NUL\n"
	     "\t		(find . -type f -print0 | dbm batch0 db x y)\n"
	     "\tnbatch	i [r]	like batch, but requires line terminator\n"
	     "\tnbatch0	i [r]	like batch0, but requires line terminator 0\n"
	     "\n"
	     "\tbins	[s [t]]	insert key/data read from stdin\n"
	     "\t		s is the key terminator, default ''=blanks.\n"
	     "\t		t is the data terminator, default ''=LF.\n"
	     "\tbrep	[s [t]]	as before, but use replace\n"
	     "\tbupd	[s [t]]	as before, but use update\n"
	     "\tbdel	[s]	delete keys read from stdin\n"
	     "\n"
	     "\tfilter	kmpt d	filter keys read from stdin (default: kmpt=000)\n"
	     "\t		Key must (k!=0) or must not exist (k=0)\n"
	     "\t		Data must match (m!=0) or must not match (m=0)\n"
	     "\t		Data match is exact (p=0) or pattern (p!=0)\n"
	     "\t		t is the line terminater, default ''=LF\n"
	     "\n"
	     "\tfind	n data	find n keys which have exact data (slow), 0=all\n"
	     "\t		Multiple data arguments give alternatives (=OR)\n"
	     "\tsearch	n patrn	as before, but use patterns\n"
	     "\tnfind	n data	like find, but data must not match\n"
	     "\tnsearch	n patrn	like search, but data must not match\n"
	     "\n"
	     "\tpattern help:   ?, *, [^xyz] or [a-z] are supported.  Hints:\n"
	     "\t		[*], [?], [[] matches literal *, ?, [ respectively\n"
	     "\t		[[-[-] matches [ or -, [z-a] matches b to y\n"
	     , arg0, DBM_VERSION, __DATE__);
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
