/*
 * Copyright (c) 2007, Novell Inc.
 *
 * This program is licensed under the BSD license, read LICENSE.BSD
 * for further information
 */

/*
 * repo_rpmdb
 * 
 * convert rpm db to repo
 * 
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <rpm/db.h>

#include "pool.h"
#include "repo.h"
#include "hash.h"
#include "util.h"
#include "repo_rpmdb.h"

#define RPMDB_COOKIE_VERSION 2

#define TAG_NAME		1000
#define TAG_VERSION		1001
#define TAG_RELEASE		1002
#define TAG_EPOCH		1003
#define TAG_SUMMARY		1004
#define TAG_DESCRIPTION		1005
#define TAG_BUILDTIME		1006
#define TAG_BUILDHOST		1007
#define TAG_INSTALLTIME		1008
#define TAG_SIZE                1009
#define TAG_DISTRIBUTION	1010
#define TAG_VENDOR		1011
#define TAG_LICENSE		1014
#define TAG_PACKAGER		1015
#define TAG_GROUP		1016
#define TAG_URL			1020
#define TAG_ARCH		1022
#define TAG_FILESIZES		1028
#define TAG_FILEMODES		1030
#define TAG_SOURCERPM		1044
#define TAG_PROVIDENAME		1047
#define TAG_REQUIREFLAGS	1048
#define TAG_REQUIRENAME		1049
#define TAG_REQUIREVERSION	1050
#define TAG_NOSOURCE		1051
#define TAG_NOPATCH		1052
#define TAG_CONFLICTFLAGS	1053
#define TAG_CONFLICTNAME	1054
#define TAG_CONFLICTVERSION	1055
#define TAG_OBSOLETENAME	1090
#define TAG_FILEDEVICES		1095
#define TAG_FILEINODES		1096
#define TAG_PROVIDEFLAGS	1112
#define TAG_PROVIDEVERSION	1113
#define TAG_OBSOLETEFLAGS	1114
#define TAG_OBSOLETEVERSION	1115
#define TAG_DIRINDEXES		1116
#define TAG_BASENAMES		1117
#define TAG_DIRNAMES		1118
#define TAG_PAYLOADFORMAT	1124
#define TAG_PATCHESNAME         1133
#define TAG_SUGGESTSNAME	1156
#define TAG_SUGGESTSVERSION	1157
#define TAG_SUGGESTSFLAGS	1158
#define TAG_ENHANCESNAME	1159
#define TAG_ENHANCESVERSION	1160
#define TAG_ENHANCESFLAGS	1161

#define DEP_LESS		(1 << 1)
#define DEP_GREATER		(1 << 2)
#define DEP_EQUAL		(1 << 3)
#define DEP_STRONG		(1 << 27)
#define DEP_PRE			((1 << 6) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12))


struct rpmid {
  unsigned int dbid;
  char *name;
};

typedef struct rpmhead {
  int cnt;
  int dcnt;
  unsigned char *dp;
  unsigned char data[1];
} RpmHead;

static int
headexists(RpmHead *h, int tag)
{
  unsigned int i;
  unsigned char *d, taga[4];

  d = h->dp - 16;
  taga[0] = tag >> 24;
  taga[1] = tag >> 16;
  taga[2] = tag >> 8;
  taga[3] = tag;
  for (i = 0; i < h->cnt; i++, d -= 16)
    if (d[3] == taga[3] && d[2] == taga[2] && d[1] == taga[1] && d[0] == taga[0])
      return 1;
  return 0;
}

static unsigned int *
headint32array(RpmHead *h, int tag, int *cnt)
{
  unsigned int i, o, *r;
  unsigned char *d, taga[4];

  d = h->dp - 16;
  taga[0] = tag >> 24;
  taga[1] = tag >> 16;
  taga[2] = tag >> 8;
  taga[3] = tag;
  for (i = 0; i < h->cnt; i++, d -= 16)
    if (d[3] == taga[3] && d[2] == taga[2] && d[1] == taga[1] && d[0] == taga[0])
      break;
  if (i >= h->cnt)
    return 0;
  if (d[4] != 0 || d[5] != 0 || d[6] != 0 || d[7] != 4)
    return 0;
  o = d[8] << 24 | d[9] << 16 | d[10] << 8 | d[11];
  i = d[12] << 24 | d[13] << 16 | d[14] << 8 | d[15];
  if (o + 4 * i > h->dcnt)
    return 0;
  d = h->dp + o;
  r = sat_calloc(i ? i : 1, sizeof(unsigned int));
  if (cnt)
    *cnt = i;
  for (o = 0; o < i; o++, d += 4)
    r[o] = d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3];
  return r;
}

static unsigned int
headint32(RpmHead *h, int tag)
{
  unsigned int i, o;
  unsigned char *d, taga[4];

  d = h->dp - 16; 
  taga[0] = tag >> 24; 
  taga[1] = tag >> 16; 
  taga[2] = tag >> 8;
  taga[3] = tag;
  for (i = 0; i < h->cnt; i++, d -= 16) 
    if (d[3] == taga[3] && d[2] == taga[2] && d[1] == taga[1] && d[0] == taga[0])
      break;
  if (i >= h->cnt)
    return 0;
  if (d[4] != 0 || d[5] != 0 || d[6] != 0 || d[7] != 4)
    return 0;
  o = d[8] << 24 | d[9] << 16 | d[10] << 8 | d[11];
  i = d[12] << 24 | d[13] << 16 | d[14] << 8 | d[15];
  if (i == 0 || o + 4 * i > h->dcnt)
    return 0;
  d = h->dp + o;
  return d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3];
}

static unsigned int *
headint16array(RpmHead *h, int tag, int *cnt)
{
  unsigned int i, o, *r;
  unsigned char *d, taga[4];

  d = h->dp - 16;
  taga[0] = tag >> 24;
  taga[1] = tag >> 16;
  taga[2] = tag >> 8;
  taga[3] = tag;
  for (i = 0; i < h->cnt; i++, d -= 16)
    if (d[3] == taga[3] && d[2] == taga[2] && d[1] == taga[1] && d[0] == taga[0])
      break;
  if (i >= h->cnt)
    return 0;
  if (d[4] != 0 || d[5] != 0 || d[6] != 0 || d[7] != 3)
    return 0;
  o = d[8] << 24 | d[9] << 16 | d[10] << 8 | d[11];
  i = d[12] << 24 | d[13] << 16 | d[14] << 8 | d[15];
  if (o + 4 * i > h->dcnt)
    return 0;
  d = h->dp + o;
  r = sat_calloc(i ? i : 1, sizeof(unsigned int));
  if (cnt)
    *cnt = i;
  for (o = 0; o < i; o++, d += 2)
    r[o] = d[0] << 8 | d[1];
  return r;
}

static char *
headstring(RpmHead *h, int tag)
{
  unsigned int i, o;
  unsigned char *d, taga[4];
  d = h->dp - 16;
  taga[0] = tag >> 24;
  taga[1] = tag >> 16;
  taga[2] = tag >> 8;
  taga[3] = tag;
  for (i = 0; i < h->cnt; i++, d -= 16)
    if (d[3] == taga[3] && d[2] == taga[2] && d[1] == taga[1] && d[0] == taga[0])
      break;
  if (i >= h->cnt)
    return 0;
  /* 6: STRING, 9: I18NSTRING */
  if (d[4] != 0 || d[5] != 0 || d[6] != 0 || (d[7] != 6 && d[7] != 9))
    return 0;
  o = d[8] << 24 | d[9] << 16 | d[10] << 8 | d[11];
  return (char *)h->dp + o;
}

static char **
headstringarray(RpmHead *h, int tag, int *cnt)
{
  unsigned int i, o;
  unsigned char *d, taga[4];
  char **r;

  d = h->dp - 16;
  taga[0] = tag >> 24;
  taga[1] = tag >> 16;
  taga[2] = tag >> 8;
  taga[3] = tag;
  for (i = 0; i < h->cnt; i++, d -= 16)
    if (d[3] == taga[3] && d[2] == taga[2] && d[1] == taga[1] && d[0] == taga[0])
      break;
  if (i >= h->cnt)
    return 0;
  if (d[4] != 0 || d[5] != 0 || d[6] != 0 || d[7] != 8)
    return 0;
  o = d[8] << 24 | d[9] << 16 | d[10] << 8 | d[11];
  i = d[12] << 24 | d[13] << 16 | d[14] << 8 | d[15];
  r = sat_calloc(i ? i : 1, sizeof(char *));
  if (cnt)
    *cnt = i;
  d = h->dp + o;
  for (o = 0; o < i; o++)
    {
      r[o] = (char *)d;
      if (o + 1 < i)
        d += strlen((char *)d) + 1;
      if (d >= h->dp + h->dcnt)
        {
          sat_free(r);
          return 0;
        }
    }
  return r;
}

static char *headtoevr(RpmHead *h)
{
  unsigned int epoch;
  char *version, *v;
  char *release;
  char *evr;

  version  = headstring(h, TAG_VERSION);
  release  = headstring(h, TAG_RELEASE);
  epoch = headint32(h, TAG_EPOCH);
  if (!version || !release)
    {
      fprintf(stderr, "headtoevr: bad rpm header\n");
      exit(1);
    }
  for (v = version; *v >= 0 && *v <= '9'; v++)
    ;
  if (epoch || (v != version && *v == ':'))
    {
      char epochbuf[11];        /* 32bit decimal will fit in */
      sprintf(epochbuf, "%u", epoch);
      evr = sat_malloc(strlen(epochbuf) + 1 + strlen(version) + 1 + strlen(release) + 1);
      sprintf(evr, "%s:%s-%s", epochbuf, version, release);
    }
  else
    {
      evr = sat_malloc(strlen(version) + 1 + strlen(release) + 1);
      sprintf(evr, "%s-%s", version, release);
    }
  return evr;
}


static void
setutf8string(Repodata *repodata, Id handle, Id tag, const char *str)
{
  const unsigned char *cp;
  int state = 0;
  int c;
  char *buf = 0, *bp;

  /* check if it's already utf8, code taken from screen ;-) */
  cp = (const unsigned char *)str;
  while ((c = *cp++) != 0)
    {
      if (state)
	{
          if ((c & 0xc0) != 0x80)
            break; /* encoding error */
          c = (c & 0x3f) | (state << 6);
          if (!(state & 0x40000000))
	    {
              /* check for overlong sequences */
              if ((c & 0x820823e0) == 0x80000000)
                c = 0xfdffffff;
              else if ((c & 0x020821f0) == 0x02000000)
                c = 0xfff7ffff;
              else if ((c & 0x000820f8) == 0x00080000)
                c = 0xffffd000;
              else if ((c & 0x0000207c) == 0x00002000)
                c = 0xffffff70;
            }
        }
      else
	{
          /* new sequence */
          if (c >= 0xfe)
            c = 0xfffd;
          else if (c >= 0xfc)
            c = (c & 0x01) | 0xbffffffc;    /* 5 bytes to follow */
          else if (c >= 0xf8)
            c = (c & 0x03) | 0xbfffff00;    /* 4 */
          else if (c >= 0xf0)
            c = (c & 0x07) | 0xbfffc000;    /* 3 */
          else if (c >= 0xe0)
            c = (c & 0x0f) | 0xbff00000;    /* 2 */
          else if (c >= 0xc2)
            c = (c & 0x1f) | 0xfc000000;    /* 1 */
          else if (c >= 0xc0)
            c = 0xfdffffff;         /* overlong */
          else if (c >= 0x80)
            c = 0xfffd;
        }
      state = (c & 0x80000000) ? c : 0;
    }
  if (c)
    {
      /* not utf8, assume latin1 */
      buf = sat_malloc(2 * strlen(str) + 1);
      cp = (const unsigned char *)str;
      str = buf;
      bp = buf;
      while ((c = *cp++) != 0)
	{
	  if (c >= 0xc0)
	    {
	      *bp++ = 0xc3;
	      c ^= 0x80;
	    }
	  else if (c >= 0x80)
	    *bp++ = 0xc2;
	  *bp++ = c;
	}
      *bp++ = 0;
    }
  repodata_set_str(repodata, handle, tag, str);
  if (buf)
    sat_free(buf);
}

static unsigned int
makedeps(Pool *pool, Repo *repo, RpmHead *rpmhead, int tagn, int tagv, int tagf, int strong)
{
  char **n, **v;
  unsigned int *f;
  int i, cc, nc, vc, fc;
  int haspre = 0;
  unsigned int olddeps;
  Id *ida;

  n = headstringarray(rpmhead, tagn, &nc);
  if (!n)
    return 0;
  v = headstringarray(rpmhead, tagv, &vc);
  if (!v)
    {
      sat_free(n);
      return 0;
    }
  f = headint32array(rpmhead, tagf, &fc);
  if (!f)
    {
      sat_free(n);
      free(v);
      return 0;
    }
  if (nc != vc || nc != fc)
    {
      fprintf(stderr, "bad dependency entries\n");
      exit(1);
    }

  cc = nc;
  if (strong)
    {
      cc = 0;
      for (i = 0; i < nc; i++)
	if ((f[i] & DEP_STRONG) == (strong == 1 ? 0 : DEP_STRONG))
	  {
	    cc++;
	    if ((f[i] & DEP_PRE) != 0)
	      haspre = 1;
	  }
    }
  else
    {
      for (i = 0; i < nc; i++)
	if ((f[i] & DEP_PRE) != 0)
	  {
	    haspre = 1;
	    break;
	  }
    }
  if (tagn != TAG_REQUIRENAME)
     haspre = 0;
  if (cc == 0)
    {
      sat_free(n);
      sat_free(v);
      sat_free(f);
      return 0;
    }
  cc += haspre;
  olddeps = repo_reserve_ids(repo, 0, cc);
  ida = repo->idarraydata + olddeps;
  for (i = 0; ; i++)
    {
      if (i == nc)
	{
	  if (haspre != 1)
	    break;
	  haspre = 2;
	  i = 0;
	  *ida++ = SOLVABLE_PREREQMARKER;
	}
      if (strong && (f[i] & DEP_STRONG) != (strong == 1 ? 0 : DEP_STRONG))
	continue;
      if (haspre == 1 && (f[i] & DEP_PRE) != 0)
	continue;
      if (haspre == 2 && (f[i] & DEP_PRE) == 0)
	continue;
      if (f[i] & (DEP_LESS|DEP_GREATER|DEP_EQUAL))
	{
	  Id name, evr;
	  int flags = 0;
	  if ((f[i] & DEP_LESS) != 0)
	    flags |= 4;
	  if ((f[i] & DEP_EQUAL) != 0)
	    flags |= 2;
	  if ((f[i] & DEP_GREATER) != 0)
	    flags |= 1;
	  name = str2id(pool, n[i], 1);
	  if (v[i][0] == '0' && v[i][1] == ':' && v[i][2])
	    evr = str2id(pool, v[i] + 2, 1);
	  else
	    evr = str2id(pool, v[i], 1);
	  *ida++ = rel2id(pool, name, evr, flags, 1);
	}
      else
        *ida++ = str2id(pool, n[i], 1);
    }
  *ida++ = 0;
  repo->idarraysize += cc + 1;
  sat_free(n);
  sat_free(v);
  sat_free(f);
  return olddeps;
}


#ifdef USE_FILEFILTER

#define FILEFILTER_EXACT    0
#define FILEFILTER_STARTS   1
#define FILEFILTER_CONTAINS 2

struct filefilter {
  int dirmatch;
  char *dir;
  char *base;
};

static struct filefilter filefilters[] = {
  { FILEFILTER_CONTAINS, "/bin/", 0},
  { FILEFILTER_CONTAINS, "/sbin/", 0},
  { FILEFILTER_CONTAINS, "/lib/", 0},
  { FILEFILTER_CONTAINS, "/lib64/", 0},
  { FILEFILTER_CONTAINS, "/etc/", 0},
  { FILEFILTER_STARTS, "/usr/games/", 0},
  { FILEFILTER_EXACT, "/usr/share/dict/", "words"},
  { FILEFILTER_STARTS, "/usr/share/", "magic.mime"},
  { FILEFILTER_STARTS, "/opt/gnome/games/", 0},
};

#endif

static void
adddudata(Pool *pool, Repo *repo, Repodata *repodata, Solvable *s, RpmHead *rpmhead, char **dn, unsigned int *di, int fc, int dic)
{
  Id handle, did;
  int i, fszc;
  unsigned int *fkb, *fn, *fsz, *fm, *fino;
  unsigned int inotest[256], inotestok;

  if (!fc)
    return;
  fsz = headint32array(rpmhead, TAG_FILESIZES, &fszc);
  if (!fsz || fc != fszc)
    {
      sat_free(fsz);
      return;
    }
  /* stupid rpm recodrs sizes of directories, so we have to check the mode */
  fm = headint16array(rpmhead, TAG_FILEMODES, &fszc);
  if (!fm || fc != fszc)
    {
      sat_free(fsz);
      sat_free(fm);
      return;
    }
  fino = headint32array(rpmhead, TAG_FILEINODES, &fszc);
  if (!fino || fc != fszc)
    {
      sat_free(fsz);
      sat_free(fm);
      sat_free(fino);
      return;
    }
  inotestok = 0;
  if (fc < sizeof(inotest))
    {
      memset(inotest, 0, sizeof(inotest));
      for (i = 0; i < fc; i++)
	{
	  int off, bit;
	  if (fsz[i] == 0 || !S_ISREG(fm[i]))
	    continue;
	  off = (fino[i] >> 5) & (sizeof(inotest)/sizeof(*inotest) - 1);
	  bit = 1 << (fino[i] & 31);
	  if ((inotest[off] & bit) != 0)
	    break;
	  inotest[off] |= bit;
	}
      if (i == fc)
	inotestok = 1;
    }
  if (!inotestok)
    {
      unsigned int *fdev = headint32array(rpmhead, TAG_FILEDEVICES, &fszc);
      unsigned int *fx, j;
      unsigned int mask, hash, hh;
      if (!fdev || fc != fszc)
	{
	  sat_free(fsz);
	  sat_free(fm);
	  sat_free(fdev);
	  sat_free(fino);
	  return;
	}
      mask = fc;
      while ((mask & (mask - 1)) != 0)
	mask = mask & (mask - 1);
      mask <<= 2;
      if (mask > sizeof(inotest)/sizeof(*inotest))
        fx = sat_calloc(mask, sizeof(unsigned int));
      else
	{
	  fx = inotest;
	  memset(fx, 0, mask * sizeof(unsigned int));
	}
      mask--;
      for (i = 0; i < fc; i++)
	{
	  if (fsz[i] == 0 || !S_ISREG(fm[i]))
	    continue;
	  hash = (fino[i] + fdev[i] * 31) & mask;
          hh = 7;
	  while ((j = fx[hash]) != 0)
	    {
	      if (fino[j - 1] == fino[i] && fdev[j - 1] == fdev[i])
		{
		  fsz[i] = 0;	/* kill entry */
		  break;
		}
	      hash = (hash + hh++) & mask;
	    }
	  if (!j)
	    fx[hash] = i + 1;
	}
      if (fx != inotest)
        sat_free(fx);
      sat_free(fdev);
    }
  sat_free(fino);
  fn = sat_calloc(dic, sizeof(unsigned int));
  fkb = sat_calloc(dic, sizeof(unsigned int));
  for (i = 0; i < fc; i++)
    {
      if (fsz[i] == 0 || !S_ISREG(fm[i]))
	continue;
      if (di[i] >= dic)
	continue;
      fn[di[i]]++;
      fkb[di[i]] += fsz[i] / 1024 + 1;
    }
  sat_free(fsz);
  sat_free(fm);
  /* commit */
  repodata_extend(repodata, s - pool->solvables);
  handle = (s - pool->solvables) - repodata->start;
  handle = repodata_get_handle(repodata, handle);
  for (i = 0; i < fc; i++)
    {
      if (!fn[i])
	continue;
      if (!*dn[i] && (s->arch == ARCH_SRC || s->arch == ARCH_NOSRC))
        did = repodata_str2dir(repodata, "/usr/src", 1);
      else
        did = repodata_str2dir(repodata, dn[i], 1);
      repodata_add_dirnumnum(repodata, handle, SOLVABLE_DISKUSAGE, did, fkb[i], fn[i]);
    }
  sat_free(fn);
  sat_free(fkb);
}

/* assumes last processed array is provides! */
static unsigned int
addfileprovides(Pool *pool, Repo *repo, Repodata *repodata, Solvable *s, RpmHead *rpmhead, unsigned int olddeps)
{
  char **bn;
  char **dn;
  unsigned int *di;
  int bnc, dnc, dic;
  int i;
#ifdef USE_FILEFILTER
  int j;
  struct filefilter *ff;
#endif
#if 0
  char *fn = 0;
  int fna = 0;
#endif

  if (!repodata)
    return olddeps;
  bn = headstringarray(rpmhead, TAG_BASENAMES, &bnc);
  if (!bn)
    return olddeps;
  dn = headstringarray(rpmhead, TAG_DIRNAMES, &dnc);
  if (!dn)
    {
      sat_free(bn);
      return olddeps;
    }
  di = headint32array(rpmhead, TAG_DIRINDEXES, &dic);
  if (!di)
    {
      sat_free(bn);
      sat_free(dn);
      return olddeps;
    }
  if (bnc != dic)
    {
      fprintf(stderr, "bad filelist\n");
      exit(1);
    }

  if (repodata)
    adddudata(pool, repo, repodata, s, rpmhead, dn, di, bnc, dic);

  for (i = 0; i < bnc; i++)
    {
#ifdef USE_FILEFILTER
      ff = filefilters;
      for (j = 0; j < sizeof(filefilters)/sizeof(*filefilters); j++, ff++)
	{
	  if (ff->dir)
	    {
	      switch (ff->dirmatch)
		{
		case FILEFILTER_STARTS:
		  if (strncmp(dn[di[i]], ff->dir, strlen(ff->dir)))
		    continue;
		  break;
		case FILEFILTER_CONTAINS:
		  if (!strstr(dn[di[i]], ff->dir))
		    continue;
		  break;
		case FILEFILTER_EXACT:
		default:
		  if (strcmp(dn[di[i]], ff->dir))
		    continue;
		  break;
		}
	    }
	  if (ff->base)
	    {
	      if (strcmp(bn[i], ff->base))
		continue;
	    }
	  break;
	}
      if (j == sizeof(filefilters)/sizeof(*filefilters))
	continue;
#endif
#if 0
      j = strlen(bn[i]) + strlen(dn[di[i]]) + 1;
      if (j > fna)
	{
	  fna = j + 256;
	  fn = sat_realloc(fn, fna);
	}
      strcpy(fn, dn[di[i]]);
      strcat(fn, bn[i]);
      olddeps = repo_addid_dep(repo, olddeps, str2id(pool, fn, 1), SOLVABLE_FILEMARKER);
#endif
      if (repodata)
	{
	  Id handle, did;
	  repodata_extend(repodata, s - pool->solvables);
	  handle = (s - pool->solvables) - repodata->start;
	  handle = repodata_get_handle(repodata, handle);
	  did = repodata_str2dir(repodata, dn[di[i]], 1);
	  if (!did)
	    did = repodata_str2dir(repodata, "/", 1);
	  repodata_add_dirstr(repodata, handle, SOLVABLE_FILELIST, did, bn[i]);
	}
    }
#if 0
  if (fn)
    sat_free(fn);
#endif
  sat_free(bn);
  sat_free(dn);
  sat_free(di);
  return olddeps;
}

static void
addsourcerpm(Pool *pool, Repodata *repodata, Id handle, char *sourcerpm, char *name, char *evr)
{
  const char *p, *sevr, *sarch;

  p = strrchr(sourcerpm, '.');
  if (!p || strcmp(p, ".rpm") != 0)
    return;
  p--;
  while (p > sourcerpm && *p != '.')
    p--;
  if (*p != '.' || p == sourcerpm)
    return;
  sarch = p-- + 1;
  while (p > sourcerpm && *p != '-')
    p--;
  if (*p != '-' || p == sourcerpm)
    return;
  p--;
  while (p > sourcerpm && *p != '-')
    p--;
  if (*p != '-' || p == sourcerpm)
    return;
  sevr = p + 1;
  if (!strcmp(sarch, "src.rpm"))
    repodata_set_constantid(repodata, handle, SOLVABLE_SOURCEARCH, ARCH_SRC);
  else if (!strcmp(sarch, "nosrc.rpm"))
    repodata_set_constantid(repodata, handle, SOLVABLE_SOURCEARCH, ARCH_NOSRC);
  else
    repodata_set_constantid(repodata, handle, SOLVABLE_SOURCEARCH, strn2id(pool, sarch, strlen(sarch) - 4, 1));
  if (!strncmp(sevr, evr, sarch - sevr - 1) && evr[sarch - sevr - 1] == 0)
    repodata_set_void(repodata, handle, SOLVABLE_SOURCEEVR);
  else
    repodata_set_id(repodata, handle, SOLVABLE_SOURCEEVR, strn2id(pool, sevr, sarch - sevr - 1, 1));
  if (!strncmp(sourcerpm, name, sevr - sourcerpm - 1) && name[sevr - sourcerpm - 1] == 0)
    repodata_set_void(repodata, handle, SOLVABLE_SOURCENAME);
  else
    repodata_set_id(repodata, handle, SOLVABLE_SOURCENAME, strn2id(pool, sourcerpm, sevr - sourcerpm - 1, 1));
}

static int
rpm2solv(Pool *pool, Repo *repo, Repodata *repodata, Solvable *s, RpmHead *rpmhead)
{
  char *name;
  char *evr;
  char *sourcerpm;

  name = headstring(rpmhead, TAG_NAME);
  if (!strcmp(name, "gpg-pubkey"))
    return 0;
  s->name = str2id(pool, name, 1);
  if (!s->name)
    {
      fprintf(stderr, "package has no name\n");
      exit(1);
    }
  sourcerpm = headstring(rpmhead, TAG_SOURCERPM);
  if (sourcerpm)
    s->arch = str2id(pool, headstring(rpmhead, TAG_ARCH), 1);
  else
    {
      if (headexists(rpmhead, TAG_NOSOURCE) || headexists(rpmhead, TAG_NOPATCH))
        s->arch = ARCH_NOSRC;
      else
        s->arch = ARCH_SRC;
    }
  if (!s->arch)
    s->arch = ARCH_NOARCH;
  evr = headtoevr(rpmhead);
  s->evr = str2id(pool, evr, 1);
  s->vendor = str2id(pool, headstring(rpmhead, TAG_VENDOR), 1);

  s->provides = makedeps(pool, repo, rpmhead, TAG_PROVIDENAME, TAG_PROVIDEVERSION, TAG_PROVIDEFLAGS, 0);
  s->provides = addfileprovides(pool, repo, repodata, s, rpmhead, s->provides);
  if (s->arch != ARCH_SRC && s->arch != ARCH_NOSRC)
    s->provides = repo_addid_dep(repo, s->provides, rel2id(pool, s->name, s->evr, REL_EQ, 1), 0);
  s->requires = makedeps(pool, repo, rpmhead, TAG_REQUIRENAME, TAG_REQUIREVERSION, TAG_REQUIREFLAGS, 0);
  s->conflicts = makedeps(pool, repo, rpmhead, TAG_CONFLICTNAME, TAG_CONFLICTVERSION, TAG_CONFLICTFLAGS, 0);
  s->obsoletes = makedeps(pool, repo, rpmhead, TAG_OBSOLETENAME, TAG_OBSOLETEVERSION, TAG_OBSOLETEFLAGS, 0);

  s->recommends = makedeps(pool, repo, rpmhead, TAG_SUGGESTSNAME, TAG_SUGGESTSVERSION, TAG_SUGGESTSFLAGS, 2);
  s->suggests = makedeps(pool, repo, rpmhead, TAG_SUGGESTSNAME, TAG_SUGGESTSVERSION, TAG_SUGGESTSFLAGS, 1);
  s->supplements = makedeps(pool, repo, rpmhead, TAG_ENHANCESNAME, TAG_ENHANCESVERSION, TAG_ENHANCESFLAGS, 2);
  s->enhances  = makedeps(pool, repo, rpmhead, TAG_ENHANCESNAME, TAG_ENHANCESVERSION, TAG_ENHANCESFLAGS, 1);
  s->freshens = 0;
  s->supplements = repo_fix_legacy(repo, s->provides, s->supplements);

  if (repodata)
    {
      Id handle;
      char *str;
      unsigned int u32;

      repodata_extend(repodata, s - pool->solvables);
      handle = repodata_get_handle(repodata, (s - pool->solvables) - repodata->start);
      str = headstring(rpmhead, TAG_SUMMARY);
      if (str)
        setutf8string(repodata, handle, SOLVABLE_SUMMARY, str);
      str = headstring(rpmhead, TAG_DESCRIPTION);
      if (str)
	{
	  char *aut, *p;
	  for (aut = str; (aut = strchr(aut, '\n')) != 0; aut++)
	    if (!strncmp(aut, "\nAuthors:\n--------\n", 19))
	      break;
	  if (aut)
	    {
	      /* oh my, found SUSE special author section */
	      int l = aut - str;
	      str = strdup(str);
	      aut = str + l;
	      str[l] = 0;
	      while (l > 0 && str[l - 1] == '\n')
	        str[--l] = 0;
	      if (l)
                setutf8string(repodata, handle, SOLVABLE_DESCRIPTION, str);
	      p = aut + 19;
	      aut = str;	/* copy over */
	      while (*p == ' ' || *p == '\n')
		p++;
	      while (*p)
		{
		  if (*p == '\n')
		    {
		      *aut++ = *p++;
		      while (*p == ' ')
			p++;
		      continue;
		    }
		  *aut++ = *p++;
		}
	      while (aut != str && aut[-1] == '\n')
		aut--;
	      *aut = 0;
	      if (*str)
	        setutf8string(repodata, handle, SOLVABLE_AUTHORS, str);
	      free(str);
	    }
	  else if (*str)
	    setutf8string(repodata, handle, SOLVABLE_DESCRIPTION, str);
	}
      str = headstring(rpmhead, TAG_GROUP);
      if (str)
        repodata_set_poolstr(repodata, handle, SOLVABLE_GROUP, str);
      str = headstring(rpmhead, TAG_LICENSE);
      if (str)
        repodata_set_poolstr(repodata, handle, SOLVABLE_LICENSE, str);
      str = headstring(rpmhead, TAG_URL);
      if (str)
	repodata_set_str(repodata, handle, SOLVABLE_URL, str);
      str = headstring(rpmhead, TAG_DISTRIBUTION);
      if (str)
	repodata_set_poolstr(repodata, handle, SOLVABLE_DISTRIBUTION, str);
      str = headstring(rpmhead, TAG_PACKAGER);
      if (str)
	repodata_set_poolstr(repodata, handle, SOLVABLE_PACKAGER, str);
      u32 = headint32(rpmhead, TAG_BUILDTIME);
      if (u32)
        repodata_set_num(repodata, handle, SOLVABLE_BUILDTIME, u32);
      u32 = headint32(rpmhead, TAG_INSTALLTIME);
      if (u32)
        repodata_set_num(repodata, handle, SOLVABLE_INSTALLTIME, u32);
      u32 = headint32(rpmhead, TAG_SIZE);
      if (u32)
        repodata_set_num(repodata, handle, SOLVABLE_INSTALLSIZE, (u32 + 1023) / 1024);
      if (sourcerpm)
	addsourcerpm(pool, repodata, handle, sourcerpm, name, evr);
    }
  sat_free(evr);
  return 1;
}

static Id
copyreldep(Pool *pool, Pool *frompool, Id id)
{
  Reldep *rd = GETRELDEP(frompool, id);
  Id name = rd->name, evr = rd->evr;
  if (ISRELDEP(name))
    name = copyreldep(pool, frompool, name);
  else
    name = str2id(pool, id2str(frompool, name), 1);
  if (ISRELDEP(evr))
    evr = copyreldep(pool, frompool, evr);
  else
    evr = str2id(pool, id2str(frompool, evr), 1);
  return rel2id(pool, name, evr, rd->flags, 1);
}

static Offset
copydeps(Pool *pool, Repo *repo, Offset fromoff, Repo *fromrepo)
{
  int cc;
  Id id, *ida, *from;
  Offset ido;
  Pool *frompool = fromrepo->pool;

  if (!fromoff)
    return 0;
  from = fromrepo->idarraydata + fromoff;
  for (ida = from, cc = 0; *ida; ida++, cc++)
    ;
  if (cc == 0)
    return 0;
  ido = repo_reserve_ids(repo, 0, cc);
  ida = repo->idarraydata + ido;
  if (frompool && pool != frompool)
    {
      while (*from)
	{
	  id = *from++;
	  if (ISRELDEP(id))
	    id = copyreldep(pool, frompool, id);
	  else
	    id = str2id(pool, id2str(frompool, id), 1);
	  *ida++ = id;
	}
      *ida = 0;
    }
  else
    memcpy(ida, from, (cc + 1) * sizeof(Id));
  repo->idarraysize += cc + 1;
  return ido;
}

static Id copydir_complex(Pool *pool, Repodata *data, Stringpool *fromspool, Repodata *fromdata, Id did, Id *cache);

static inline Id
copydir(Pool *pool, Repodata *data, Stringpool *fromspool, Repodata *fromdata, Id did, Id *cache)
{
  if (cache && cache[did & 255] == did)
    return cache[(did & 255) + 256];
  return copydir_complex(pool, data, fromspool, fromdata, did, cache);
}

static Id
copydir_complex(Pool *pool, Repodata *data, Stringpool *fromspool, Repodata *fromdata, Id did, Id *cache)
{
  Id parent = dirpool_parent(&fromdata->dirpool, did);
  Id compid = dirpool_compid(&fromdata->dirpool, did);
  if (parent)
    parent = copydir(pool, data, fromspool, fromdata, parent, cache);
  if (fromspool != &pool->ss)
    compid = str2id(pool, stringpool_id2str(fromspool, compid), 1);
  compid = dirpool_add_dir(&data->dirpool, parent, compid, 1);
  if (cache)
    {
      cache[did & 255] = did;
      cache[(did & 255) + 256] = compid;
    }
  return compid;
}

struct solvable_copy_cbdata {
  Repodata *data;
  Id handle;
  Id *dircache;
};

static int
solvable_copy_cb(void *vcbdata, Solvable *r, Repodata *fromdata, Repokey *key, KeyValue *kv)
{
  struct solvable_copy_cbdata *cbdata = vcbdata;
  Id id, keyname;
  Repodata *data = cbdata->data;
  Id handle = cbdata->handle;
  Pool *pool = data->repo->pool, *frompool = fromdata->repo->pool;
  Stringpool *fromspool = fromdata->localpool ? &fromdata->spool : &frompool->ss;

  keyname = key->name;
  if (keyname >= ID_NUM_INTERNAL)
    keyname = str2id(pool, id2str(frompool, keyname), 1);
  switch(key->type)
    {
    case REPOKEY_TYPE_ID:
    case REPOKEY_TYPE_CONSTANTID:
      id = kv->id;
      assert(!data->localpool);	/* implement me! */
      if (pool != frompool || fromdata->localpool)
	{
	  if (ISRELDEP(id))
	    id = copyreldep(pool, frompool, id);
	  else
	    id = str2id(pool, stringpool_id2str(fromspool, id), 1);
	}
      if (key->type == REPOKEY_TYPE_ID)
        repodata_set_id(data, handle, keyname, id);
      else
        repodata_set_constantid(data, handle, keyname, id);
      break;
    case REPOKEY_TYPE_STR:
      repodata_set_str(data, handle, keyname, kv->str);
      break;
    case REPOKEY_TYPE_VOID:
      repodata_set_void(data, handle, keyname);
      break;
    case REPOKEY_TYPE_NUM:
      repodata_set_num(data, handle, keyname, kv->num);
      break;
    case REPOKEY_TYPE_CONSTANT:
      repodata_set_constant(data, handle, keyname, kv->num);
      break;
    case REPOKEY_TYPE_DIRNUMNUMARRAY:
      id = kv->id;
      assert(!data->localpool);	/* implement me! */
      id = copydir(pool, data, fromspool, fromdata, id, cbdata->dircache);
      repodata_add_dirnumnum(data, handle, keyname, id, kv->num, kv->num2);
      break;
    case REPOKEY_TYPE_DIRSTRARRAY:
      id = kv->id;
      assert(!data->localpool);	/* implement me! */
      id = copydir(pool, data, fromspool, fromdata, id, cbdata->dircache);
      repodata_add_dirstr(data, handle, keyname, id, kv->str);
      break;
    default:
      break;
    }
  return 0;
}

static void
solvable_copy(Solvable *s, Solvable *r, Repodata *data, Id *dircache)
{
  Repo *repo = s->repo;
  Repo *fromrepo = r->repo;
  Pool *pool = repo->pool;
  struct solvable_copy_cbdata cbdata;

  /* copy solvable data */
  if (pool == fromrepo->pool)
    {
      s->name = r->name;
      s->evr = r->evr;
      s->arch = r->arch;
      s->vendor = r->vendor;
    }
  else
    {
      if (r->name)
	s->name = str2id(pool, id2str(fromrepo->pool, r->name), 1);
      if (r->evr)
	s->evr = str2id(pool, id2str(fromrepo->pool, r->evr), 1);
      if (r->arch)
	s->arch = str2id(pool, id2str(fromrepo->pool, r->arch), 1);
      if (r->vendor)
	s->vendor = str2id(pool, id2str(fromrepo->pool, r->vendor), 1);
    }
  s->provides = copydeps(pool, repo, r->provides, fromrepo);
  s->requires = copydeps(pool, repo, r->requires, fromrepo);
  s->conflicts = copydeps(pool, repo, r->conflicts, fromrepo);
  s->obsoletes = copydeps(pool, repo, r->obsoletes, fromrepo);
  s->recommends = copydeps(pool, repo, r->recommends, fromrepo);
  s->suggests = copydeps(pool, repo, r->suggests, fromrepo);
  s->supplements = copydeps(pool, repo, r->supplements, fromrepo);
  s->enhances  = copydeps(pool, repo, r->enhances, fromrepo);
  s->freshens = copydeps(pool, repo, r->freshens, fromrepo);

  /* copy all attributes */
  if (!data)
    return;
  repodata_extend(data, s - pool->solvables);
  cbdata.data = data;
  cbdata.handle = repodata_get_handle(data, (s - pool->solvables) - data->start);
  cbdata.dircache = dircache;
  repo_search(fromrepo, (r - fromrepo->pool->solvables), 0, 0, SEARCH_NO_STORAGE_SOLVABLE, solvable_copy_cb, &cbdata);
}

/* used to sort entries returned in some database order */
static int
rpmids_sort_cmp(const void *va, const void *vb)
{
  struct rpmid const *a = va, *b = vb;
  int r;
  r = strcmp(a->name, b->name);
  if (r)
    return r;
  return a->dbid - b->dbid;
}

static Repo *pkgids_sort_cmp_data;

static int
pkgids_sort_cmp(const void *va, const void *vb)
{
  Pool *pool = pkgids_sort_cmp_data->pool;
  Solvable *a = pool->solvables + *(Id *)va;
  Solvable *b = pool->solvables + *(Id *)vb;
  Id *rpmdbid;

  if (a->name != b->name)
    return strcmp(id2str(pool, a->name), id2str(pool, b->name));
  rpmdbid = pkgids_sort_cmp_data->rpmdbid;
  return rpmdbid[(a - pool->solvables) - pkgids_sort_cmp_data->start] - rpmdbid[(b - pool->solvables) - pkgids_sort_cmp_data->start];
}

static void
swap_solvables(Repo *repo, Repodata *data, Id pa, Id pb)
{
  Pool *pool = repo->pool;
  Solvable tmp;

  tmp = pool->solvables[pa];
  pool->solvables[pa] = pool->solvables[pb];
  pool->solvables[pb] = tmp;
  if (repo->rpmdbid)
    {
      Id tmpid = repo->rpmdbid[pa - repo->start];
      repo->rpmdbid[pa - repo->start] = repo->rpmdbid[pb - repo->start];
      repo->rpmdbid[pb - repo->start] = tmpid;
    }
  /* only works if nothing is already internalized! */
  if (data && data->attrs)
    {
      Id tmpattrs = data->attrs[pa - data->start];
      data->attrs[pa - data->start] = data->attrs[pb - data->start];
      data->attrs[pb - data->start] = tmpattrs;
    }
}

static void
mkrpmdbcookie(struct stat *st, unsigned char *cookie)
{
  memset(cookie, 0, 32);
  cookie[3] = RPMDB_COOKIE_VERSION;
  memcpy(cookie + 16, &st->st_ino, sizeof(st->st_ino));
  memcpy(cookie + 24, &st->st_dev, sizeof(st->st_dev));
}

/*
 * read rpm db as repo
 * 
 */

void
repo_add_rpmdb(Repo *repo, Repo *ref, const char *rootdir)
{
  Pool *pool = repo->pool;
  unsigned char buf[16];
  DB *db = 0;
  DBC *dbc = 0;
  int byteswapped;
  unsigned int dbid;
  unsigned char *dp, *dbidp;
  int dl, nrpmids;
  struct rpmid *rpmids, *rp;
  int i;
  int rpmheadsize;
  RpmHead *rpmhead;
  Solvable *s;
  Id id, *refhash;
  unsigned int refmask, h;
  int asolv;
  Repodata *repodata;
  char dbpath[PATH_MAX];
  DB_ENV *dbenv = 0;
  DBT dbkey;
  DBT dbdata;
  struct stat packagesstat;

  memset(&dbkey, 0, sizeof(dbkey));
  memset(&dbdata, 0, sizeof(dbdata));

  if (repo->start != repo->end)
    abort();		/* FIXME: rpmdbid */

  if (!rootdir)
    rootdir = "";

  repodata = repo_add_repodata(repo, 0);

  if (ref && !(ref->nsolvables && ref->rpmdbid))
    ref = 0;

  if (db_env_create(&dbenv, 0))
    {
      perror("db_env_create");
      exit(1);
    }
  snprintf(dbpath, PATH_MAX, "%s/var/lib/rpm", rootdir);
  if (dbenv->open(dbenv, dbpath, DB_CREATE|DB_PRIVATE|DB_INIT_MPOOL, 0))
    {
      perror("dbenv open");
      exit(1);
    }
  if (db_create(&db, dbenv, 0))
    {
      perror("db_create");
      exit(1);
    }

  /* XXX: should get ro lock of Packages database! */
  snprintf(dbpath, PATH_MAX, "%s/var/lib/rpm/Packages", rootdir);
  if (stat(dbpath, &packagesstat))
    {
      perror(dbpath);
      exit(1);
    }
  mkrpmdbcookie(&packagesstat, repo->rpmdbcookie);

  if (!ref || memcmp(repo->rpmdbcookie, ref->rpmdbcookie, 32) != 0)
    {
      Id *pkgids;
      if (db->open(db, 0, dbpath, 0, DB_HASH, DB_RDONLY, 0664))
	{
	  perror("db->open var/lib/rpm/Packages");
	  exit(1);
	}
      if (db->get_byteswapped(db, &byteswapped))
	{
	  perror("db->get_byteswapped");
	  exit(1);
	}
      if (db->cursor(db, NULL, &dbc, 0))
	{
	  perror("db->cursor");
	  exit(1);
	}
      dbidp = (unsigned char *)&dbid;
      repo->rpmdbid = sat_calloc(256, sizeof(Id));
      asolv = 256;
      rpmheadsize = 0;
      rpmhead = 0;
      i = 0;
      s = 0;
      while (dbc->c_get(dbc, &dbkey, &dbdata, DB_NEXT) == 0)
	{
	  if (!s)
	    s = pool_id2solvable(pool, repo_add_solvable(repo));
	  if (i >= asolv)
	    {
	      repo->rpmdbid = sat_realloc(repo->rpmdbid, (asolv + 256) * sizeof(Id));
	      memset(repo->rpmdbid + asolv, 0, 256 * sizeof(unsigned int));
	      asolv += 256;
	    }
          if (dbkey.size != 4)
	    {
	      fprintf(stderr, "corrupt Packages database (key size)\n");
	      exit(1);
	    }
	  dp = dbkey.data;
	  if (byteswapped)
	    {
	      dbidp[0] = dp[3];
	      dbidp[1] = dp[2];
	      dbidp[2] = dp[1];
	      dbidp[3] = dp[0];
	    }
	  else
	    memcpy(dbidp, dp, 4);
	  if (dbid == 0)		/* the join key */
	    continue;
	  if (dbdata.size < 8)
	    {
	      fprintf(stderr, "corrupt rpm database (size %u)\n", dbdata.size);
	      exit(1);
	    }
	  if (dbdata.size > rpmheadsize)
	    rpmhead = sat_realloc(rpmhead, sizeof(*rpmhead) + dbdata.size);
	  memcpy(buf, dbdata.data, 8);
	  rpmhead->cnt = buf[0] << 24  | buf[1] << 16  | buf[2] << 8 | buf[3];
	  rpmhead->dcnt = buf[4] << 24  | buf[5] << 16  | buf[6] << 8 | buf[7];
	  if (8 + rpmhead->cnt * 16 + rpmhead->dcnt > dbdata.size)
	    {
	      fprintf(stderr, "corrupt rpm database (data size)\n");
	      exit(1);
	    }
	  memcpy(rpmhead->data, (unsigned char *)dbdata.data + 8, rpmhead->cnt * 16 + rpmhead->dcnt);
	  rpmhead->dp = rpmhead->data + rpmhead->cnt * 16;
	  repo->rpmdbid[i] = dbid;
	  if (rpm2solv(pool, repo, repodata, s, rpmhead))
	    {
	      i++;
	      s = 0;
	    }
	  else
	    {
	      /* We can reuse this solvable, but make sure it's still
		 associated with this repo.  */
	      memset(s, 0, sizeof(*s));
	      s->repo = repo;
	    }
	}
      if (s)
	{
	  /* oops, could not reuse. free it instead */
          repo_free_solvable_block(repo, s - pool->solvables, 1, 1);
	  s = 0;
	}
      dbc->c_close(dbc);
      db->close(db, 0);
      db = 0;
      /* now sort all solvables */
      if (repo->end - repo->start > 1)
	{
	  pkgids = sat_malloc2(repo->end - repo->start, sizeof(Id));
	  for (i = repo->start; i < repo->end; i++)
	    pkgids[i - repo->start] = i;
	  pkgids_sort_cmp_data = repo;
	  qsort(pkgids, repo->end - repo->start, sizeof(Id), pkgids_sort_cmp);
	  /* adapt order */
	  for (i = repo->start; i < repo->end; i++)
	    {
	      int j = pkgids[i - repo->start];
	      while (j < i)
		j = pkgids[i - repo->start] = pkgids[j - repo->start];
	      if (j != i)
	        swap_solvables(repo, repodata, i, j);
	    }
	  sat_free(pkgids);
	}
    }
  else
    {
      Id dircache[512];

      memset(dircache, 0, sizeof(dircache));
      snprintf(dbpath, PATH_MAX, "%s/var/lib/rpm/Name", rootdir);
      if (db->open(db, 0, dbpath, 0, DB_HASH, DB_RDONLY, 0664))
	{
	  perror("db->open var/lib/rpm/Name");
	  exit(1);
	}
      if (db->get_byteswapped(db, &byteswapped))
	{
	  perror("db->get_byteswapped");
	  exit(1);
	}
      if (db->cursor(db, NULL, &dbc, 0))
	{
	  perror("db->cursor");
	  exit(1);
	}
      dbidp = (unsigned char *)&dbid;
      nrpmids = 0;
      rpmids = 0;
      while (dbc->c_get(dbc, &dbkey, &dbdata, DB_NEXT) == 0)
	{
	  if (dbkey.size == 10 && !memcmp(dbkey.data, "gpg-pubkey", 10))
	    continue;
	  dl = dbdata.size;
	  dp = dbdata.data;
	  while(dl >= 8)
	    {
	      if (byteswapped)
		{
		  dbidp[0] = dp[3];
		  dbidp[1] = dp[2];
		  dbidp[2] = dp[1];
		  dbidp[3] = dp[0];
		}
	      else
		memcpy(dbidp, dp, 4);
	      rpmids = sat_extend(rpmids, nrpmids, 1, sizeof(*rpmids), 255);
	      rpmids[nrpmids].dbid = dbid;
	      rpmids[nrpmids].name = sat_malloc((int)dbkey.size + 1);
	      memcpy(rpmids[nrpmids].name, dbkey.data, (int)dbkey.size);
	      rpmids[nrpmids].name[(int)dbkey.size] = 0;
	      nrpmids++;
	      dp += 8;
	      dl -= 8;
	    }
	}
      dbc->c_close(dbc);
      db->close(db, 0);
      db = 0;

      /* sort rpmids */
      qsort(rpmids, nrpmids, sizeof(*rpmids), rpmids_sort_cmp);

      rp = rpmids;
      dbidp = (unsigned char *)&dbid;
      rpmheadsize = 0;
      rpmhead = 0;

      /* create hash from dbid to ref */
      refmask = mkmask(ref->nsolvables);
      refhash = sat_calloc(refmask + 1, sizeof(Id));
      for (i = 0; i < ref->nsolvables; i++)
	{
	  h = ref->rpmdbid[i] & refmask;
	  while (refhash[h])
	    h = (h + 317) & refmask;
	  refhash[h] = i + 1;	/* make it non-zero */
	}

      repo->rpmdbid = sat_calloc(nrpmids, sizeof(unsigned int));
      s = pool_id2solvable(pool, repo_add_solvable_block(repo, nrpmids));

      for (i = 0; i < nrpmids; i++, rp++, s++)
	{
	  dbid = rp->dbid;
	  repo->rpmdbid[i] = dbid;
	  if (refhash)
	    {
	      h = dbid & refmask;
	      while ((id = refhash[h]))
		{
		  if (ref->rpmdbid[id - 1] == dbid)
		    break;
		  h = (h + 317) & refmask;
		}
	      if (id)
		{
		  Solvable *r = ref->pool->solvables + ref->start + (id - 1);
		  solvable_copy(s, r, repodata, dircache);
		  continue;
		}
	    }
	  if (!db)
	    {
	      if (db_create(&db, 0, 0))
		{
		  perror("db_create");
		  exit(1);
		}
	      snprintf(dbpath, PATH_MAX, "%s/var/lib/rpm/Packages", rootdir);
	      if (db->open(db, 0, dbpath, 0, DB_HASH, DB_RDONLY, 0664))
		{
		  perror("db->open var/lib/rpm/Packages");
		  exit(1);
		}
	      if (db->get_byteswapped(db, &byteswapped))
		{
		  perror("db->get_byteswapped");
		  exit(1);
		}
	    }
	  if (byteswapped)
	    {
	      buf[0] = dbidp[3];
	      buf[1] = dbidp[2];
	      buf[2] = dbidp[1];
	      buf[3] = dbidp[0];
	    }
	  else
	    memcpy(buf, dbidp, 4);
	  dbkey.data = buf;
	  dbkey.size = 4;
	  dbdata.data = 0;
	  dbdata.size = 0;
	  if (db->get(db, NULL, &dbkey, &dbdata, 0))
	    {
	      perror("db->get");
	      fprintf(stderr, "corrupt rpm database\n");
	      exit(1);
	    }
	  if (dbdata.size < 8)
	    {
	      fprintf(stderr, "corrupt rpm database (size)\n");
	      exit(1);
	    }
	  if (dbdata.size > rpmheadsize)
	    rpmhead = sat_realloc(rpmhead, sizeof(*rpmhead) + dbdata.size);
	  memcpy(buf, dbdata.data, 8);
	  rpmhead->cnt = buf[0] << 24  | buf[1] << 16  | buf[2] << 8 | buf[3];
	  rpmhead->dcnt = buf[4] << 24  | buf[5] << 16  | buf[6] << 8 | buf[7];
	  if (8 + rpmhead->cnt * 16 + rpmhead->dcnt > dbdata.size)
	    {
	      fprintf(stderr, "corrupt rpm database (data size)\n");
	      exit(1);
	    }
	  memcpy(rpmhead->data, (unsigned char *)dbdata.data + 8, rpmhead->cnt * 16 + rpmhead->dcnt);
	  rpmhead->dp = rpmhead->data + rpmhead->cnt * 16;

	  rpm2solv(pool, repo, repodata, s, rpmhead);
	}

      if (refhash)
	sat_free(refhash);
      if (rpmids)
	{
	  for (i = 0; i < nrpmids; i++)
	    sat_free(rpmids[i].name);
	  sat_free(rpmids);
	}
    }
  if (rpmhead)
    sat_free(rpmhead);
  if (db)
    db->close(db, 0);
  dbenv->close(dbenv, 0);
  if (repodata)
    repodata_internalize(repodata);
}

static inline unsigned int
getu32(unsigned char *dp)
{
  return dp[0] << 24 | dp[1] << 16 | dp[2] << 8 | dp[3];
}

static void
add_location(Repodata *data, Solvable *s, Id handle, const char *location)
{
  Pool *pool = s->repo->pool;
  const char *name, *n1, *n2;
  int l;

  repodata_extend(data, s - pool->solvables);

  /* skip ./ prefix */
  if (location[0] == '.' && location[1] == '/' && location[2] != '/')
    location += 2;

  name = strrchr(location, '/');
  if (!name)
    name = location;
  else
    {
      name++;
      n2 = id2str(pool, s->arch);
      l = strlen(n2);
      if (strncmp(location, n2, l) != 0 || location + l + 1 != name)
	{
	  /* too bad, need to store directory */
	  char *dir = strdup(location);
	  dir[name - location - 1] = 0;
	  repodata_set_str(data, handle, SOLVABLE_MEDIADIR, dir);
	  free(dir);
	}
      else
        repodata_set_void(data, handle, SOLVABLE_MEDIADIR);
    }
  n1 = name;
  for (n2 = id2str(pool, s->name); *n2; n1++, n2++)
    if (*n1 != *n2)
      break;
  if (*n2 || *n1 != '-')
    goto nontrivial;
  n1++;
  for (n2 = id2str (pool, s->evr); *n2; n1++, n2++)
    if (*n1 != *n2)
      break;
  if (*n2 || *n1 != '.')
    goto nontrivial;
  n1++;
  for (n2 = id2str (pool, s->arch); *n2; n1++, n2++)
    if (*n1 != *n2)
      break;
  if (*n2 || strcmp (n1, ".rpm"))
    goto nontrivial;
  repodata_set_void(data, handle, SOLVABLE_MEDIAFILE);
  return;

nontrivial:
  repodata_set_str(data, handle, SOLVABLE_MEDIAFILE, name);
  return;
}

void
repo_add_rpms(Repo *repo, const char **rpms, int nrpms)
{
  int i, sigdsize, sigcnt, l;
  Pool *pool = repo->pool;
  Solvable *s;
  Repodata *repodata;
  RpmHead *rpmhead = 0;
  int rpmheadsize = 0;
  char *payloadformat;
  FILE *fp;
  unsigned char lead[4096];
  int headerstart, headerend;
  struct stat stb;

  if (nrpms <= 0)
    return;
  repodata = repo_add_repodata(repo, 0);
  for (i = 0; i < nrpms; i++)
    {
      if ((fp = fopen(rpms[i], "r")) == 0)
	{
	  perror(rpms[i]);
	  continue;
	}
      if (fstat(fileno(fp), &stb))
	{
	  perror("stat");
	  continue;
	}
      if (fread(lead, 96 + 16, 1, fp) != 1 || getu32(lead) != 0xedabeedb)
	{
	  fprintf(stderr, "%s: not a rpm\n", rpms[i]);
	  fclose(fp);
	  continue;
	}
      if (lead[78] != 0 || lead[79] != 5)
	{
	  fprintf(stderr, "%s: not a V5 header\n", rpms[i]);
	  fclose(fp);
	  continue;
	}
      if (getu32(lead + 96) != 0x8eade801)
	{
	  fprintf(stderr, "%s: bad signature header\n", rpms[i]);
	  fclose(fp);
	  continue;
	}
      sigcnt = getu32(lead + 96 + 8);
      sigdsize = getu32(lead + 96 + 12);
      if (sigcnt >= 0x4000000 || sigdsize >= 0x40000000)
	{
	  fprintf(stderr, "%s: bad signature header\n", rpms[i]);
	  fclose(fp);
	  continue;
	}
      sigdsize += sigcnt * 16;
      sigdsize = (sigdsize + 7) & ~7;
      headerstart = 96 + 16 + sigdsize;
      while (sigdsize)
	{
	  l = sigdsize > 4096 ? 4096 : sigdsize;
	  if (fread(lead, l, 1, fp) != 1)
	    {
	      fprintf(stderr, "%s: unexpected EOF\n", rpms[i]);
	      fclose(fp);
	      continue;
	    }
	  sigdsize -= l;
	}
      if (fread(lead, 16, 1, fp) != 1)
	{
	  fprintf(stderr, "%s: unexpected EOF\n", rpms[i]);
	  fclose(fp);
	  continue;
	}
      if (getu32(lead) != 0x8eade801)
	{
	  fprintf(stderr, "%s: bad header\n", rpms[i]);
	  fclose(fp);
	  continue;
	}
      sigcnt = getu32(lead + 8);
      sigdsize = getu32(lead + 12);
      if (sigcnt >= 0x4000000 || sigdsize >= 0x40000000)
	{
	  fprintf(stderr, "%s: bad header\n", rpms[i]);
	  fclose(fp);
	  continue;
	}
      l = sigdsize + sigcnt * 16;
      headerend = headerstart + 16 + l;
      if (l > rpmheadsize)
	rpmhead = sat_realloc(rpmhead, sizeof(*rpmhead) + l);
      if (fread(rpmhead->data, l, 1, fp) != 1)
	{
	  fprintf(stderr, "%s: unexpected EOF\n", rpms[i]);
	  fclose(fp);
	  continue;
	}
      rpmhead->cnt = sigcnt;
      rpmhead->dcnt = sigdsize;
      rpmhead->dp = rpmhead->data + rpmhead->cnt * 16;
      if (headexists(rpmhead, TAG_PATCHESNAME))
	{
	  /* this is a patch rpm, ignore */
	  fclose(fp);
	  continue;
	}
      payloadformat = headstring(rpmhead, TAG_PAYLOADFORMAT);
      if (payloadformat && !strcmp(payloadformat, "drpm"))
	{
	  /* this is a delta rpm */
	  fclose(fp);
	  continue;
	}
      fclose(fp);
      s = pool_id2solvable(pool, repo_add_solvable(repo));
      rpm2solv(pool, repo, repodata, s, rpmhead);
      if (repodata)
	{
	  Id handle = (s - pool->solvables) - repodata->start;
	  handle = repodata_get_handle(repodata, handle);
	  add_location(repodata, s, handle, rpms[i]);
	  repodata_set_num(repodata, handle, SOLVABLE_DOWNLOADSIZE, (unsigned int)((stb.st_size + 1023) / 1024));
	  repodata_set_num(repodata, handle, SOLVABLE_HEADEREND, headerend);
	}
    }
  if (rpmhead)
    sat_free(rpmhead);
  if (repodata)
    repodata_internalize(repodata);
}
