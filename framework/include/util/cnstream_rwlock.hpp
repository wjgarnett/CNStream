/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *************************************************************************/
#ifndef CNSTREAM_RWLOCK_H_
#define CNSTREAM_RWLOCK_H_

#include <pthread.h>  // for pthread_rwlock_t
#include <memory>
#include <utility>

/**
 * @note
 * 这里的读写锁封装充分体现了编程中的RAII原则，同时简化了用户调用。
 * 
 * RAII原则是C++中的一种编程惯例:（Resource Acquisition Is Initialization）
 *  1.资源获取：在对象的构造函数中获取资源（内存、文件、锁等）
 *  2.资源释放：在对象的析构函数中释放资源
 */

namespace cnstream {

// FIXME
/**
 * 定义读写锁类，并提供了获取读锁、写锁以及释放锁的方法
 */
class RwLock {
 public:
  RwLock() { pthread_rwlock_init(&rwlock, NULL); }
  ~RwLock() { pthread_rwlock_destroy(&rwlock); }
  void wrlock() { pthread_rwlock_wrlock(&rwlock); } // 请求获取写锁，若此时其他线程持有读锁或写锁，当前线程将被阻塞。
  void rdlock() { pthread_rwlock_rdlock(&rwlock); } // 请求获取读锁，若此时其他线程持有写锁，当前线程将被阻塞。
  void unlock() { pthread_rwlock_unlock(&rwlock); }

 private:
  pthread_rwlock_t rwlock;
};

/**
 * 定义写锁类，并用析构函数自动管理资源释放问题，简化调用
 */
class RwLockWriteGuard {
 public:
  explicit RwLockWriteGuard(RwLock& lock) : lock_(lock) { lock_.wrlock(); }
  ~RwLockWriteGuard() { lock_.unlock(); }

 private:
  RwLock& lock_;
}; 

/**
 * 定义读锁类，并用析构函数自动管理资源释放问题，简化调用
 */
class RwLockReadGuard {
 public:
  explicit RwLockReadGuard(RwLock& lock) : lock_(lock) { lock_.rdlock(); }
  ~RwLockReadGuard() { lock_.unlock(); }

 private:
  RwLock& lock_;
};

} /* namespace cnstream */

#endif /* CNSTREAM_RWLOCK_H_ */
