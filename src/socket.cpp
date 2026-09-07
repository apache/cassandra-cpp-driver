/*
  Copyright (c) 2014-2017 DataStax

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "socket.hpp"

#include "logger.hpp"

#define SSL_READ_SIZE 8192
#define SSL_WRITE_SIZE 8192
#define SSL_ENCRYPTED_BUFS_COUNT 16

#define MAX_BUFFER_REUSE_NO 8
#define BUFFER_REUSE_SIZE 64 * 1024

using namespace datastax::internal;
using namespace datastax::internal::core;

typedef Vector<uv_buf_t> UvBufVec;

/**
 * A basic socket write handler.
 */
class SocketWrite : public SocketWriteBase {
public:
  SocketWrite(Socket* socket)
      : SocketWriteBase(socket) {}

  size_t flush();
};

size_t SocketWrite::flush() {
  size_t total = 0;
  if (!is_flushed_ && !buffers_.empty()) {
    UvBufVec bufs;

    bufs.reserve(buffers_.size());

    for (BufferVec::const_iterator it = buffers_.begin(), end = buffers_.end(); it != end; ++it) {
      total += it->size();
      bufs.push_back(uv_buf_init(const_cast<char*>(it->data()), it->size()));
    }

    is_flushed_ = true;
    uv_stream_t* sock_stream = reinterpret_cast<uv_stream_t*>(tcp());
    uv_write(&req_, sock_stream, bufs.data(), bufs.size(), SocketWrite::on_write);
  }
  return total;
}

SocketHandler::~SocketHandler() {
  while (!buffer_reuse_list_.empty()) {
    uv_buf_t buf = buffer_reuse_list_.top();
    Memory::free(buf.base);
    buffer_reuse_list_.pop();
  }
}

SocketWriteBase* SocketHandler::new_pending_write(Socket* socket) {
  return new SocketWrite(socket);
}

void SocketHandler::alloc_buffer(size_t suggested_size, uv_buf_t* buf) {
  if (suggested_size <= BUFFER_REUSE_SIZE) {
    if (!buffer_reuse_list_.empty()) {
      *buf = buffer_reuse_list_.top();
      buffer_reuse_list_.pop();
    } else {
      *buf = uv_buf_init(reinterpret_cast<char*>(Memory::malloc(BUFFER_REUSE_SIZE)),
                         BUFFER_REUSE_SIZE);
    }
  } else {
    *buf = uv_buf_init(reinterpret_cast<char*>(Memory::malloc(suggested_size)), suggested_size);
  }
}

void SocketHandler::free_buffer(const uv_buf_t* buf) {
  if (buf->len == BUFFER_REUSE_SIZE && buffer_reuse_list_.size() < MAX_BUFFER_REUSE_NO) {
    buffer_reuse_list_.push(*buf);
    return;
  }
  Memory::free(buf->base);
}

/**
 * A SSL socket write handler.
 */
class SslSocketWrite : public SocketWriteBase {
public:
  SslSocketWrite(Socket* socket, SslSession* ssl_session)
      : SocketWriteBase(socket)
      , ssl_session_(ssl_session)
      , encrypted_size_(0) {}

  virtual size_t flush();

private:
  void encrypt();

  static void on_write(uv_write_t* req, int status);

private:
  SslSession* ssl_session_;
  size_t encrypted_size_;
};

size_t SslSocketWrite::flush() {
  size_t total = 0;
  if (!is_flushed_ && !buffers_.empty()) {
    rb::RingBuffer::Position prev_pos = ssl_session_->outgoing().write_position();

    encrypt();

    SmallVector<uv_buf_t, SSL_ENCRYPTED_BUFS_COUNT> bufs;
    total = encrypted_size_ = ssl_session_->outgoing().peek_multiple(prev_pos, &bufs);

    LOG_TRACE("Sending %u encrypted bytes", static_cast<unsigned int>(encrypted_size_));

    uv_stream_t* sock_stream = reinterpret_cast<uv_stream_t*>(tcp());
    uv_write(&req_, sock_stream, bufs.data(), bufs.size(), SslSocketWrite::on_write);

    is_flushed_ = true;
  }
  return total;
}

void SslSocketWrite::encrypt() {
  char buf[SSL_WRITE_SIZE];

  size_t copied = 0;
  size_t offset = 0;
  size_t total = 0;

  BufferVec::const_iterator it = buffers_.begin(), end = buffers_.end();

  LOG_TRACE("Copying %u bufs", static_cast<unsigned int>(buffers_.size()));

  bool is_done = (it == end);

  while (!is_done) {
    assert(it->size() > 0);
    size_t size = it->size();

    size_t to_copy = size - offset;
    size_t available = SSL_WRITE_SIZE - copied;
    if (available < to_copy) {
      to_copy = available;
    }

    memcpy(buf + copied, it->data() + offset, to_copy);

    copied += to_copy;
    offset += to_copy;
    total += to_copy;

    if (offset == size) {
      ++it;
      offset = 0;
    }

    is_done = (it == end);

    if (is_done || copied == SSL_WRITE_SIZE) {
      int rc = ssl_session_->encrypt(buf, copied);
      if (rc <= 0 && ssl_session_->has_error()) {
        LOG_ERROR("Unable to encrypt data: %s", ssl_session_->error_message().c_str());
        socket_->defunct();
        return;
      }
      copied = 0;
    }
  }

  LOG_TRACE("Copied %u bytes for encryption", static_cast<unsigned int>(total));
}

void SslSocketWrite::on_write(uv_write_t* req, int status) {
  if (status == 0) {
    SslSocketWrite* socket_write = static_cast<SslSocketWrite*>(req->data);
    socket_write->ssl_session_->outgoing().read(NULL, socket_write->encrypted_size_);
  }
  SocketWriteBase::on_write(req, status);
}

SocketWriteBase* SslSocketHandler::new_pending_write(Socket* socket) {
  return new SslSocketWrite(socket, ssl_session_.get());
}

void SslSocketHandler::alloc_buffer(size_t suggested_size, uv_buf_t* buf) {
  buf->base = ssl_session_->incoming().peek_writable(&suggested_size);
  buf->len = suggested_size;
}

void SslSocketHandler::on_read(Socket* socket, ssize_t nread, const uv_buf_t* buf) {
  if (nread < 0) return;

  ssl_session_->incoming().commit(nread);
  char decrypted[SSL_READ_SIZE];
  int rc = 0;
  while ((rc = ssl_session_->decrypt(decrypted, sizeof(decrypted))) > 0) {
    on_ssl_read(socket, decrypted, rc);
  }
  if (rc <= 0 && ssl_session_->has_error()) {
    if (ssl_session_->error_code() == CASS_ERROR_SSL_CLOSED) {
      LOG_DEBUG("SSL session closed");
      socket->close();
    } else {
      LOG_ERROR("Unable to decrypt data: %s", ssl_session_->error_message().c_str());
      socket->defunct();
    }
  }
}

uv_tcp_t* SocketWriteBase::tcp() { return &socket_->tcp_; }

SocketWriteBase::SocketWriteBase(Socket* socket)
    : socket_(socket)
    , is_flushed_(false)
    , handler_generation_(socket->handler_generation()) {
  req_.data = this;
  buffers_.reserve(MIN_BUFFERS_SIZE);
}

void SocketWriteBase::on_close() {
  for (RequestVec::iterator i = requests_.begin(), end = requests_.end(); i != end; ++i) {
    (*i)->on_close();
  }
}

int32_t SocketWriteBase::write(SocketRequest* request) {
  size_t last_buffer_size = buffers_.size();
  int32_t request_size = request->encode(&buffers_);
  if (request_size <= 0) {
    buffers_.resize(last_buffer_size); // Rollback
    return request_size;
  }

  requests_.push_back(request);

  return request_size;
}

void SocketWriteBase::on_write(uv_write_t* req, int status) {
  SocketWriteBase* pending_write = static_cast<SocketWriteBase*>(req->data);
  pending_write->handle_write(req, status);
}

void SocketWriteBase::handle_write(uv_write_t* req, int status) {
  Socket* socket = socket_;

  if (status != 0) {
    if (!socket->is_closing()) {
      LOG_ERROR("Socket write error '%s'", uv_strerror(status));
      socket->defunct();
    }
  }

  if (socket->handler_) {
    for (RequestVec::iterator i = requests_.begin(), end = requests_.end(); i != end; ++i) {
      socket->handler_->on_write(socket, status, *i);
    }
  }

  socket->pending_writes_.remove(this);

  // Don't recycle a write created under a handler that's since been replaced.
  if (handler_generation_ == socket->handler_generation_ &&
      socket->free_writes_.size() < socket->max_reusable_write_objects_) {
    clear();
    socket->free_writes_.push_back(this);
  } else {
    delete this;
  }

  socket->flush();
}

Socket::Socket(const Address& address, size_t max_reusable_write_objects)
    : handler_generation_(0)
    , is_defunct_(false)
    , max_reusable_write_objects_(max_reusable_write_objects)
    , address_(address) {
  tcp_.data = this;
}

Socket::~Socket() { cleanup_free_writes(); }

void Socket::set_handler(SocketHandlerBase* handler) {
  handler_.reset(handler);
  ++handler_generation_;
  cleanup_free_writes();
  free_writes_.clear();
  if (handler_) {
    uv_read_start(reinterpret_cast<uv_stream_t*>(&tcp_), Socket::alloc_buffer, Socket::on_read);
  } else {
    uv_read_stop(reinterpret_cast<uv_stream_t*>(&tcp_));
  }
}

int32_t Socket::write(SocketRequest* request) {
  if (!handler_) {
    return SocketRequest::SOCKET_REQUEST_ERROR_NO_HANDLER;
  }

  if (is_closing()) {
    return SocketRequest::SOCKET_REQUEST_ERROR_CLOSED;
  }

  if (pending_writes_.is_empty() || pending_writes_.back()->is_flushed()) {
    if (!free_writes_.empty()) {
      pending_writes_.add_to_back(free_writes_.back());
      free_writes_.pop_back();
    } else {
      pending_writes_.add_to_back(handler_->new_pending_write(this));
    }
  }

  return pending_writes_.back()->write(request);
}

int32_t Socket::write_and_flush(SocketRequest* request) {
  int32_t result = write(request);
  if (result > 0) {
    flush();
  }
  return result;
}

size_t Socket::flush() {
  if (pending_writes_.is_empty()) return 0;

  return pending_writes_.back()->flush();
}

bool Socket::is_closing() const {
  return uv_is_closing(reinterpret_cast<const uv_handle_t*>(&tcp_)) != 0;
}

void Socket::close() {
  uv_handle_t* handle = reinterpret_cast<uv_handle_t*>(&tcp_);
  if (!uv_is_closing(handle)) {
    uv_close(handle, on_close);
  }
}

void Socket::defunct() {
  close();
  is_defunct_ = true;
}

void Socket::alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  Socket* socket = static_cast<Socket*>(handle->data);
  socket->handler_->alloc_buffer(suggested_size, buf);
}

void Socket::on_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf) {
  Socket* socket = static_cast<Socket*>(client->data);
  socket->handle_read(nread, buf);
}

void Socket::handle_read(ssize_t nread, const uv_buf_t* buf) {
  if (nread < 0) {
    if (nread != UV_EOF) {
      LOG_ERROR("Socket read error '%s'", uv_strerror(nread));
    }
    defunct();
  }
  handler_->on_read(this, nread, buf);
}

void Socket::on_close(uv_handle_t* handle) {
  Socket* socket = static_cast<Socket*>(handle->data);
  socket->handle_close();
}

void Socket::handle_close() {
  LOG_DEBUG("Socket(%p) to host %s closed", static_cast<void*>(this), address_.to_string().c_str());

  while (!pending_writes_.is_empty()) {
    SocketWriteBase* pending_write = pending_writes_.pop_front();
    pending_write->on_close();
    delete pending_write;
  }

  if (handler_) {
    handler_->on_close();
  }
  dec_ref();
}

void Socket::cleanup_free_writes() {
  for (SocketWriteVec::iterator i = free_writes_.begin(), end = free_writes_.end(); i != end; ++i) {
    delete *i;
  }
}
