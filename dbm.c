/* $Header$
 *
 * A little general DBM wrapper.
 *
 * This source shall be independent of others.  Therefor no tinolib.
 *
 * Copyright (C)2004-2007 by Valentin Hilbig <webmaster@scylla-charybdis.com>
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
 * Revision 1.20  2007-01-23 17:08:42  tino
 * Another try if everything works
 *
 * Revision 1.19  2007/01/18 18:12:38  tino
 * Lot of changes, see ChangeLog and ANNOUNCE.  Commit before function checks.
 *
 * Revision 1.18  2007/01/13 01:03:59  tino
 * db_close() called before errors are printed.
 * This is in preparation to add the changes for option -a
 *
 * Revision 1.17  2006/08/12 12:26:26  tino
 * option -q
 *
 * Revision 1.16  2006/08/09 23:11:08  tino
 * Test script added.  Well it's very small for now.
 *
 * Revision 1.15  2006/07/22 00:49:04  tino
 * See ChangeLog
 *
 * Revision 1.14  2006/07/15 14:51:54  tino
 * alter command corrected
 *
 * Revision 1.13  2006/06/11 01:22:11  tino
 * Better return values
 *
 * Revision 1.12  2006/06/06 20:47:21  tino
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

static int ignoresleep, advancedsleep;
static const char	*db_name;

static void db_close(void);

static void
ex(int nr, const char *s, ...)
{
  va_list	list;
  int		e, ge;
  static int	inerr;

  e	= errno;
  ge	= gdbm_errno;
  if (inerr)
    fprintf(stderr, "error within error");
  else
    {
      inerr	= 1;
      db_close();
      fprintf(stderr, "error");
    }
  if (db_name)
    fprintf(stderr, " %s: ", db_name);
  else
    fprintf(stderr, ": ");
  va_start(list, s);
  vfprintf(stderr, s, list);
  va_end(list);
  fprintf(stderr, ": %s (%s)\n", strerror(e), gdbm_strerror(ge));
  exit(nr ? nr : 10);
}

static void *
my_realloc(void *ptr, size_t len)
{
  void	*tmp;

  tmp	= (ptr ? realloc(ptr, len) : malloc(len));
  if (!tmp)
    ex(0, "out of memory");
  return tmp;
}

static void
fatal_func(const char *s)
{
  ex(0, "gdbm fatal: %s\n", s);
}

static GDBM_FILE	db;
static int		timeout;
static datum		key, data, kbuf, kbuf2;
static int		data_alloc;

static void
data_free(void)
{
  if (data_alloc && data.dptr)
    free(data.dptr);
  data_alloc	= 0;
  data.dptr	= 0;
  data.dsize	= 0;
}

static void
db_open(char *name, int mode, const char *type)
{
  static int		seeded;
  struct stat		st;
  struct timespec	hold;
  time_t		now=0;

  db_name	= name;
  if (!type)
    type	= "open";
  if (mode!=GDBM_WRCREAT && lstat(name, &st))
    ex(0, "database missing");
  while ((db=gdbm_open(name, BUFSIZ, mode, 0775, fatal_func))==0)
    {
      if (errno==EBUSY || gdbm_errno==GDBM_CANT_BE_READER || gdbm_errno==GDBM_CANT_BE_WRITER)
	{
	  if (timeout)
	    {
	      if (!now)
		{
		  time(&now);
		  if (!ignoresleep)
		    fprintf(stderr, "sleeping: %s: %s\n", type, gdbm_strerror(gdbm_errno));
		  hold.tv_sec	= 0;
		  hold.tv_nsec	= 0;
		  continue;
		}
	      if (timeout<0 || time(NULL)-now<=timeout)
		{
  		  if (!seeded)
		    srand(getpid());	/* we need different random for each process	*/
		  seeded	= 1;
		  if (hold.tv_nsec<500000000l)
		    hold.tv_nsec	+= 1000000l;
		  hold.tv_nsec -= rand()&0xffff;	/* randomnize the wait	*/
		  nanosleep(&hold, NULL);
		  errno	= 0;	/* CygWin fix: Sometimes open fails with EBUSY */
		  continue;
		}
	    }
	  ex(-1, "timeout %s db", type);
	}
      ex(0, "cannot %s db", type);
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
  void	*old;
  if ((old=key.dptr)!=0)
    {
      key	= gdbm_nextkey(db, key);
      free(old);	/* we came from db_first()	*/
    }
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
db_close_if(void)
{
  if (advancedsleep)
    db_close();
}

static void
db_key(char *s)
{
  key.dptr	= s;
  key.dsize	= strlen(s);
}

static void
db_get(void)
{
  data_free();
  data	= gdbm_fetch(db, key);
  data_alloc	= 1;
}

static void
db_data(int argc, char **argv)
{
  data_free();
  if (argc>0)
    ex(0, "too many parameters");
  if (argc==0)
    {
      data.dptr	= argv[0];
      data.dsize= strlen(argv[0]);
    }
  else
    {
      data_alloc	= 1;
      do
	{
	  int	n, m;

	  m		= data.dsize+BUFSIZ*16;
	  data.dptr	= my_realloc(data.dptr, m);
	  n = fread(((char *)data.dptr)+data.dsize, 1, m-data.dsize, stdin);
	  if (n<0 || ferror(stdin))
	    ex(0, "read error");
	  data.dsize	+= n;
	} while (!feof(stdin));
    }
}

static int
db_put(char *name, int replace, int check)
{
  if (!db)
    db_open(name, GDBM_WRITER, NULL);
  if (check>0 && !gdbm_exists(db, key))
    ex(1, "key does not exist: %.*s", (int)key.dsize, key.dptr);
  if (gdbm_store(db, key, data, replace ? GDBM_REPLACE : GDBM_INSERT))
    {
      if (check<0)
	return 1;
      ex(2, "cannot store %.*s into %.*s", (int)data.dsize, data.dptr, (int)key.dsize, key.dptr);
    }
  return 0;
}

static void
db_store(int argc, char **argv, int flag, int check)
{
  db_data(argc-2, argv+2);
  db_key(argv[1]);
  db_put(argv[0], flag, check);
}

static void
db_replace(datum d)
{
  if (gdbm_store(db, key, d, GDBM_REPLACE))
    ex(2, "cannot store %.*s into %.*s", (int)d.dsize, d.dptr, (int)key.dsize, key.dptr);
}

static void
db_cmp(const char *val)
{
  db_get();
  if (!data.dptr)
    ex(1, "key does not exist: %.*s", (int)key.dsize, key.dptr);
  if (data.dsize!=strlen(val) || memcmp(data.dptr,val,data.dsize))
    ex(2, "key data mismatch: %.*s", (int)key.dsize, key.dptr);
}

static void
db_delete(char *name, const char *cmp)
{
  if (!db)
    db_open(name, GDBM_WRITER, NULL);
  if (cmp)
    db_cmp(cmp);
  if (gdbm_delete(db, key))
    ex(1, "could not delete %.*s", (int)key.dsize, key.dptr);
}

static void
c_create(int argc, char **argv)
{
  struct stat	st;

  if (!lstat(argv[0], &st))
    ex(0, "already exists");
  if (errno!=ENOENT)
    ex(0, "unsupported error return value");
  db_open(argv[0], GDBM_WRCREAT, "create");
}

static void
c_kill(int argc, char **argv)
{
  db_open(argv[0], GDBM_WRITER, NULL);
  if (db_first())
    ex(0, "database not empty");
  if (unlink(argv[0]))
    ex(0, "cannot unlink");
}

static void
c_reorg(int argc, char **argv)
{
  db_open(argv[0], GDBM_WRITER, NULL);
  if (gdbm_reorganize(db))
    ex(0, "error reorganizing");
}

static void
c_list(int argc, char **argv)
{
  unsigned long	n, i;
  char		*end, *sep;
  size_t	seplen;

  n	= 1;
  if (argc>0)
    {
      n	= strtoul(argv[1], &end, 0);
      if (!end || *end)
	ex(0, "wrong value: %s", argv[1]);
    }
  db_open(argv[0], GDBM_READER, NULL);
  if (!db_first())
    ex(1, "empty database");
  sep		= argc>1 ? argv[2] : "\n";
  seplen	= *sep ? strlen(sep) : 1;
  for (i=0;
       fwrite(key.dptr, key.dsize, 1, stdout), (!n || ++i<n) && db_next();
       fwrite(sep, seplen, 1, stdout));
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
	ex(0, "wrong value: %s", argv[1]);
    }
  db_open(argv[0], GDBM_READER, NULL);
  if (!db_first())
    ex(1, "empty database");
  for (i=0;; )
    {
      db_get();
      dump(key.dptr,  key.dsize, "key");
      dump(data.dptr, data.dsize, "  data");
      if ((n && ++i>=n) || !db_next())
	break;
    }
}

static void
c_insert(int argc, char **argv)
{
  db_store(argc, argv, 0, 0);
}

static void
c_replace(int argc, char **argv)
{
  db_store(argc, argv, 1, 0);
}

static void
c_update(int argc, char **argv)
{
  db_store(argc, argv, 1, 1);
}

static void
c_alter(int argc, char **argv)
{
  datum	o;

  db_data(argc-3, argv+3);

  /* ugly hack:
   *
   * db_cmp kills datum data
   */
  o		= data;
  data.dptr	= 0;

  db_open(argv[0], GDBM_WRITER, NULL);
  db_key(argv[1]);
  db_cmp(argv[2]);
  db_replace(o);
}

static void
batch_alter(int argc, char **argv)
{
  if (!db)
    db_open(argv[0], GDBM_WRITER, NULL);
  db_cmp(argv[1]);
  db_data(argc-2, argv+2);	/* does not read from stdin	*/
  db_replace(data);
}

static void
c_delete(int argc, char **argv)
{
  db_key(argv[1]);
  db_delete(argv[0], argc==2 ? argv[2] : NULL);
}

static void
c_get(int argc, char **argv)
{
  db_open(argv[0], GDBM_READER, NULL);
  db_key(argv[1]);
  db_get();
  if (!data.dptr)
    exit(1);	/* no data, print nothing, common case!	*/
  db_close_if();
  fwrite(data.dptr, data.dsize, 1, stdout);
}

static void
batch_store(int argc, char **argv)
{
  db_data(0, argv+1);	/* does not read from stdin	*/
  if (db_put(argv[0], 0, (argc>1 ? -1 : 0)))
    {
      db_data(0, argv+2);	/* dito	*/
      db_put(argv[0], 1, 0);
    }
  db_close_if();
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

  db_close_if();
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
batcher_new(int argc, char **argv, int term, void (*fn)(int argc, char **argv))
{
  int	c;

  while ((c=read_key_term_c(term))!=EOF)
    fn(argc, argv);
}  

static void
c_nbatch(int argc, char **argv)
{
  batcher_new(argc, argv, '\n', batch_store);
}

static void
c_nbatch0(int argc, char **argv)
{
  batcher_new(argc, argv, 0, batch_store);
}

static void
c_balter(int argc, char **argv)
{
  batcher_new(argc, argv, '\n', batch_alter);
}

static void
c_balter0(int argc, char **argv)
{
  batcher_new(argc, argv, 0, batch_alter);
}

static int
read_key_term_s(const char *term)
{
  int		i, c, l;
  size_t	len;

  db_close_if();
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
      if (c==l && i>=len && (!len || !memcmp(kbuf.dptr+i-len, term, len)))
        {
	  i	-= len;
	  break;
        }
      kbuf_set(i, c);
    }
#if 0
  kbuf_set(i, 0);	/* just in case	*/
#endif
  key		= kbuf;
  key.dsize	= i;
  DP(("term_s: %.*s", i, kbuf));
  return c;
}

static int
read_data_term_s(const char *term)
{
  datum	tmp1, tmp2;
  int	c;

  if (data_alloc)	/* shall never happen	*/
    data_free();

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
      db_delete(argv[0], argc==2 ? argv[2] : NULL);
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
      db_put(argv[0], flag, check);
      if (c==EOF)
        break;
    }
}

static void
c_bins(int argc, char **argv)
{
  batch_read(argc, argv, 0, 0);
}

static void
c_brep(int argc, char **argv)
{
  batch_read(argc, argv, 1, 0);
}

static void
c_bupd(int argc, char **argv)
{
  batch_read(argc, argv, 1, 1);
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
    ex(0, "wrong value: %s", argv[1]);

  db_open(argv[0], GDBM_READER, NULL);
  if (!db_first())
    ex(1, "empty database");
  i	= 0;
  for (j=2; j<=argc; j++)
    tot[j]	= strlen(argv[j]);
  do
    {
      int	ok;

      db_get();
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
    } while (db_next());
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

static void
dobget(char **argv, char *sep)
{
  static int seplen;	/* AARGH, ugly hack	*/

  if (!db)
    db_open(argv[0], GDBM_READER, NULL);
  db_get();
  db_close_if();
  if (!data.dptr)
    ex(1, "key does not exist: %.*s", (int)key.dsize, key.dptr);
  if (sep)
    {
      fwrite(key.dptr, key.dsize, 1, stdout);
      if (!seplen)
	seplen	= *sep ? strlen(sep) : 1;
      fwrite(sep, seplen, 1, stdout);
    }
  fwrite(data.dptr, data.dsize, 1, stdout);
  putchar('\n');
}

static void
c_bget(int argc, char **argv)
{
  int	c;
  char	*term, *sep;

  term	= (argc>0 ? argv[1] : "");
  sep	= (argc>1 ? argv[2] : 0);
  while ((c=read_key_term_s(term))!=EOF || key.dsize)
    {
      dobget(argv, sep);
      if (c==EOF)
	break;
    }
}

static void
c_bget0(int argc, char **argv)
{
  int	c;
  char	*sep;

  sep	= (argc>0 ? argv[1] : 0);
  while ((c=read_key_term_c(0))!=EOF)
    dobget(argv, sep);
}

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
      DP(("key %.*s", (int)key.dsize, key.dptr));
      db_get();
      DP(("data %*s", (int)data.dsize, data.dptr));
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
      db_close_if();
      fwrite(key.dptr, key.dsize, 1, stdout);
      fputs(term, stdout);
    }
}

static void
xmlescape(const unsigned char *ptr, size_t len, const char *format)
{
  unsigned char	c;
  int		i;

  if (!format)
    format	= "&#%d;";
  for (i=len; --i>=0; )
    if ((c= *ptr++)<' ' || c==127	/* not needed but nice to have	*/
	|| c=='&' || c=='<'		/* escapes for element contents	*/
	|| c=='>'		/* must be escaped for simple parsing	*/
	|| c=='"' || c=='\''		/* escapes for attributes	*/
	)
      printf(format, c);
    else
      putchar(c);
}

static void
xmldump(const unsigned char *ptr, size_t len, const char *ele)
{
  printf("  <%s>", ele);
  xmlescape(ptr, len, NULL);
  printf("</%s>\n", ele);
}

static void
c_export(int argc, char **argv)
{
  time_t	tim;
  struct tm	*tm;
  int		row;

  db_open(argv[0], GDBM_READER, NULL);
  /* We use ISO-8859-1 as the default
   */
  printf("<?xml version=\"1.0\" encoding=\"%s\" standalone=\"yes\" ?>\n", argc>0 && argv[1][0] ? argv[1] : "ISO-8859-1");
  printf("<dbm name=\"");
  xmlescape(argv[0], strlen(argv[0]), NULL);
  time(&tim);
  tm	= gmtime(&tim);
  printf("\" utcdate=\"%04d-%02d-%02d %02d:%02d:%02d\">\n",
	 tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
	 tm->tm_hour, tm->tm_min, tm->tm_sec);
  row	= 0;
  if (db_first())
    do
      {
	printf(" <row n=\"%d\">\n", ++row);
	xmldump(key.dptr,  key.dsize, "key");
	db_get();
	xmldump(data.dptr, data.dsize, "data");
	printf(" </row>\n");
      } while (db_next());
  printf("</dbm>\n");
  printf("<!-- %d rows -->\n", row);
}

static int
xmlcmp(const char *s)
{
  size_t len;

  DP(("1 %s", s));
  len	= strlen(s);
  return (key.dsize<len || (len && memcmp(key.dptr, s, len)));
}

static void
xmlget(void)
{
  if (read_key_term_s("<")==EOF && !key.dsize)
    ex(0, "unexpected end of file");
}

static int
xmlskip(const char *s)
{
  xmlget();
  return xmlcmp(s);
}

/* Yes, bad design, it is as it is:
 */
static int
xmlcmp2(const char *s)
{
  size_t len;

  DP(("2 %s", s));
  len	= strlen(s);
  return (data.dsize<len || (len && memcmp(data.dptr, s, len)));
}

static void
xmlget2(void)
{
  if (read_data_term_s("<")==EOF && !data.dsize)
    ex(0, "unexpected end of file");
}

static int
xmlskip2(const char *s)
{
  xmlget2();
  return xmlcmp2(s);
}

static void
xmlunescape(datum *d, int off, int row)
{
  int	n;

  n	= 0;
  while (off<d->dsize)
    {
      unsigned char	c;

      if ((c=d->dptr[off++])=='&')
	{
	  int	i;

	  /* decode an entity &#nnn; or &#xHH;
	   */
	  i	= 0;
	  for (;;)
	    {
	      if (++i<6 && off+i<d->dsize)
		{
		  if (d->dptr[off+i]!=';')
		    continue;
		  if (d->dptr[off++]=='#')
		    {
		      long	tmp;
		      char	*end;

		      /* we either have nnn; or xHH;
		       */
		      tmp	= (d->dptr[off]=='x'
				   ? strtoul(d->dptr+off+1, &end, 16)
				   : strtoul(d->dptr+off, &end, 10)
				   );
		      off	+= i;
		      c		= tmp;
		      if (tmp==(long)c && end && *end==';')
			break;
		    }
		}
	      ex(0, "wrong entity encoding in row %d", row);
	    }
	}
      d->dptr[n++]	= c;
    }
  d->dsize	= n;
}

/* This is supposed to be able to read what export wrote,
 * it is not supposed to be a correct XML parser!
 *
 * In fact the file is broken up into fields terminated by "<".
 * And it will not parse any XML entries nor entities!
 */
static void
c_import(int argc, char **argv)
{
  int	row;

  if (argc==1 && !freopen(argv[1], "rb", stdin))
    ex(0, "cannot open %s", argv[1]);
  if (xmlskip("") || xmlskip("?xml ") || xmlskip("dbm "))
    ex(0, "parse error in header");
  row	= 0;
  while (!xmlskip("row ") || !xmlcmp("row>"))
    {
      row++;
      if (xmlskip("key>"))
        ex(0, "key-tag expected in row %d", row);
      if (xmlskip2("/key>"))
        ex(0, "key-tag not closed in row %d", row);
      if (xmlskip2("data>"))
        ex(0, "data-tag expected in row %d", row);
      xmlunescape(&key,  4, row);
      xmlunescape(&data, 5, row);
      db_put(argv[0],0,0);
      /* We do not need to adjust the key/data pointers as they came
       * from the global read-buffers.
       */
      db_close_if();
      if (xmlskip("/data>"))
        ex(0, "data-tag not properly closed in row %d", row);
      if (xmlskip("/row>"))
        ex(0, "row-tag not properly closed in row %d", row);
    }
  DP(("import end"));
  if (xmlcmp("/dbm>"))
    ex(0, "missing row-tag/dbm-tag not properly closed in row %d", row);
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
    { "list",	c_list,		0, 2	},
    { "dump",	c_dump,		0, 1	},
    { "export",	c_export,	0, 1	},
    { "import",	c_import,	0, 1	},
    { "insert",	c_insert,	1, 2	},
    { "replace",c_replace,	1, 2	},
    { "update",	c_update,	1, 2	},
    { "alter",	c_alter,	2, 3	},
    { "balter",	c_balter,	2, 2	},
    { "balter0", c_balter0,	2, 2	},
    { "delete",	c_delete,	1, 2	},
    { "get",	c_get,		1, 1	},
    { "bget",	c_bget,		0, 2	},
    { "bget0",	c_bget0,	0, 1	},
    { "batch",	c_batch,	1, 2	},
    { "batch0",	c_batch0,	1, 2	},
    { "nbatch",	c_nbatch,	1, 2	},
    { "nbatch0", c_nbatch0,	1, 2	},
    { "bdel",	c_bdel,		0, 2	},
    { "bins",	c_bins,		0, 2	},
    { "brep",	c_brep,		0, 2	},
    { "bupd",	c_bupd,		0, 2	},
    { "filter",	c_filter,	0, 2	},
    { "find",	c_find,		2, -1	},
    { "search",	c_search,	2, -1	},
    { "nfind",	c_nfind,	2, -1	},
    { "nsearch",c_nsearch,	2, -1	},
  };

static int run(int argc, char **argv);

int
main(int argc, char **argv)
{
  char	*arg0;

  arg0	= argv[0];

  if (argc>1 && argv[1][0]=='-')	/* actually a hack	*/
    switch (argv[1][1])
      {
      case 'a':
	advancedsleep=1;		/* set automatic timeout	*/
      case 'q':
	ignoresleep=1;			/* set quiet timeout	*/
      case 't':
	timeout=atoi(*++argv+2);	/* set timeout	*/
	argc--;
	break;
      }
  if (argc<2)
    {
      printf("Usage: %s [-tSEC|-qSEC] action gdbm-file [args...]\n"
	     "\tVersion %s compiled %s\n"
	     "\treturn 0=ok 1=missing_key 2=no_store 10=other 255=locked/timeout\n"
	     "\n"
	     "\t-aSEC	advanced timeout, like -q, but keep db closed if possible\n"
	     "\t-qSEC	quiet timeout, like -t\n"
	     "\t-tSEC	timeout in seconds, -1=unlimited, 0=none (default)\n"
	     "\n"
	     "\tAction	Args	Description:\n"
	     "\t-------	-------	------------\n"
	     "\tcreate	-	create new DBM file, fails if exists\n"
	     "\tkill	-	erase DBM file if empty\n"
	     "\treorg	-	reorganize DBM (fails under CygWin?!?)\n"
	     "\n"
	     "\tlist	[n [p]]	write n keys to stdout, default 1, 0=all\n"
	     "\t		p is the line terminator, default LF, '' for NUL\n"
	     "\tdump	[n]	diagnostic dump, default 0=all\n"
	     "\texport	[c]	dump database to some XML like output\n"
	     "\t		c is the character encoding, default ISO-8859-1\n"
	     "\timport	[file]	read database from what export wrote\n"
	     "\t		Please note that DBM does not parse encodings!\n"
	     "\n"
	     "\tinsert	key [d]	insert d(ata) under key, must not exist\n"
	     "\t		If d is missing, data is read from stdin\n"
	     "\tupdate	key [d]	update existing key.\n"
	     "\treplace	key [d]	as before, but replace (insert+update)\n"
	     "\tdelete	key [d]	delete key entry, data must match if given\n"
	     "\tupdate	key [d]	as before, but key must exist.\n"
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
	     "\tbalter	o d	like batch, but alter old (o) values\n"
	     "\tbalter0	o d	like balter, but line terminator 0\n"
	     "\n"
	     "\tbget	[s [p]]	batch get, read keys from stdin and output data.\n"
	     "\t		s is the key terminator, default ''=blanks.\n"
	     "\t		if p is given, print 'key p data' instead of data.\n"
	     "\tbget0	[p]	like bget where s is NUL.\n"
	     "\tbins	[s [t]]	insert key/data read from stdin\n"
	     "\t		t is the data terminator, default ''=LF.\n"
	     "\tbrep	[s [t]]	as before, but use replace\n"
	     "\tbupd	[s [t]]	as before, but use update\n"
	     "\tbdel	[s [d]]	delete keys read from stdin.\n"
	     "\t		if data(d) is given it must match.\n"
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
	     "\t		note: to search in keys use list | grep\n"
	     "\n"
	     "\tpattern help:   ?, *, [^xyz] or [a-z] are supported.  Hints:\n"
	     "\t		[*], [?], [[] matches literal *, ?, [ respectively\n"
	     "\t		[[-[-] matches [ or -, [z-a] matches b to y\n"
	     , arg0, DBM_VERSION, __DATE__);
      return 1;
    }
  return run(argc, argv);
}

static int
run(int argc, char **argv)
{
  int	i;

  for (i=sizeof actions/sizeof *actions; --i>=0; )
    if (!strcmp(actions[i].command, argv[1]))
      {
	argc	-= 3;
	argv	+= 2;
	if (argc<0)
	  ex(0, "missing database name for command '%s'", actions[i].command);
	if (argc<actions[i].minargs ||
	    (actions[i].maxargs!=-1 && argc>actions[i].maxargs))
	  ex(0, "wrong number of parameters for command '%s': %d (%d-%d)",
	     actions[i].command, argc, actions[i].minargs, actions[i].maxargs
	     );
	actions[i].fn(argc, argv);
	db_close();
	return 0;
      }
  ex(0, "unknown command: '%s'", argv[1]);
  return 0;
}
