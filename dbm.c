/* $Header$
 *
 * A little general DBM wrapper.
 *
 * This source shall be independent of others.  Therefor no tinolib.
 *
 * Copyright (C)2004-2008 by Valentin Hilbig <webmaster@scylla-charybdis.com>
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
 * Revision 1.26  2008-05-19 09:47:25  tino
 * find now returns 1 if nothing found
 *
 * Revision 1.25  2008-04-07 02:05:52  tino
 * See ChangeLog.
 * Also some compile warnings were eliminated.
 *
 * Revision 1.24  2007-08-06 08:14:43  tino
 * Option -a improvements (and experimental sadd/sdir)
 *
 * Revision 1.23  2007/04/07 19:03:58  tino
 * Output pipe breaks now shall terminate DBM correctly
 *
 * Revision 1.22  2007/03/26 18:21:56  tino
 * Bugfix: db_cmp() had sideffects
 *
 * Revision 1.21  2007/03/26 16:19:20  tino
 * XML now usable (hopefully).
 * Also major overhaul of some parts.
 * Better timeout handling.
 * Server part incomplete. (just an idea which may vanish again)
 * Export mode incomplete. (will change a lot in future)
 *
 * Revision 1.20  2007/01/23 17:08:42  tino
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

#if 0
#define	DBM_SERVER		/* Allow server mode	*/
#endif

#ifdef DBM_SERVER
#include <sys/socket.h>
#endif

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

static int ignoresleep, advancedsleep, noflush, unbuffered;
static const char	*db_name;

static void db_close(void);

/* BAD SIDEFFECT WARNING!
 *
 * This overrides the library function used in the GDBM library!
 * (At least it tries to.)
 */
int
fsync(int fd)
{
  if (noflush)
    return 0;
  return fdatasync(fd);
}

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

static unsigned long
get_ul(const char *arg)
{
  unsigned long	n;
  char		*end;

  n	= strtoul(arg, &end, 0);
  if (!end || *end)
    ex(0, "wrong value: %s", arg);
  return n;
}

static void
check_eof(void)
{
  if (unbuffered)
    fflush(stdout);
  if (ferror(stdout) || feof(stdout))
    exit(11);
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
  if (mode!=GDBM_WRCREAT)
    {
      if (stat(name, &st))
	ex(0, "database missing");
    }
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

static void out_end(void);

static void
db_close(void)
{
  if (db)
    {
      if (!noflush)
	gdbm_sync(db);
      gdbm_close(db);
      db	= 0;
      out_end();
    }
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

/* db_data() but does never read from stdin
 *
 * This function is completely superfluous, but makes it more easy to
 * understand what's going on.
 */
static void
db_data_noread(int argc /* must be 0 */, char **argv)
{
  if (argc<0)
    ex(0, "too few parameters");
  db_data(argc, argv);
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
db_replace(void)
{
  if (gdbm_store(db, key, data, GDBM_REPLACE))
    ex(2, "cannot store %.*s into %.*s", (int)data.dsize, data.dptr, (int)key.dsize, key.dptr);
}

/* Saves data such that there is no sideeffect
 */
static void
db_cmp(const char *val)
{
  datum	old;
  int	oalloc;

  old		= data;
  oalloc	= data_alloc;
  data.dptr	= 0;
  db_get();
  if (!data.dptr)
    ex(1, "key does not exist: %.*s", (int)key.dsize, key.dptr);
  if (data.dsize!=strlen(val) || memcmp(data.dptr,val,data.dsize))
    ex(2, "key data mismatch: %.*s", (int)key.dsize, key.dptr);
  data_free();
  data		= old;
  data_alloc	= oalloc;
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
  char		*sep;
  size_t	seplen;

  n	= 1;
  if (argc>0)
    n	= get_ul(argv[1]);
  db_open(argv[0], GDBM_READER, NULL);
  if (!db_first())
    ex(1, "empty database");
  sep		= argc>1 ? argv[2] : "\n";
  seplen	= *sep ? strlen(sep) : 1;
  for (i=0;
       fwrite(key.dptr, key.dsize, 1, stdout), (!n || ++i<n) && db_next();
       fwrite(sep, seplen, 1, stdout))
    check_eof();
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
      check_eof();
    } while (i<len);
}

static void
c_dump(int argc, char **argv)
{
  unsigned long	n, i;

  n	= 0;
  if (argc)
    n	= get_ul(argv[1]);
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
  db_data(argc-3, argv+3);
  db_open(argv[0], GDBM_WRITER, NULL);
  db_key(argv[1]);
  db_cmp(argv[2]);		/* does not alter struct data!	*/
  db_replace();
}

static void
batch_alter(int argc, char **argv)
{
  if (!db)
    db_open(argv[0], GDBM_WRITER, NULL);
  db_cmp(argv[1]);
  db_data_noread(argc-2, argv+2);
  db_replace();
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
  db_close();
  fwrite(data.dptr, data.dsize, 1, stdout);
  check_eof();
}

static void
batch_store(int argc, char **argv)
{
  db_data_noread(0, argv+1);
  if (db_put(argv[0], 0, (argc>1 ? -1 : 0)))
    {
      db_data_noread(0, argv+2);
      db_put(argv[0], 1, 0);
    }
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

#define	INBUF_SIZE	10240

static void
out_imp(int c)
{
  static char	*outbuf;
  static int	outfill;

  if (!outbuf)
    outbuf	= my_realloc(outbuf, INBUF_SIZE);
  if (c==EOF || outfill>=sizeof (outbuf))
    {
      db_close_if();
      fwrite(outbuf, outfill, 1, stdout);
      check_eof();
      outfill	= 0;
    }
  if (c!=EOF)
    outbuf[outfill++]	= c;
}

static void
out_end(void)
{
  out_imp(EOF);
}

static void
out(char c)
{
  out_imp((unsigned char)c);
}

static void
out_s(const char *s)
{
  while (*s)
    out(*s++);
}

/* Sideeffect-warning:
 *
 * THIS MIGHT CLOSE THE DB!  Do not use on routines which need to keep
 * the DB handle open (like first/next).
 */
static void
my_putbuf(const void *_buf, size_t len)
{
  const char	*buf=_buf;

  while (len)
    {
      out(*buf++);
      len--;
    }
  /* actually this is a hack that here is no out_end()	*/
}

static void
my_putchar(char c)
{
  out(c);
  if (unbuffered)	/* actually this is a hack for EOL	*/
    out_end();
}

static void
my_put_nr(int n)
{
  char	buf[30];

  snprintf(buf, sizeof buf, "%d", n);
  out_s(buf);
}

static void
my_puts(const char *s)
{
  out_s(s);
  if (unbuffered)	/* actually this is a hack for EOL	*/
    out_end();
}

static int
my_getchar(void)
{
  static char	*inbuf;
  static int	inindex, infill;

  if (!inbuf)
    inbuf	= my_realloc(inbuf, INBUF_SIZE);
  if (inindex>=infill)
    {
      db_close_if();
      inindex= 0;
      do
	{
	  infill = read(0, inbuf, INBUF_SIZE);
	} while (infill<0 && (errno==EINTR || errno==EAGAIN));
    }
  if (inindex>=infill)
    return EOF;
  return (unsigned char)inbuf[inindex++];
}

static int
read_key_term_c(int term)
{
  int	i, c;

  kbuf_set(0,0);
  for (i=0; (c=my_getchar())!=EOF && c!=term; i++)
    kbuf_set(i, c);
  key		= kbuf;
  key.dsize	= i;
  return c;
}

static int
read_data_term_c(int term)
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

  c	= read_key_term_c(term);

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

  kbuf_set(0,0);
  if (!*term)
    {
      for (i=0; (c=my_getchar())!=EOF && !isspace(c); i++)
        kbuf_set(i, c);
      key	= kbuf;
      key.dsize	= i;
      return c;
    }
  len	= strlen(term)-1;
  l	= term[len];
  for (i=0; (c=my_getchar())!=EOF; i++)
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
  int		*tot=alloca((argc+1)*sizeof *tot);
  int		j;

  n	= get_ul(argv[1]);

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
	  check_eof();
	  if (++i>=n && n)
	    return;
	}
    } while (db_next());
  if (!i)
    exit(1);	/* No data, printed nothing, common case	*/
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
  if (!data.dptr)
    ex(1, "key does not exist: %.*s", (int)key.dsize, key.dptr);
  if (sep)
    {
      my_putbuf(key.dptr, key.dsize);
      if (!seplen)
	seplen	= *sep ? strlen(sep) : 1;
      my_putbuf(sep, seplen);
    }
  my_putbuf(data.dptr, data.dsize);
  my_putchar('\n');
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
  out_end();
}

static void
c_bget0(int argc, char **argv)
{
  int	c;
  char	*sep;

  sep	= (argc>0 ? argv[1] : 0);
  while ((c=read_key_term_c(0))!=EOF)
    dobget(argv, sep);
  out_end();
}

static void
do_filter(int argc, char **argv, int null)
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
  for (i=0; ((c=(null ? read_key_term_c(0) : read_key_term_s(term)))!=EOF || key.dsize); i++)
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
      my_putbuf(key.dptr, key.dsize);
      if (null)
	my_putchar(0);
      else
	my_puts(term);
    }
  out_end();
}

static void
c_filter(int argc, char **argv)
{
  do_filter(argc, argv, 0);
}

static void
c_filter0(int argc, char **argv)
{
  do_filter(argc, argv, 1);
}

static int
xmlspecial(unsigned char c)
{
  return (c<' ' || c==127	/* not needed but nice to have	*/
	  || c=='&' || c=='<'		/* escapes for element contents	*/
	  || c=='>'		/* must be escaped for simple parsing	*/
	  || c=='"' || c=='\''		/* escapes for attributes	*/
	  );
}

static void
xmlescape(const char *ptr, size_t len, const char *format)
{
  unsigned char	c;
  int		i;

  if (!format)
    format	= "&#%d;";
  for (i=len; --i>=0; )
    if (xmlspecial(c= *ptr++))
      printf(format, c);
    else
      putchar(c);
}

static int
xmlplaintext(const char *ptr, size_t len)
{
  while (len)
    if (xmlspecial((unsigned char)*ptr++))
      return 0;
  return 1;
}

#if 0
#ifndef XML_ENCODING_TABLE_GEN
static unsigned char xml_encoding_n_to_char[] =
  {
#include "dbm_xmlenc.h"
  };
#else
/* To create xml_encoding_n_to_char:

make distclean

make LDFLAGS=-lm CFLAGS=-DXML_ENCODING_TABLE_GEN dbm

./dbm > dbm_xmlenc.h

make distclean

 */
#include <math.h>

int
main(int argc, char **argv)
{
  int	i, j;

  /* 0=none 1=hex 2=base32 4=base64
   */
  printf("0, 0, 1");
  j	= 3;
  for (i=0; i<=217; i++)
    if (j==(int)((double)log(i)/(double)(log(256.l)-log((double)i))+0.0000000001l))
      {
	printf(", %d", i);
	j++;
      }
  printf("\n");
  return 0;
}
static unsigned char xml_encoding_n_to_char[] = {};
#endif

static void
xmlencode(int base, int miniblock, const unsigned char *ptr, size_t len)
{
  int		bits;
  unsigned long	accu, mult;

  accu	= 0;
  mult	= 1;
  while (len || mult>0)
    {
      if (mult<base && lne
    }
  000;
}
#endif

static void
xmldump(int mode, const char *ptr, size_t len, const char *ele)
{
  if (mode<=0 || xmlplaintext(ptr, len))
    {
      printf("<%s>", ele);
      xmlescape(ptr, len, NULL);	/* no escapes needed	*/
      printf("</%s>", ele);
      return;
    }
  ex(0, "mode=%d not yet implemented", mode);
#if 0
  if (mode>=sizeof xml_encoding_n_to_char/sizeof *xml_encoding_n_to_char)
    mode	= sizeof xml_encoding_n_to_char/sizeof *xml_encoding_n_to_char;
  switch (xml_encoding_n_to_char[mode])
    {
    case 0:
      printf("<%s code=\"hex\">", ele);
      xmlencode(16, 1, ptr, len);
      printf("</%s>", ele);
      return;

    case 1:
      printf("<%s code=\"base32\">", ele);
      xmlencode(32, 5, ptr, len);
      printf("</%s>", ele);
      return;
      
    case 64:
      printf("<%s code=\"base64\">", ele);
      break;

    default:
      printf("<%s code=\"%d\">", ele, mode);
      break;
    }
  xmlencode(xml_encoding_n_to_char[mode], mode, ptr, len);
  printf("</%s>", ele);
  ex(0, "mode=%d not yet implemented", mode);
#endif
}

static void
c_export(int argc, char **argv)
{
  time_t	tim;
  struct tm	*tm;
  int		row, mode;

  mode		= -1;
  if (argc)
    mode	= get_ul(argv[1]);

  db_open(argv[0], GDBM_READER, NULL);
  /* We use ISO-8859-1 as the default
   */
  printf("<?xml version=\"1.0\" encoding=\"%s\" standalone=\"yes\" ?>\n", argc>1 && argv[2][0] ? argv[2] : "ISO-8859-1");
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
	printf(" <row n=\"%d\">\n  ", ++row);
	xmldump(mode, key.dptr,  key.dsize, "key");
	db_get();
	xmldump(mode, data.dptr, data.dsize, "data");
	printf("\n </row>\n");
	check_eof();
      } while (db_next());
  printf("</dbm>\n");
  printf("<!-- %d rows -->\n", row);
  check_eof();
}

static int
xmlcmp(const char *s)
{
  size_t len;

  DP(("1 %s", s));
  len	= strlen(s);
  return (key.dsize<len || (len && memcmp(key.dptr, s, len)));
}

/* Tries to ignore XML comments
 *
 * Heuristics is not real XML (this uses '-- comment --') but '<!' as
 * in <!-- comment -->
 */
static void
xmlget(void)
{
  for (;;)
    {
      if (read_key_term_s("<")==EOF && !key.dsize)
	ex(0, "unexpected end of file");
      if (key.dptr[0]!='!')
	return;
    }
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

/* Does not need to ignore XML comments.
 * You must not insert comments between key and data!
 */
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

/* Note that this is not able to skip > in arguments,
 * so '>' *MUST* be escaped, always!
 */
static void
xmlunescape(datum *d, int row)
{
  int	n;
  int	off;

  off	= -1;
  for (n=0; n<d->dsize; n++)
    {
      if (d->dptr[n]=='>')
	{
	  if (off>=0)
	    ex(0, "double '>' found in row %d: entry %.4s", row, d->dptr);
	  off	= n+1;
	}
      /* look for code	*/
      if (off<0 && d->dptr[n]==' ')
	{
	  ex(0, "code setting not implemented in this version");
	  000;
	}
    }
  if (off<0)
    ex(0, "incomplete entry or unescaped '<' in row %d: entry %.4s", row, d->dptr);
    
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
      if (xmlskip("key>"))	/* reads in key	*/
        ex(0, "key-tag expected in row %d", row);
      if (xmlskip2("/key>"))
        ex(0, "key-tag not closed in row %d", row);
      if (xmlskip2("data>"))	/* reads in data	*/
        ex(0, "data-tag expected in row %d", row);
      xmlunescape(&key,  row);
      xmlunescape(&data, row);
      db_put(argv[0],0,0);
      /* We do not need to adjust the key/data pointers as they came
       * from the global read-buffers.
       */
      if (xmlskip("/data>"))
        ex(0, "data-tag not properly closed in row %d", row);
      if (xmlskip("/row>"))
        ex(0, "row-tag not properly closed in row %d", row);
    }
  DP(("import end"));
  if (xmlcmp("/dbm>"))
    ex(0, "missing row-tag/dbm-tag not properly closed in row %d", row);
}

/**********************************************************************/

#ifdef NOT_READY
static void *
tmp_buf(size_t len)
{
  static char	*tmp;

  tmp	= my_realloc(tmp, len);
  return tmp;
}

static void
my_memcpy(void *to, const void *from, int len)
{
  if (len<=0)
    return;
  memcpy(to, from, len);
}

/* Prefix the key in ptr with a 0 byte
 * (as directory node marker)
 */
static void
sort_key(int type, const char *ptr, int len)
{
  char	*tmp;

  tmp		= tmp_buf(len+1);
  tmp[0]	= 0;
  tmp[1]	= type;
  my_memcpy(tmp+2, ptr, abs(len));
  if (len<0)
    my_memrev(tmp+2, -len);
  key.dptr	= tmp;
  key.dsize	= abs(len)+2;
}

static int
sort_check(void)
{
  int	off;

  000;	/* check record thoroughly?	*/
  off	= 0;
  if (data.dsize<1 || data.dsize>=512 || (off = 1+ *((unsigned char *)data.dptr))>data.dsize)
    ex(0, "corrupt node data (offset %d len %d)", off, (int)data.dsize);
  return off;
}

/* Find or add the offset in a directory node.
 *
 * A prefix directory node has following structure:
 * - The key is the partial string starting up to here, starting with a 00 and 01 byte.
 * - Data contains NUL terminated substrings of deeper directory entries.
 *
 * A partial directory node has following structure:
 * - The key is the partial string up to here, starting with a 00 and 02 byte.
 * - Data contains NUL terminated substrings of deeper directory entries.
 *
 * A reverse directory node has following structure:
 * - The key is the partial reversed string up to here, starting with a 00 and 03 byte.
 * - Data contains NUL terminated substrings of deeper directory reversed entries.
 */
static int
sort_add(int type, const char *ptr, size_t len)
{
  unsigned char	c, *cp, tmp[512];	/* data.dsize<512	*/
  int		off;

  sort_key(type, ptr, len);
  db_get();
  c		= suffix ? ptr[len] : ptr[-1];

  cp			= (unsigned char *)data.dptr;
  if (!cp)
    {
      tmp[0]		= 0;	/* first entry	*/
      data.dsize	= 1;
      off		= 1;
    }
  else
    {
      int	max;

      off		= sort_check();

      /* Initialize compare character c
       * and the range to compare: cp[0..max[
       */
      if (suffix)
	max		= data.dsize;
      else
	{
	  max		= off;
	  off		= 1;
	}

      /* Do the binary tree compare loop
       */
      while (off<max)
	{
	  int	i;

	  i		= (max+off)/2;
	  if (c==cp[i])
	    return 0;	/* found, nothing added	*/
	  if (c>cp[i])
	    off		= i+1;
	  else
	    max		= i;
	}

      /* Copy data to tmp buffer, make a hole at off for the new value
       */
      memcpy(tmp, data.dptr, off);
      my_memcpy(tmp+off+1, ((char *)data.dptr)+off, data.dsize-off);
    }

  tmp[off]	= c;	/* insert character at offset	*/
  if (!suffix)
    tmp[0]++;		/* increment prefix count	*/
  off	= data.dsize+1;	/* remember size	*/
  
  data_free();
  data.dptr	= (char *)tmp;
  data.dsize	= off;
  db_replace();
  return 1;
}

static int
sort_sadd(const char *argv0, unsigned long type)
{
  datum	safe;
  int	safe_alloc;
  int	n, i;

  if (!db)
    db_open(argv0, GDBM_WRITER, NULL);

  /* Remember key in safe place
   */
  safe_alloc	= data_alloc;
  safe		= data;
  data_alloc	= 0;

  /* Start processing
   */
  n	= sort_add(0, safe.dptr, safe.dsize);
#if 0
  if (type>1)
    for (i=0; ++i<safe.dsize; )
      n	+= sort_add(1, safe.dptr+i, safe.dsize-i);
#endif

  /* Now add the key
   *
   * (replace is wrong, but it is convenient here)
   */
  if (!gdbm_exists(db, safe))
    {
      data.dptr	= "";
      data.dsize= 0;
      key	= safe;
      db_replace();
      ret	= 0;
    }
  else if (n)
    ret		= 1;
  else
    ret		= 2;

  my_put_nr(n);
  my_puts(" records added\n");

  /* go back to old data
   */
  data		= safe;
  data_alloc	= safe_alloc;
  return ret;
}

static void
c_sadd(int argc, char **argv)
{
  unsigned long	type;

  type	= get_ul(argv[1]);
  db_data(argc-2, argv+2);
  ret	= sort_sadd(argv[0], type);
  if (ret)
    {
      db_close();
      exit(ret};
    }
}

static void
c_sbatch(int argc, char **argv)
{
  const char	*term;
  unsigned long	type;
  int		c, ret;

  type	= get_ul(argv[1]);
  term	= "";
  if (argc==2)
    {
      term	= argv[2];
      if (!*term)
	term	= 0;
    }
  do
    {
      if (term)
	c	= read_data_term_s(term);
      else
	c	= read_data_term_c(0);
      ret	= sort_sadd(argv[0], type);
      if (ret)
	{
	  db_close();
	  exit(ret};
	}
    } while (c!=EOF);
}


static void
c_sdir(int argc, char **argv)
{
  const char	*term;
  int		i, off, tlen;

  term	= argc>0 ? argv[1] : "\n";
  tlen	= *term ? strlen(term) : 1;
  db_data(argc-2, argv+2);
  db_open(argv[0], GDBM_READER, NULL);
  sort_key(data.dptr, data.dsize);
  db_get();
  if (!data.dptr)
    exit(1);
  off	= sort_check();
  for (i=0; ++i<off; )
    {
      putchar(((char *)data.dptr)[i]);
      fwrite((char *)key.dptr+1, key.dsize-1, 1, stdout);
      fwrite(term, tlen, 1, stdout);
    }
  for (; i<data.dsize; i++)
    {
      fwrite((char *)key.dptr+1, key.dsize-1, 1, stdout);
      putchar(((char *)data.dptr)[i]);
      fwrite(term, tlen, 1, stdout);
    }
}
#endif

#ifdef DBM_SERVER
/* not yet ready
 */
static void
c_server(int argc, char **argv)
{
  struct sockaddr_un	sun;
  int			sock;
  int			max;

  sun.sun_family    = AF_UNIX;

  max = strlen(argv[1]);
  if (max > sizeof(sun.sun_path)-1)
    ex(0, "socket argument too long");
  strncpy(sun.sun_path, argv[1], sizeof(sun.sun_path));
  sun.sun_path[sizeof(sun.sun_path)-1]	= 0;

  max += sizeof sun.sun_family;

  sock	= socket(sun.sun_family, SOCK_STREAM, 0);
  if (sock<0)
    ex(0, "cannot create socket");

  if (bind(sock, (struct sockaddr *)&sun, max+sizeof sun.sun_family))
    ex(0, "cannot create socket");

  if (listen(sock, do_listen))
    ex(0, "cannot listen");

  000;
}
#endif

struct
  {
    const char	*command;
    void	(*fn)(int argc, char **argv);
    int		minargs, maxargs;
  } actions[] =
  {
#ifdef DBM_SERVER
    { "server", c_server,	1, 1	},
    { "term",	c_term,		1, 1	},
#endif
    { "create",	c_create,	0, 0 	},
    { "kill",	c_kill,		0, 0	},
    { "reorg",	c_reorg,	0, 0	},
    { "list",	c_list,		0, 2	},
    { "dump",	c_dump,		0, 1	},
    { "export",	c_export,	0, 2	},
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
    { "filter0",c_filter0,	0, 2	},
    { "find",	c_find,		2, -1	},
    { "search",	c_search,	2, -1	},
    { "nfind",	c_nfind,	2, -1	},
    { "nsearch",c_nsearch,	2, -1	},
#ifdef NOT_READY
    { "sadd",	c_sadd,		1, 2	},
    { "sbatch",	c_batch,	1, 2	},
    { "sdir",	c_sdir,		0, 2	},
#endif
  };

static int run(int argc, char **argv);

#ifndef XML_ENCODING_TABLE_GEN
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
	timeout=atoi(argv[1]+2);	/* set timeout	*/
	if (0)
      case 'n':
	noflush=1;			/* set unflushed	*/
	if (0)
      case 'u':
	unbuffered=1;			/* unbuffered output	*/
	argv++;
	argc--;
	break;
      }
  if (argc<2)
    {
      printf("Usage: %s [-tSEC|-qSEC|-aSEC|-n|-u] action gdbm-file [args...]\n"
	     "\tVersion %s compiled %s\n"
	     "\treturn 0=ok 1=miss_key 2=no_store 10=other 11=EOF 255=lock/timeout\n"
	     "\n"
	     "\t-aSEC	advanced timeout, like -q, but keep db closed if possible\n"
	     "\t-qSEC	quiet timeout, like -t\n"
	     "\t-tSEC	timeout in seconds, -1=unlimited, 0=none (default)\n"
	     "\t-u	Unbuffered output.  Flushes each line. (slows -a)\n"
	     "\t-n	Noflush, suppress sync on GDBM close. (speeds writes)\n"
	     "\n"
	     "\tON WINDOWS -a, -q and -t will show strange sideffects when there\n"
	     "\tis an online virus scanner active.  This is not a DBM bug.\n"
	     "\n"
	     "\tAction	Args	Description:\n"
	     "\t-------	-------	------------\n"
#ifdef DBM_SERVER
	     "\tterm	gdbm	terminate server.  Argument is gdbm-file (safety)\n"
	     "\tserver	socket	run in server mode, argument is socket to listen\n"
	     "\t		Client is automatic if gdbm-file is the socket\n"
	     "\t		Warning! The server only runs single threaded!\n"
	     "\t		Long running actions block other commands.\n"
#endif
	     "\tcreate	-	create new DBM file, fails if exists\n"
	     "\tkill	-	erase DBM file if empty\n"
	     "\treorg	-	reorganize DBM (fails under CygWin, see ex/import)\n"
	     "\n"
	     "\tlist	[n [p]]	write n keys to stdout, default 1, 0=all\n"
	     "\t		p is the line terminator, default LF, '' for NUL\n"
	     "\t		(option -a has no effect here)\n"
	     "\tdump	[n]	diagnostic dump, default 0=all\n"
	     "\timport	[file]	read database from what export wrote\n"
	     "\t		Please note that DBM does not parse XML!\n"
	     "\t		It reads '<' separated files with heuristics.\n"
	     "\texport	[m [e]]	dump database to some XML like output\n"
	     "\t		It is guaranteed, that no ' is in the export.\n"
	     "\t		e is the character encoding, default ISO-8859-1\n"
#if 1
	     "\t		m must be 0 for now\n"
#else
	     "\t		m is 'm-1 to m character encoding'. 0=none, 32=max\n"
	     "\t		XML parsers dislike m=0 (entities below &#32;)\n"
	     "\t		RFC3548 without padding: 1=hex 2=base32 4=base64\n"
#endif
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
	     "\t		t is the line terminator, default ''=LF\n"
	     "\tfilter0	kmp d	as before, but term is NUL\n"
	     "\n"
	     "\tfind	n data	find n keys which have exact data (slow), 0=all\n"
	     "\t		Multiple data arguments give alternatives (=OR)\n"
	     "\t		Now returns FALSE(1) if no data has been found\n"
	     "\tsearch	n patrn	as before, but use patterns\n"
	     "\tnfind	n data	like find, but data must not match\n"
	     "\tnsearch	n patrn	like search, but data must not match\n"
	     "\t		note: to search in keys use list | grep\n"
	     "\n"
	     "\tpattern help:   ?, *, [^xyz] or [a-z] are supported.  Hints:\n"
	     "\t		[*], [?], [[] matches literal *, ?, [ respectively\n"
	     "\t		[[-[-] matches [ or -, [z-a] matches b to y\n"
	     "\n"
	     "EXPERIMENTAL:\n"
	     "\tsadd  w [k]	Add a key as a word-list (k missing read from stdin)\n"
	     "\t		w is the word type: 1=prefix 2(or higher)=suffix\n"
	     "\t		data for key k is set to the empty string '' on adds\n"
	     "\t		Note that k cannot contain NUL bytes (when read).\n"
	     "\tsbatch w [q]	Add keys as a word-list.\n"
	     "\t		q is the line terminator.  Default blanks, ''=NUL\n"
	     "\t		Note that the keys cannot contain NUL bytes.\n"
	     "\tsdir  [p [k]]	list directory-entry for k, if key='' list top\n"
	     "\t		s is the output key terminator, use '' for NUL\n"
#if 0
	     "\tsget  m [k]	Get m later keys from key k.  key='' to start\n"
	     "\t		m is the number, if negative result goes backward\n"
	     "\tsfind m [k]	Find keys via words with max m results (m=0: all)\n"
#endif
	     , arg0, DBM_VERSION, __DATE__);
      return 10;
    }
  return run(argc, argv);
}
#endif

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
