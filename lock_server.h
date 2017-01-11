// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include <string>
#include <map>
#include <pthread.h>
#include "lang/verify.h"
#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc/slock.h"
#include "rpc/rpc.h"

class lock_server {
 typedef enum {
  FREE,
  LOCKED
 } lock_stat;

 protected:
  int nacquire;
  pthread_mutex_t _m;
  std::map<lock_protocol::lockid_t, lock_stat> _lock_map;
  std::map<lock_protocol::lockid_t, pthread_cond_t> _cond_map;

 public:
  lock_server();
  ~lock_server() {};
  lock_protocol::status stat(int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status acquire(int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status release(int clt, lock_protocol::lockid_t lid, int &);
};

#endif







