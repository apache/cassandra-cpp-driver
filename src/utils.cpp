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

#include "utils.hpp"

#include "constants.hpp"

#include <algorithm>
#include <assert.h>
#include <functional>
#include <uv.h>

#if (defined(WIN32) || defined(_WIN32))
#include <windows.h>
#else
#include <sched.h>
#include <unistd.h>
#endif

namespace datastax { namespace internal {

String opcode_to_string(int opcode) {
  switch (opcode) {
    case CQL_OPCODE_ERROR:
      return "CQL_OPCODE_ERROR";
    case CQL_OPCODE_STARTUP:
      return "CQL_OPCODE_STARTUP";
    case CQL_OPCODE_READY:
      return "CQL_OPCODE_READY";
    case CQL_OPCODE_AUTHENTICATE:
      return "CQL_OPCODE_AUTHENTICATE";
    case CQL_OPCODE_CREDENTIALS:
      return "CQL_OPCODE_CREDENTIALS";
    case CQL_OPCODE_OPTIONS:
      return "CQL_OPCODE_OPTIONS";
    case CQL_OPCODE_SUPPORTED:
      return "CQL_OPCODE_SUPPORTED";
    case CQL_OPCODE_QUERY:
      return "CQL_OPCODE_QUERY";
    case CQL_OPCODE_RESULT:
      return "CQL_OPCODE_RESULT";
    case CQL_OPCODE_PREPARE:
      return "CQL_OPCODE_PREPARE";
    case CQL_OPCODE_EXECUTE:
      return "CQL_OPCODE_EXECUTE";
    case CQL_OPCODE_REGISTER:
      return "CQL_OPCODE_REGISTER";
    case CQL_OPCODE_EVENT:
      return "CQL_OPCODE_EVENT";
    case CQL_OPCODE_BATCH:
      return "CQL_OPCODE_BATCH";
    case CQL_OPCODE_AUTH_CHALLENGE:
      return "CQL_OPCODE_AUTH_CHALLENGE";
    case CQL_OPCODE_AUTH_RESPONSE:
      return "CQL_OPCODE_AUTH_RESPONSE";
    case CQL_OPCODE_AUTH_SUCCESS:
      return "CQL_OPCODE_AUTH_SUCCESS";
  };
  assert(false);
  return "";
}

String to_string(const CassUuid& uuid) {
  char str[CASS_UUID_STRING_LENGTH];
  cass_uuid_string(uuid, str);
  return String(str);
}

void explode(const String& str, Vector<String>& vec, const char delimiter /* = ',' */) {
  IStringStream stream(str);
  while (!stream.eof()) {
    String token;
    std::getline(stream, token, delimiter);
    if (!trim(token).empty()) {
      vec.push_back(token);
    }
  }
}

String implode(const Vector<String>& vec, const char delimiter /* = ' ' */) {
  String str;
  for (Vector<String>::const_iterator it = vec.begin(), end = vec.end(); it != end; ++it) {
    if (!str.empty()) {
      str.push_back(delimiter);
    }
    str.append(*it);
  }
  return str;
}

bool not_isspace(int c) { return !::isspace(c); }

String& trim(String& str) {
  // Trim front
  str.erase(str.begin(), std::find_if(str.begin(), str.end(), not_isspace));
  // Trim back
  str.erase(std::find_if(str.rbegin(), str.rend(), not_isspace).base(), str.end());
  return str;
}

static bool is_lowercase(const String& str) {
  if (str.empty()) return true;

  char c = str[0];
  if (!(c >= 'a' && c <= 'z')) return false;

  for (String::const_iterator it = str.begin() + 1, end = str.end(); it != end; ++it) {
    char c = *it;
    if (!((c >= '0' && c <= '9') || (c == '_') || (c >= 'a' && c <= 'z'))) {
      return false;
    }
  }
  return true;
}

static String& quote_id(String& str) {
  String temp(str);
  str.clear();
  str.push_back('"');
  for (String::const_iterator i = temp.begin(), end = temp.end(); i != end; ++i) {
    if (*i == '"') {
      str.push_back('"');
      str.push_back('"');
    } else {
      str.push_back(*i);
    }
  }
  str.push_back('"');

  return str;
}

String& escape_id(String& str) { return is_lowercase(str) ? str : quote_id(str); }

String unescape_id(const String& str) {
  if (str.size() < 2 || str.front() != '"' || str.back() != '"') {
    return str;
  }

  String result;
  result.reserve(str.size() - 2);
  for (size_t i = 1, end = str.size() - 1; i < end; ++i) {
    char c = str[i];
    if (c == '"' && i + 1 < end && str[i + 1] == '"') {
      ++i; // Escaped quote
    }
    result.push_back(c);
  }
  return result;
}

int32_t get_pid() {
#if (defined(WIN32) || defined(_WIN32))
  return static_cast<int32_t>(GetCurrentProcessId());
#else
  return static_cast<int32_t>(getpid());
#endif
}

void thread_yield() {
#if defined(WIN32) || defined(_WIN32)
  SwitchToThread();
#else
  sched_yield();
#endif
}

// Code was taken from MSDN documentation see:
// https://docs.microsoft.com/en-us/visualstudio/debugger/how-to-set-a-thread-name-in-native-code
#if defined(_MSC_VER) && defined(_DEBUG)
const DWORD MS_VC_EXCEPTION = 0x406D1388;
#pragma pack(push, 8)
typedef struct tagTHREADNAME_INFO {
  DWORD dwType;     // Must be 0x1000.
  LPCSTR szName;    // Pointer to name (in user addr space).
  DWORD dwThreadID; // Thread ID (-1=caller thread).
  DWORD dwFlags;    // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)
#endif
void set_thread_name(const String& thread_name) {
#if defined(_MSC_VER) && defined(_DEBUG)
  THREADNAME_INFO info;
  info.dwType = 0x1000;
  info.szName = thread_name.c_str();
  info.dwThreadID = (DWORD)-1;
  info.dwFlags = 0;
#pragma warning(push)
#pragma warning(disable : 6320 6322)
  __try {
    RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR),
                   reinterpret_cast<ULONG_PTR*>(&info));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
#pragma warning(pop)
#endif
}

}} // namespace datastax::internal
