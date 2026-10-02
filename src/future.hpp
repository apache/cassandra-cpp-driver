/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef DATASTAX_INTERNAL_FUTURE_HPP
#define DATASTAX_INTERNAL_FUTURE_HPP

#include "atomic.hpp"
#include "cassandra.h"
#include "external.hpp"
#include "host.hpp"
#include "macros.hpp"
#include "ref_counted.hpp"
#include "scoped_lock.hpp"
#include "scoped_ptr.hpp"
#include "string.hpp"

#include <assert.h>
#include <uv.h>

namespace datastax { namespace internal { namespace core {

struct Error;

class Future : public RefCounted<Future> {
public:
  typedef SharedRefPtr<Future> Ptr;
  typedef void (*Callback)(CassFuture*, void*);

  enum Type { FUTURE_TYPE_GENERIC, FUTURE_TYPE_SESSION, FUTURE_TYPE_RESPONSE };

  enum SetState { UNSTARTED, STARTED, FINISHED };

  struct Error : public Allocated {
    Error(CassError code, const String& message)
        : code(code)
        , message(message) {}

    CassError code;
    String message;
  };

  Future(Type type)
      : state_(UNSTARTED)
      , type_(type)
      , callback_(NULL)
      , set_caller_(0) {
    uv_mutex_init(&mutex_);
    uv_cond_init(&cond_);
  }

  virtual ~Future() {
    uv_mutex_destroy(&mutex_);
    uv_cond_destroy(&cond_);
  }

  Type type() const { return type_; }

  bool ready() {
    ScopedMutex lock(&mutex_);
    return is_set();
  }

  virtual void wait() {
    ScopedMutex lock(&mutex_);
    internal_wait(lock);
  }

  virtual bool wait_for(uint64_t timeout_us) {
    ScopedMutex lock(&mutex_);
    return internal_wait_for(lock, timeout_us);
  }

  Error* error() {
    ScopedMutex lock(&mutex_);
    internal_wait(lock);
    return error_.get();
  }

  void set() {
    ScopedMutex lock(&mutex_);
    internal_set(lock);
  }

  bool set_error(CassError code, const String& message) {
    ScopedMutex lock(&mutex_);
    if (is_unstarted()) {
      internal_set_error(code, message, lock);
      return true;
    }
    return false;
  }

  bool set_callback(Callback callback, void* data);

protected:

  bool is_unstarted() const { return state_ == UNSTARTED; }

  bool is_set() const { return state_ == FINISHED; }

  bool is_set_caller() {
    /* CASSCPP-6 If we're being called by the thread setting the future just return immediately */
    uv_thread_t me = uv_thread_self();
    const uv_thread_t caller = set_caller_;
    return set_caller_ && uv_thread_equal(&caller, &me);
  }

  void internal_wait(ScopedMutex& lock) {
    if (is_set_caller()) {
      return;
    }
    while (!is_set()) {
      uv_cond_wait(&cond_, lock.get());
    }
  }

  bool internal_wait_for(ScopedMutex& lock, uint64_t timeout_us) {
    if (is_set_caller()) {
      return false;
    }
    if (!is_set()) {
      if (uv_cond_timedwait(&cond_, lock.get(), timeout_us * 1000) != 0) { // Expects nanos
        return false;
      }
    }
    return is_set();
  }

  void internal_set(ScopedMutex& lock);

  void internal_set_error(CassError code, const String& message, ScopedMutex& lock) {

    /* If the future has already been set (or another thread is already working on it) return immediately.

       We do not change the value of state_ to started because internal_set() performs a similar check and
       doing so here would lead to an early return from that function.  We'll let the subsequent call to
       internal_set() take care of any state updates. */
    if (state_ != UNSTARTED) {
      return;
    }

    error_.reset(new Error(code, message));
    internal_set(lock);
  }

  uv_mutex_t mutex_;

private:
  SetState state_;
  uv_cond_t cond_;
  Type type_;
  ScopedPtr<Error> error_;
  Callback callback_;
  void* data_;
  uv_thread_t set_caller_;

private:
  DISALLOW_COPY_AND_ASSIGN(Future);
};

}}} // namespace datastax::internal::core

EXTERNAL_TYPE(datastax::internal::core::Future, CassFuture)

#endif
