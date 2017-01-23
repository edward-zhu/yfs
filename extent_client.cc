// RPC stubs for clients to talk to extent_server

#include "extent_client.h"

#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#include "lang/verify.h"
#include "rpc/slock.h"
#include "tprintf.h"

// The calls assume that the caller holds a lock on the extent

extent_client::extent_client(std::string dst) : _cache()
{
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }

  VERIFY(pthread_mutex_init(&_m, NULL) == 0);
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  // if the resource is cached
  {
    ScopedLock l(&_m);
    if (_cache.count(eid)) {
      if (_cache[eid].removed) {
        return extent_protocol::NOENT;
      }
      else if (_cache[eid].buf != NULL) {
        tprintf("[EXT CLI] read from cache %llu, sz: %lu\n", eid, _cache[eid].buf->size());
        buf = *(_cache[eid].buf);
        return ret;
      }
    }
  }
  tprintf("[EXT CLI] missed cache %llu\n", eid);
  ret = cl->call(extent_protocol::get, eid, buf);
  {
    ScopedLock l(&_m);
    // load cache
    _cache[eid].buf = new std::string(buf);
    tprintf("[EXT CLI] write cache %llu. sz: %lu\n", eid, _cache[eid].buf->size());
    return ret;
  }
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid,
		       extent_protocol::attr &attr)
{
  extent_protocol::status ret = extent_protocol::OK;
  {
    ScopedLock l(&_m);
    if (_cache.count(eid)) {
      if (_cache[eid].removed) {
        return extent_protocol::NOENT;
      }
      else if (_cache[eid].attr) {
        attr = *(_cache[eid].attr);
        return ret;
      }
    }
  }
  ret = cl->call(extent_protocol::getattr, eid, attr);
  {
    ScopedLock l(&_m);
    // load cache
    _cache[eid].attr = new extent_protocol::attr(attr);
    return ret;
  }
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  ScopedLock l(&_m);
  // int r;
  // ret = cl->call(extent_protocol::put, eid, buf, r);
  _cache[eid].removed = false;
  _cache[eid].dirty = true;
  if (!_cache[eid].buf) {
    tprintf("[EXT CLI] new string\n");
    _cache[eid].buf = new std::string();
  }
  _cache[eid].buf->assign(buf);

  tprintf("[EXT CLI] buf size: %lu\n", buf.size());

  time_t now = time(NULL);

  if (_cache.count(eid) == 0 || _cache[eid].attr == NULL) {
    // no attr
    _cache[eid].attr = new extent_protocol::attr();
    _cache[eid].attr->atime = now;
  }
  _cache[eid].attr->mtime = now;
  _cache[eid].attr->ctime = now;
  _cache[eid].attr->size = buf.size();

  tprintf("[EXT CLI] write cache %llu sz: %lu\n", eid, _cache[eid].buf->size());

  return ret;
}

void
extent_client::_clean_cache(extent_protocol::extentid_t eid)
{
  _cache.erase(eid);
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  extent_protocol::status ret = extent_protocol::OK;
  ScopedLock l(&_m);
  _clean_cache(eid);
  // mark resource is removed
  _cache[eid].removed = true;
  return ret;
}

extent_protocol::status
extent_client::flush(extent_protocol::extentid_t eid)
{
  extent_protocol::status ret = extent_protocol::OK;
  bool removed = false, dirty = false;
  std::string buf;
  {
    ScopedLock l(&_m);
    if (_cache.count(eid) == 0) {
      return extent_protocol::NOENT;
    }
    removed = _cache[eid].removed;
    dirty = _cache[eid].dirty;
    tprintf("[EXT CLI] flushing %llu removed:%d dirty:%d\n", eid, removed, dirty);
    if (!removed && !dirty) {
      _clean_cache(eid);
      return ret;
    }
    if (dirty) {
      buf = *(_cache[eid].buf);
      tprintf("[EXT CLI] buf: \n===\n%s\n===\n", _cache[eid].buf->c_str());
    }
  }
  int r;
  if (removed) {
    ret = cl->call(extent_protocol::remove, eid, r);
  }
  // if the resource is modified, write back to the server
  else if (dirty) {
    ret = cl->call(extent_protocol::put, eid, buf, r);
  }
  {
    ScopedLock l(&_m);
    _clean_cache(eid);
    if (r != extent_protocol::OK) {
      return r;
    }
    return ret;
  }
}


