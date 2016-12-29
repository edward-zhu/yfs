#ifndef yfs_client_h
#define yfs_client_h

#include <string>
//#include "yfs_protocol.h"
#include "extent_client.h"
#include <vector>

#include "lock_protocol.h"
#include "lock_client.h"

class yfs_client {
  extent_client *ec;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, EXIST };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    yfs_client::inum inum;

    dirent() {}

    dirent(const std::string & from) {
      size_t pos = from.find(':');
      if (pos == from.npos || pos == from.size() - 1) return;
      name = from.substr(0, pos);
      inum = stoul(from.substr(pos + 1, from.npos));
    }

    dirent(const std::string & name, yfs_client::inum inum) {
      this->name = name;
      this->inum = inum;
    }

    const std::string str() const {
      return name + ":" + std::to_string(inum);
    }

    bool valid() {
      return !name.empty();
    }
  };

 private:
  static std::string filename(inum);
  static inum n2i(std::string);
  static bool find(const std::vector<dirent> &, const std::string &, inum *);
  static bool contains(const std::vector<dirent> &, const std::string &);
  static bool _remove(std::vector<dirent> &, const std::string &, inum *);
 public:

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);

  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);
  int lookup(inum, const std::string &, inum *);
  int create(inum, const std::string &, inum);
  int remove(inum, const std::string &);
  int readdir(inum, std::vector<dirent> &);
  int writedir(inum, const std::vector<dirent> &);
  int resize(inum, unsigned int);
  int write(inum, const char * buf, size_t, size_t, size_t *);
  int read(inum, size_t, size_t, std::string &);
};

#endif
