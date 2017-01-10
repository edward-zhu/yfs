// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc/rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"
#include "rpc/slock.h"


lock_client_cache::lock_client_cache(std::string xdst,
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu), _lock_map(), _cond(), _acq_cond(),
    _rel_cond(), _emp_cond(), _wait_set(), _wait_queue()
{
  rpcs *rlsrpc = new rpcs(0);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);

  const char *hname;
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlsrpc->port();
  id = host.str();
  VERIFY(pthread_mutex_init(&_m, NULL) == 0);
}

void
lock_client_cache::_wait(lock_protocol::lockid_t lid, pthread_t tid) {
  if (_wait_set[lid].count(tid) == 0) {
    _wait_set[lid].insert(tid);
    _wait_queue[lid].push(tid);
  }
  // tprintf("[LOCK CLI] %s thread %lu enqueued lock %llu, stat: %u, queue size: %lu\n",
  //    id.c_str(), tid, lid, _lock_map[lid], _wait_queue[lid].size());
  // tprintf("[LOCK CLI] queue front: %lu\n", _wait_queue[lid].front());
  lock_stat s = _lock_map[lid];
  while (!(s == FREE || s == LOCKED) ||  _wait_queue[lid].front() != tid) {
    pthread_cond_wait(&_cond[lid], &_m);
    s = _lock_map[lid];
    // tprintf("[LOCK CLI] %s thread %lu woke up. front: %lu stat: %u\n",
    //    id.c_str(), tid, _wait_queue[lid].front(), _lock_map[lid]);
  }
  // tprintf("[LOCK CLI] %s thread %lu granted lock %llu, stat:%u, queue size: %lu\n",
  //    id.c_str(),tid, lid, _lock_map[lid], _wait_queue[lid].size());
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  int ret = lock_protocol::OK;
  pthread_t self = pthread_self();


  // while (true) {
    {
      ScopedLock l(&_m);
      // tprintf("[LOCK CLI] %s thread %lu acquiring lock %llu, stat: %u\n",
      //    id.c_str(), self, lid, _lock_map[lid]);
      if (_lock_map.find(lid) == _lock_map.end()) {
        _lock_map[lid] = NONE;
        _cond[lid] = PTHREAD_COND_INITIALIZER;
        _acq_cond[lid] = PTHREAD_COND_INITIALIZER;
        _rel_cond[lid] = PTHREAD_COND_INITIALIZER;
        _emp_cond[lid] = PTHREAD_COND_INITIALIZER;
      }
      lock_stat stat = _lock_map[lid];
      if (stat == FREE) {
        _lock_map[lid] = LOCKED;
        _wait(lid, self);
        return ret;
      }
      else if (stat == LOCKED) {
        _wait(lid, self);
        return ret;
      }
      else if (stat == RELEASING) {
        if (_wait_set[lid].count(self) == 0) {
          _wait_set[lid].insert(self);
          _wait_queue[lid].push(self);
        }
        lock_stat s = _lock_map[lid];
        while (!(s == FREE || s == LOCKED || s == NONE) ||  _wait_queue[lid].front() != self) {
          pthread_cond_wait(&_cond[lid], &_m);
          s = _lock_map[lid];
          // tprintf("[LOCK CLI] %s thread %lu woke up. front: %lu stat: %u\n",
          //    id.c_str(), self, _wait_queue[lid].front(), _lock_map[lid]);
        }
        if (s != NONE) return ret;
      }
      else if (stat == ACQUIRING) {
        _wait(lid, self);
        return ret;
      }
      VERIFY(_lock_map[lid] == NONE);
      _retry_flag[lid] = false;
      _lock_map[lid] = ACQUIRING;
      if (_wait_set[lid].count(self) == 0) {
        _wait_set[lid].insert(self);
        _wait_queue[lid].push(self);
      }
      // tprintf("[LOCK CLI] %s thread %lu acquiring lock %llu from server queue size: %lu\n",
      //    id.c_str(), self, lid, _wait_queue[lid].size());
    }
    while (true) {
      lock_protocol::status r, rret;
      rret = cl->call(lock_protocol::acquire, lid, id, r);
      // tprintf("[LOCK CLI] %s acquire(%llu) returned with %d\n", id.c_str(), lid, r);
      {
        ScopedLock l(&_m);
        if (r == lock_protocol::OK) {
          // tprintf("[LOCK CLI] %s thread %lu get lock %llu\n", id.c_str(), self, lid);
          if (_lock_map[lid] != RELEASING) {
            _lock_map[lid] = LOCKED;
          }
          if (!_wait_queue[lid].empty()) {
            // tprintf("[LOCK CLI] %s thread %lu broadcast for lock %llu\n",
            //    id.c_str(),self, lid);
            pthread_cond_broadcast(&_cond[lid]);
          }
          lock_stat s = _lock_map[lid];
          while (!(s == FREE || s == LOCKED) ||  _wait_queue[lid].front() != self) {
            pthread_cond_wait(&_cond[lid], &_m);
            s = _lock_map[lid];
          }
          // tprintf("[LOCK CLI] %s thread %lu granted lock %llu, stat:%u, queue size: %lu\n",
          //    id.c_str(), self, lid, _lock_map[lid], _wait_queue[lid].size());
          return ret;
        }
        else if (r == lock_protocol::RETRY) {
          while (!_retry_flag[lid]) {
            pthread_cond_wait(&_acq_cond[lid], &_m);
          }
        }
      }
    }
  // }

  return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
  ScopedLock l(&_m);
  pthread_t self = pthread_self();
  // tprintf("[LOCK CLI] %s thread %lu released lock %llu, stat: %u, queue size: %lu\n",
  //    id.c_str(), self, lid, _lock_map[lid], _wait_queue[lid].size());
  if (self == _wait_queue[lid].front()) {
    _wait_queue[lid].pop();
    _wait_set[lid].erase(self);
  }
  // tprintf("[LOCK CLI] queue front: %lu\n", _wait_queue[lid].empty() ? 0 :_wait_queue[lid].front());
  // tprintf("[LOCK CLI] new queue size: %lu\n", _wait_queue[lid].size());
  if (_wait_queue[lid].empty()) {
    if (_lock_map[lid] != RELEASING)_lock_map[lid] = FREE;
    // tprintf("[LOCK CLI] %s thread %lu signaling empty condition lock %llu\n",
    //    id.c_str(), self, lid);
    pthread_cond_signal(&_emp_cond[lid]);
  }
  else {
    // tprintf("[LOCK CLI] %s thread %lu signaling next thread waiting for lock %llu\n",
    //    id.c_str(), self, lid);
    pthread_cond_broadcast(&_cond[lid]);
  }
  return lock_protocol::OK;

}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid,
                                  int & r)
{
  // tprintf("[LOCK CLI] %s got revoke request for lock %llu.\n", id.c_str(), lid);
  int ret = rlock_protocol::OK;
  r = rlock_protocol::OK;
  {
    ScopedLock l(&_m);
    if (_lock_map[lid] == NONE) {
      // tprintf("[LOCK CLI] %s fake revoke for lock %llu\n", id.c_str(), lid);
      return ret;
    }
    while (!_wait_queue[lid].empty()) {
      pthread_cond_wait(&_emp_cond[lid], &_m);
    }

    _lock_map[lid] = RELEASING;
  }
  int rr;
  // tprintf("[LOCK CLI] %s is giving up lock %llu.\n", id.c_str(), lid);
  cl->call(lock_protocol::release, lid, id, rr);
  {
    ScopedLock l(&_m);
    _lock_map[lid] = NONE;
    pthread_cond_broadcast(&_cond[lid]);
    // tprintf("[LOCK CLI] %s has given up lock %llu.\n", id.c_str(), lid);
  }
  return ret;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid,
                                 int & r)
{
  int ret = rlock_protocol::OK;
  r = rlock_protocol::OK;
  ScopedLock l(&_m);
  pthread_cond_signal(&_acq_cond[lid]);
  _retry_flag[lid] = true;
  return ret;
}



