#ifndef _LBFS_DB_
#define _LBFS_DB_

#include "vec.h"
#include "async.h"
#include "nfs3_prot.h"
#ifdef HAVE_DB3_H
#include <db3.h>
#else /* !HAVE_DB3_H */
#include <db.h>
#endif /* !HAVE_DB3_H */
#include "rpctypes.h"

// we keep P(t), x, and K the same for whole file system, so two equivalent
// files would have the same breakmarks. for string A, fingerprint of A is
//
//   f(A) = A(t) mod P(t) 
//
// we create breakmarks when 
//
//   f(A) mod K = x
//
// if we use K = 8192, the average chunk size is 8k. we allow multiple K
// values so we can do multi-level chunking.

#define FINGERPRINT_PT     0xbfe6b8a5bf378d83LL
#define BREAKMARK_VALUE    0x78
#define NUM_CHUNK_SIZES    4
#define CHUNK_SIZES(i) \
  (i == 0 ? 8192 : \
   (i == 1 ? 32768 : \
    (i == 2 ? 131072 : \
     (i == 3 ? 524288 : 0))))

#define FP_DB "fp.db"
#define FH_DB "fh.db"

class lbfs_chunk_loc {

private:
  unsigned char _fh[NFS3_FHSIZE];
  unsigned _fhsize;
  off_t _pos;
  size_t _count;
 
public:
  lbfs_chunk_loc() {
    _fhsize = 0;
  }
  
  lbfs_chunk_loc(off_t p, size_t c) {
    _fhsize = 0;
    _pos = p;
    _count = c;
  }

  lbfs_chunk_loc& operator= (const lbfs_chunk_loc &l) {
    _fhsize = l._fhsize;
    if (_fhsize > 0) memmove(_fh, l._fh, _fhsize);
    _pos = l._pos;
    _count = l._count;
    return *this;
  }
  
  void set_fh(const nfs_fh3 &f) {
    memmove(_fh, f.data.base(), f.data.size());
    _fhsize = f.data.size();
  }

  int get_fh(nfs_fh3 &f) const {
    if (_fhsize > 0) {
      char *data = New char[_fhsize];
      memmove(&data[0], _fh, _fhsize);
      f.data.set(data, _fhsize, freemode::DELETE);
      return 0;
    }
    else
      return -1;
  }

  off_t pos() const 		{ return _pos; }
  void set_pos(off_t p) 	{ _pos = p; }
  
  size_t count() const 		{ return _count; }
  void set_count(size_t c) 	{ _count = c; }
};

struct lbfs_chunk {
  lbfs_chunk_loc loc;
  u_int64_t fingerprint;
  lbfs_chunk(off_t p, size_t s, u_int64_t fp) : loc(p, s), fingerprint(fp) {}
};

class lbfs_db {
private:
  DB *_fp_dbp;

public:

  class chunk_iterator {
    friend lbfs_db;
  private:
    DBC* _cursor;
    chunk_iterator(DBC *c);

  public:
    ~chunk_iterator();
    operator bool() const { return _cursor != 0; }

    int del();

    // get current entry
    int get(lbfs_chunk_loc *c);
    // increment iterator, return entry
    int next(lbfs_chunk_loc *c);
  };

  lbfs_db();
  ~lbfs_db();

  // open db, returns db3 errnos
  int open(u_int32_t db3_flags = DB_CREATE); 

  // open and truncate existing db
  int open_and_truncate();

  // creates a chunk iterator and copies a ptr to it into the memory
  // referenced by iterp (callee responsible for freeing iterp). additionally,
  // iterator is moved under the data for the given key, if any.
  int get_chunk_iterator(u_int64_t fingerprint, chunk_iterator **iterp);

  // add a chunk to the database, returns db3 errnos
  int add_chunk(u_int64_t fingerprint, lbfs_chunk_loc *c);

  // sync data to stable storage
  int sync();
};

inline
lbfs_db::chunk_iterator::chunk_iterator(DBC *c)
{
  _cursor = c;
}

inline
lbfs_db::chunk_iterator::~chunk_iterator()
{ 
  if (_cursor) 
    _cursor->c_close(_cursor);
  _cursor = 0L;
}

inline int
lbfs_db::chunk_iterator::del()
{
  return _cursor->c_del(_cursor, 0);
}

inline int
lbfs_db::chunk_iterator::get(lbfs_chunk_loc *c)
{
  assert(_cursor);
  DBT key;
  DBT data;
  memset(&key, 0, sizeof(key));
  memset(&data, 0, sizeof(data));
  int ret = _cursor->c_get(_cursor, &key, &data, DB_CURRENT);
  if (ret == 0 && data.data) {
    lbfs_chunk_loc *loc = reinterpret_cast<lbfs_chunk_loc*>(data.data);
    *c = *loc;
  }
  return ret;
}

inline int
lbfs_db::chunk_iterator::next(lbfs_chunk_loc *c)
{
  assert(_cursor);
  DBT key;
  DBT data;
  memset(&key, 0, sizeof(key));
  memset(&data, 0, sizeof(data));
  int ret = _cursor->c_get(_cursor, &key, &data, DB_NEXT_DUP);
  if (ret == 0) {
    if (data.data) 
      *c = *(reinterpret_cast<lbfs_chunk_loc*>(data.data));
    else assert(0);
  }
  return ret;
}

inline int
lbfs_db::sync()
{
  return _fp_dbp->sync(_fp_dbp,0);
}



#endif _LBFS_DB_

