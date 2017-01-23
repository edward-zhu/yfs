// lock client interface.

#ifndef lock_client_cache_h

#define lock_client_cache_h

#include <string>
#include <map>
#include <pthread.h>
#include <unordered_set>
#include <queue>

#include "lock_protocol.h"
#include "rpc/rpc.h"
#include "lock_client.h"
#include "lang/verify.h"

// Classes that inherit lock_release_user can override dorelease so that
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 5.
class lock_release_user {
 public:
  virtual void dorelease(lock_protocol::lockid_t) = 0;
  virtual ~lock_release_user() {};
};

class lock_client_cache : public lock_client {
 private:
  class lock_release_user *lu;
  int rlock_port;
  std::string hostname;
  std::string id;

  typedef enum {
    NONE = 0,
    FREE,
    LOCKED,
    ACQUIRING,
    RELEASING
  } lock_stat;

  pthread_mutex_t _m;
  std::map<lock_protocol::lockid_t, lock_stat> _lock_map;

  std::map<lock_protocol::lockid_t, pthread_cond_t> _cond;
  std::map<lock_protocol::lockid_t, pthread_cond_t> _acq_cond;
  std::map<lock_protocol::lockid_t, pthread_cond_t> _rel_cond;
  std::map<lock_protocol::lockid_t, pthread_cond_t> _emp_cond;

  std::map<lock_protocol::lockid_t, std::unordered_set<pthread_t>> _wait_set;

  std::map<lock_protocol::lockid_t, bool> _retry_flag;

  void _wait(lock_protocol::lockid_t, pthread_t);

 public:
  lock_client_cache(std::string xdst, class lock_release_user *l = 0);
  virtual ~lock_client_cache() {};
  lock_protocol::status acquire(lock_protocol::lockid_t);
  lock_protocol::status release(lock_protocol::lockid_t);
  rlock_protocol::status revoke_handler(lock_protocol::lockid_t,
                                        int &);
  rlock_protocol::status retry_handler(lock_protocol::lockid_t,
                                       int &);
};


#endif
