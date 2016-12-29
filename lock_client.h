// lock client interface.

#ifndef lock_client_h
#define lock_client_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include <vector>

// Client interface to the lock server
class lock_client {
 protected:
  rpcc *cl;
 public:
  lock_client(std::string d);
  virtual ~lock_client() {};
  virtual lock_protocol::status acquire(lock_protocol::lockid_t);
  virtual lock_protocol::status release(lock_protocol::lockid_t);
  virtual lock_protocol::status stat(lock_protocol::lockid_t);
};

class ScopedNLock {
 private:
  lock_client * lc;
  lock_protocol::lockid_t lid;
 public:
  ScopedNLock(lock_client * lc, lock_protocol::lockid_t lid) {
    this->lc = lc;
    this->lid = lid;
    printf("[LOCK SRV] acquire %llu\n", lid);
    lc->acquire(lid);
  }

  ~ScopedNLock() {
    printf("[LOCK SRV] release %llu\n", lid);
    lc->release(lid);
  }
};


#endif
