#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>
#include <map>
#include <queue>
#include <unordered_set>

#include <pthread.h>

#include "lock_protocol.h"
#include "rpc/rpc.h"
#include "lock_server.h"


class lock_server_cache {
 private:
  int nacquire;
  std::map<lock_protocol::lockid_t, std::string> _owners;
  std::map<lock_protocol::lockid_t, std::queue<std::string>> _wait_queue;
  std::map<lock_protocol::lockid_t, std::unordered_set<std::string>> _wait_set;

  std::map<lock_protocol::lockid_t, pthread_cond_t> _rev_cond;

  pthread_mutex_t _m;
 public:
  lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  int acquire(lock_protocol::lockid_t, std::string id, int &);
  int release(lock_protocol::lockid_t, std::string id, int &);
};

#endif
