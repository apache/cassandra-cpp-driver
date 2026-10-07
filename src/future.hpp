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

  struct Error : public Allocated {
    Error(CassError code, const String& message)
        : code(code)
        , message(message) {}

    CassError code;
    String message;
  };

  Future(Type type)
      : is_set_(false)
      , type_(type)
      , callback_(NULL) {
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
    return is_set_;
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
    if (!is_set_) {
      internal_set_error(code, message, lock);
      return true;
    }
    return false;
  }

  bool set_callback(Callback callback, void* data);

protected:
  bool is_set() const { return is_set_; }

  void internal_wait(ScopedMutex& lock) {
    /* CASSCPP-6 If we're being called by the thread setting the future just return immediately */
    uv_thread_t me = uv_thread_self();
    const uv_thread_t caller = set_caller_;
    if (set_caller_ && uv_thread_equal(&caller, &me)) {
      return;
    }
    while (!is_set_) {
      uv_cond_wait(&cond_, lock.get());
    }
  }

  bool internal_wait_for(ScopedMutex& lock, uint64_t timeout_us) {
    /* CASSCPP-6 If we're being called by the thread setting the future just return immediately */
    uv_thread_t me = uv_thread_self();
    const uv_thread_t caller = set_caller_;
    if (set_caller_ && uv_thread_equal(&caller, &me)) {
      return true;
    }
    if (!is_set_) {
      if (uv_cond_timedwait(&cond_, lock.get(), timeout_us * 1000) != 0) { // Expects nanos
        return false;
      }
    }
    return is_set_;
  }

  void internal_set(ScopedMutex& lock);

  void internal_set_error(CassError code, const String& message, ScopedMutex& lock) {
    error_.reset(new Error(code, message));
    internal_set(lock);
  }

  uv_mutex_t mutex_;

private:
  bool is_set_;
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
