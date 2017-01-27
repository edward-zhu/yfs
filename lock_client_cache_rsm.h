// lock client interface.

#ifndef lock_client_cache_rsm_h

#define lock_client_cache_rsm_h

#include <string>
#include <unordered_map>
#include <pthread.h>
#include <unordered_set>
#include <queue>

#include "lock_protocol.h"
#include "rpc/rpc.h"
#include "lock_client.h"
#include "lang/verify.h"

#include "rsm_client.h"

// Classes that inherit lock_release_user can override dorelease so that
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 5.
class lock_release_user {
 public:
  virtual void dorelease(lock_protocol::lockid_t) = 0;
  virtual ~lock_release_user() {};
};


class lock_client_cache_rsm;

// Clients that caches locks.  The server can revoke locks using
// lock_revoke_server.
class lock_client_cache_rsm : public lock_client {
 private:
  rsm_client *rsmc;
  class lock_release_user *lu;
  int rlock_port;
  std::string hostname;
  std::string id;
  lock_protocol::xid_t xid;

  typedef enum {
    NONE,
    FREE,
    LOCKED,
    ACQUIRING,
    RELEASING
  } lock_stat;

  pthread_mutex_t _m;
  std::unordered_map<lock_protocol::lockid_t, lock_stat> _stat;

  std::unordered_map<lock_protocol::lockid_t, pthread_cond_t> _lc, _ac, _ec;
  std::unordered_map<lock_protocol::lockid_t, std::unordered_set<pthread_t>> _ws;
  std::unordered_map<lock_protocol::lockid_t, bool> _retry_flag;

  void _wait(lock_protocol::lockid_t, pthread_t);

 public:
  static int last_port;
  lock_client_cache_rsm(std::string xdst, class lock_release_user *l = 0);
  virtual ~lock_client_cache_rsm() {};
  lock_protocol::status acquire(lock_protocol::lockid_t);
  virtual lock_protocol::status release(lock_protocol::lockid_t);
  void releaser();
  rlock_protocol::status revoke_handler(lock_protocol::lockid_t,
				        lock_protocol::xid_t, int &);
  rlock_protocol::status retry_handler(lock_protocol::lockid_t,
				       lock_protocol::xid_t, int &);
};


#endif
