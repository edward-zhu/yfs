// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "rpc/slock.h"
#include "lang/verify.h"

extent_server::extent_server() : ext_map_(), nacquire(0)
{
  unsigned int now = time(NULL);
  ext_map_[1] = {
    {0, now, now, now},
    ""
  };
  VERIFY(pthread_mutex_init(&_m, 0) == 0);
}


int extent_server::put(extent_protocol::extentid_t id, std::string buf, int & r)
{
  ScopedLock m(&_m);
  unsigned int now = time(NULL);
  auto it = ext_map_.find(id);
  r = nacquire;
  printf("[EXT SERVER] put id %016llx\n", id);
  // If there is no such node
  if (it == ext_map_.end()) {
    node & n = ext_map_[id];
    n.attr = {now, now, now, static_cast<unsigned int>(buf.size())};
    n.buf = buf;
    return extent_protocol::OK;
  }

  // if this node exist
  printf("EXT SERVER] replace: %s\n", buf.c_str());
  node & n = (*it).second;
  n.attr.ctime = now;
  n.attr.mtime = now;
  n.buf = buf;
  n.attr.size = static_cast<unsigned int>(buf.size());

  return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
  ScopedLock m(&_m);
  auto it = ext_map_.find(id);
  printf("[EXT SERVER] get id %016llx\n", id);
  node & n = (*it).second;
  n.attr.atime = time(NULL);
  buf = n.buf;
  printf("[EXT SERVER] contains: %s\n", buf.c_str());
  return extent_protocol::OK;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  // You replace this with a real implementation. We send a phony response
  // for now because it's difficult to get FUSE to do anything (including
  // unmount) if getattr fails.

  ScopedLock m(&_m);
  auto it = ext_map_.find(id);

  a = it->second.attr;
  return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t id, int & r)
{
  ScopedLock m(&_m);
  auto it = ext_map_.find(id);
  r = nacquire;
  if (it == ext_map_.end()) {
    r = extent_protocol::NOENT;
  }
  ext_map_.erase(it);
  return extent_protocol::OK;
}

