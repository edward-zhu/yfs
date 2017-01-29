#ifndef __SCOPED_LOCK__
#define __SCOPED_LOCK__

#include <pthread.h>
#include <iostream>
#include "lang/verify.h"
struct ScopedLock {
	private:
		pthread_mutex_t *m_;
	public:
		ScopedLock(pthread_mutex_t *m): m_(m) {
      int ret = pthread_mutex_lock(m_);
      if (ret != 0) printf("scopedlock returns %d\n", ret);
			VERIFY(ret==0);
		}
		~ScopedLock() {
			VERIFY(pthread_mutex_unlock(m_)==0);
		}
};
#endif  /*__SCOPED_LOCK__*/
