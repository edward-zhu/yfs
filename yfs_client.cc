// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include "lock_client_cache.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "utils/utils.h"


yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  lc = dynamic_cast<lock_client *>(new lock_client_cache(lock_dst));
}

yfs_client::~yfs_client()
{
  delete ec;
  delete lc;
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;
  // You modify this function for Lab 3
  // - hold and release the file lock
  ScopedNLock l(lc, inum);
  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;
  // You modify this function for Lab 3
  // - hold and release the directory lock
  ScopedNLock l(lc, inum);
  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  return r;
}

bool
yfs_client::find(const std::vector<dirent> &vec,
    const std::string & name, inum * inum) {
  for (auto && ent : vec) {
    if (ent.name == name) {
      if (inum != NULL) {
        (*inum) = ent.inum;
      }
      return true;
    }
  }

  return false;
}

bool
yfs_client::_remove(std::vector<dirent> &vec, const std::string & name, inum * inum) {
  auto it = vec.begin();
  for (; it != vec.end(); it++) {
    if ((*it).name == name) {
      (*inum) = (*it).inum;
      break;
    }
  }

  if (it != vec.end()) {
    vec.erase(it);
    return true;
  }
  else {
    return false;
  }
}

bool
yfs_client::contains(const std::vector<dirent> &vec, const std::string & name) {
  return find(vec, name, NULL);
}

int
yfs_client::lookup(inum parent, const std::string & name, yfs_client::inum * inum) {
  ScopedNLock l(lc, parent);
  printf("[YFS CLI] lookup %016llx name %s\n", parent, name.c_str());
  std::vector<dirent> ents;
  int ret = _readdir(parent, ents);
  if (ret != OK) {
    return ret;
  }

  if(!find(ents, name, inum)) {
    printf("[YFS CLI] lookup failed, name: %s\n", name.c_str());
    return NOENT;
  }

  return OK;
}

int
yfs_client::create(inum inum, const std::string & name, yfs_client::inum parent) {
  ScopedNLock l(lc, parent);
  printf("[YFS CLI] create %s in %016llx\n", name.c_str(), parent);
  std::vector<dirent> ents;
  yfs_client::status ret;
  if ((ret = _readdir(parent, ents)) != OK) {
    return ret;
  }
  if (contains(ents, name)) {
    return EXIST;
  }
  ents.emplace_back(name, inum);

  if (ec->put(inum, "") != extent_protocol::OK) {
    return IOERR;
  }

  if ((ret = _writedir(parent, ents)) != OK) {
    return ret;
  }
  return OK;
}

int
yfs_client::remove(inum parent, const std::string & name) {
  ScopedNLock l(lc, parent);
  std::vector<dirent> ents;
  status ret;
  if ((ret = _readdir(parent, ents)) != OK) {
    return ret;
  }

  printf("[YFS CLI] remove %s in %016llx\n", name.c_str(), parent);

  inum inum;
  bool exist = _remove(ents, name, &inum);

  if (!exist) {
    return NOENT;
  }

  int ec_ret;
  if ((ec_ret = ec->remove(inum)) != extent_protocol::OK) {
    if (ec_ret == extent_protocol::NOENT) {
      return NOENT;
    }
    return IOERR;
  }

  if ((ret = _writedir(parent, ents)) != OK) {
    return ret;
  }

  return OK;
}

int
yfs_client::_readdir(inum inum, std::vector<dirent> &vec) {
  int r = OK;
  printf("[YFS CLI] readdir %016llx\n", inum);
  int ec_ret;
  std::string buf;
  if ((ec_ret = ec->get(inum, buf)) != extent_protocol::OK) {
    if (ec_ret == extent_protocol::NOENT) {
      r = NOENT;
    }
    r = IOERR;
  }

  if (r != OK) return r;
  std::vector<std::string> strvec;
  split(buf, strvec, '|');
  for (auto && entstr : strvec) {
    dirent ent(entstr);
    // printf("[YFS CLI] %s\t%016llx\n", ent.name.c_str(), ent.inum);
    if (ent.valid()) vec.push_back(ent);
    else {
      printf("invalid entry data : %s\n", entstr.c_str());
    }
  }

  printf("[YFS CLI] readdir %016llx finish : size %lu\n", inum, vec.size());
  return r;
}

int
yfs_client::readdir(inum inum, std::vector<dirent> &vec) {
  ScopedNLock l(lc, inum);
  return _readdir(inum, vec);
}

int
yfs_client::_writedir(inum inum, const std::vector<dirent> & vec) {
  int r = OK;
  printf("[YFS CLI] writedir %016llx\n", inum);

  std::stringstream ss;
  for (auto && ent : vec) {
    ss << ent.str();
    ss << '|';
  }

  int ec_ret;
  if ((ec_ret = ec->put(inum, ss.str())) != extent_protocol::OK) {
    if (ec_ret == extent_protocol::NOENT) {
      return NOENT;
    }
    return IOERR;
  }
  return r;
}

int
yfs_client::writedir(inum inum, const std::vector<dirent> & vec) {
  ScopedNLock l(lc, inum);
  return _writedir(inum, vec);
}

int
yfs_client::resize(inum inum, unsigned int size) {
  ScopedNLock l(lc, inum);
  std::string buf;
  int ec_ret;
  if ((ec_ret = ec->get(inum, buf)) != extent_protocol::OK) {
    if (ec_ret == extent_protocol::NOENT) {
      return NOENT;
    }
    return IOERR;
  }

  buf.resize(size);

  if ((ec_ret = ec->put(inum, buf)) != extent_protocol::OK) {
    return IOERR;
  }

  return OK;
}

int
yfs_client::read(inum inum, size_t size, size_t off, std::string & buf) {
  ScopedNLock l(lc, inum);
  std::string orig;
  int ec_ret;
  if ((ec_ret = ec->get(inum, orig)) != extent_protocol::OK) {
    if (ec_ret == extent_protocol::NOENT) return NOENT;
    return IOERR;
  }

  if (off > orig.length()) {
    buf = "";
  }
  else {
    buf = orig.substr(off, size);
  }

  return OK;
}

int
yfs_client::write(inum inum, const char * buf, size_t size, size_t off, size_t * nsize) {
  ScopedNLock l(lc, inum);
  std::string orig;
  int ec_ret;
  if ((ec_ret = ec->get(inum, orig)) != extent_protocol::OK) {
    if (ec_ret == extent_protocol::NOENT) return NOENT;
    return IOERR;
  }
  printf("[YFS CLI] write file original size: %lu, write: %lu, offset: " \
      "%lu\n", orig.length(), size, off);
  if (off > orig.length()) {
    orig.resize(off + size);
  }
  orig.replace(off, size, buf, size);
  if ((ec_ret = ec->put(inum, orig)) != extent_protocol::OK) {
    return IOERR;
  }
  printf("[YFS CLI] write file succeed! size : %lu.\n", orig.length());
  (*nsize) = size;
  return OK;
}
