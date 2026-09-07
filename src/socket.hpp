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

#ifndef DATASTAX_INTERNAL_SOCKET_HPP
#define DATASTAX_INTERNAL_SOCKET_HPP

#include "allocated.hpp"
#include "buffer.hpp"
#include "constants.hpp"
#include "list.hpp"
#include "scoped_ptr.hpp"
#include "ssl.hpp"
#include "stack.hpp"
#include "tcp_connector.hpp"
#include "timer.hpp"

#include <uv.h>

#define MIN_BUFFERS_SIZE 128

namespace datastax { namespace internal { namespace core {

class Socket;
class SocketWriteBase;

/**
 * A generic socket request that handles encoding data to be written to a
 * socket.
 */
class SocketRequest
    : public Allocated
    , public List<SocketRequest>::Node {
public:
  virtual ~SocketRequest() {}

  enum {
    SOCKET_REQUEST_ERROR_CLOSED = CASS_INT32_MIN,
    SOCKET_REQUEST_ERROR_NO_HANDLER,
    SOCKET_REQUEST_ERROR_LAST_ENTRY
  };

  /**
   * Encode a request into buffers.
   *
   * @param bufs
   * @return The number of bytes written, or negative for an error
   */
  virtual int32_t encode(BufferVec* bufs) = 0;

  /**
   * Handle a socket closing during a request.
   */
  virtual void on_close() = 0;
};

/**
 * A basic socket request that appends a buffer to the encode buffers.
 */
class BufferSocketRequest : public SocketRequest {
public:
  /**
   * Constructor
   *
   * @param buf A buffer to append to buffers
   */
  BufferSocketRequest(const Buffer& buf)
      : buf_(buf) {}

  virtual int32_t encode(BufferVec* bufs) {
    bufs->push_back(buf_);
    return buf_.size();
  }

  virtual void on_close() {}

private:
  Buffer buf_;
};

/**
 * A generic handler for handling the basic actions of a socket. This allows
 * sockets to handle different ways of processing the socket's
 * incoming/outgoing data streams e.g. encryption/decryption,
 * compression/decompression, etc.
 */
class SocketHandlerBase : public Allocated {
public:
  virtual ~SocketHandlerBase() {}

  /**
   * Allocate a write request.
   *
   * @param socket The socket requesting the new write request.
   * @return A newly allocated write request.
   */
  virtual SocketWriteBase* new_pending_write(Socket* socket) = 0;
  /**
   * Allocate a buffer for reading data from the socket.
   *
   * @param suggested_size The suggested size for the read buffer.
   * @param buf The allocated buffer.
   */
  virtual void alloc_buffer(size_t suggested_size, uv_buf_t* buf) = 0;

  /**
   * A callback for handling a socket read.
   *
   * @param socket The socket receiving the read.
   * @param nread The size of the read or an error if negative.
   * @param buf The buffer read from the socket.
   */
  virtual void on_read(Socket* socket, ssize_t nread, const uv_buf_t* buf) = 0;

  /**
   * A callback for handling a socket write.
   *
   * @param socket The socket that processed the write.
   * @param status The status of the write. 0 if successful, otherwise and error.
   * @param request The request doing the write.
   */
  virtual void on_write(Socket* socket, int status, SocketRequest* request) = 0;

  /**
   * A callback for handling the socket close.
   */
  virtual void on_close() = 0;
};

/**
 * A basic socket handler that caches buffers used for reading socket data.
 */
class SocketHandler : public SocketHandlerBase {
public:
  ~SocketHandler();

  virtual SocketWriteBase* new_pending_write(Socket* socket);
  virtual void alloc_buffer(size_t suggested_size, uv_buf_t* buf);

  /**
   * Free or cache a read buffer.
   * @param buf The buffer to free or cache. The buffer was created in
   * alloc_buffer().
   */
  void free_buffer(const uv_buf_t* buf);

private:
  Stack<uv_buf_t> buffer_reuse_list_;
};

/**
 * A socket handler that encrypts/decrypts socket data using SSL.
 */
class SslSocketHandler : public SocketHandlerBase {
public:
  /**
   * Constructor
   *
   * @param ssl_session A SSL session used to encrypt/decrypt data.
   */
  SslSocketHandler(SslSession* ssl_session)
      : ssl_session_(ssl_session) {}

  virtual SocketWriteBase* new_pending_write(Socket* socket);
  virtual void alloc_buffer(size_t suggested_size, uv_buf_t* buf);

  virtual void on_read(Socket* socket, ssize_t nread, const uv_buf_t* buf);

  /**
   * A callback for handling decrypted socket data.
   *
   * @param socket The socket receiving the read.
   * @param buf The decrypted data.
   * @param size The number of bytes decrypted.
   */
  virtual void on_ssl_read(Socket* socket, char* buf, size_t size) = 0;

private:
  ScopedPtr<SslSession> ssl_session_;
};

/**
 * A generic write handler. This is used to coalesce several write requests
 * into a flush (a write() system call).
 */
class SocketWriteBase
    : public Allocated
    , public List<SocketWriteBase>::Node {
public:
  typedef internal::List<SocketWriteBase> List;

  /**
   * Constructor
   *
   * @param The socket handling the write.
   */
  SocketWriteBase(Socket* socket);

  virtual ~SocketWriteBase() {}

  uv_tcp_t* tcp();

  /**
   * Returns the flush status of the socket write.
   *
   * @return true if the requests have been flushed.
   */
  bool is_flushed() const { return is_flushed_; }

  /**
   * Clear the write so that it can be reused for more requests.
   */
  void clear() {
    buffers_.clear();
    requests_.clear();
    is_flushed_ = false;
  }

  /**
   * Handle a socket closing by calling request's on_close() callbacks.
   */
  void on_close();

  /**
   * Add a request to this write request.
   *
   * @param The request to add.
   * @return The number of bytes written. It's negative if an error occurred.
   */
  int32_t write(SocketRequest* request);

  /**
   * Flush the outstanding requests to the socket.
   */
  virtual size_t flush() = 0;

protected:
  static void on_write(uv_write_t* req, int status);
  void handle_write(uv_write_t* req, int status);

  typedef Vector<SocketRequest*> RequestVec;

  Socket* socket_;
  uv_write_t req_;
  bool is_flushed_;
  // Socket's handler generation when this write was created, to avoid reuse under a new handler.
  size_t handler_generation_;
  BufferVec buffers_;
  RequestVec requests_;
};

/**
 * A socket. It cannot be connected directly, use a SocketConnector to connect
 * a socket.
 *
 * @see SocketConnector
 */
class Socket : public RefCounted<Socket> {
  friend class SocketConnector;
  friend class SocketWriteBase;

public:
  typedef SharedRefPtr<Socket> Ptr;

  /**
   * Constructor: Don't use this directly.
   *
   * @param address The address for the socket.
   * @param max_reusable_write_objects The max limit on the number of write buffer
   * objects to keep around.
   */
  Socket(const Address& address, size_t max_reusable_write_objects);

  /**
   * Destructor.
   */
  ~Socket();

  /**
   * Set the handler that will process the actions for this socket.
   *
   * @param handler The socket handler.
   */
  void set_handler(SocketHandlerBase* handler);

  /**
   * The number of times the socket's handler has been set.
   */
  size_t handler_generation() const { return handler_generation_; }

  /**
   * Write a request to the socket and coalesce with outstanding requests. This
   * method doesn't flush.
   *
   * @param request The request to write to the socket.
   * @return The number of bytes written by the request to the socket.
   */
  int32_t write(SocketRequest* request);

  /**
   * Write a request to the socket and flush immediately.
   *
   * @param request The request to write to the socket.
   * @return The number of bytes written by the request to the socket.
   */
  int32_t write_and_flush(SocketRequest* request);

  /**
   * Flush all outstanding requests.
   */
  size_t flush();

  /**
   * Determine if the socket is closing.
   *
   * @return Returns true if closing.
   */

  bool is_closing() const;

  /**
   * Close the socket and notify all outstanding requests.
   */
  void close();

  /**
   * Determine if the socket is defunct.
   *
   * @return Returns true if defunct.
   */
  bool is_defunct() const { return is_defunct_; }

  /**
   * Mark as defunct and close the socket.
   */
  void defunct();

public:
  uv_tcp_t* handle() { return &tcp_; }
  uv_loop_t* loop() { return tcp_.loop; }

  const Address& address() const { return address_; }

private:
  static void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);

  static void on_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
  void handle_read(ssize_t nread, const uv_buf_t* buf);

  static void on_close(uv_handle_t* handle);
  void handle_close();

  void cleanup_free_writes();

private:
  typedef Vector<SocketWriteBase*> SocketWriteVec;

  uv_tcp_t tcp_;
  ScopedPtr<SocketHandlerBase> handler_;
  size_t handler_generation_;

  SocketWriteBase::List pending_writes_;
  SocketWriteVec free_writes_;

  bool is_defunct_;
  size_t max_reusable_write_objects_;

  Address address_;
};

}}} // namespace datastax::internal::core

#endif
