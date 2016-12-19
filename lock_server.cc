// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>

lock_server::lock_server() :
  nacquire (0), _lock_map(), _cond_map()
{
  VERIFY(pthread_mutex_init(&_m, 0) == 0);
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int & r)
{
  {
    ScopedLock ml(&(_m));
    if (_lock_map.find(lid) == _lock_map.end() || _lock_map[lid] == lock_stat::FREE) {
      _lock_map[lid] = lock_stat::LOCKED;
      VERIFY(pthread_cond_init(&_cond_map[lid], 0) == 0);
      r = nacquire;
      return lock_protocol::OK;
    }
    while (_lock_map[lid] == lock_stat::LOCKED) {
      pthread_cond_wait(&_cond_map[lid], &_m);
    }

    _lock_map[lid] = lock_stat::LOCKED;

    return lock_protocol::OK;
  }
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int & r)
{
  {
    ScopedLock ml(&(_m));
    VERIFY(_lock_map.find(lid) != _lock_map.end());
    if (_lock_map[lid] == lock_stat::LOCKED) {
      _lock_map[lid] = lock_stat::FREE;
      pthread_cond_signal(&_cond_map[lid]);
    }
    r = nacquire;
    return lock_protocol::OK;
  }
}



