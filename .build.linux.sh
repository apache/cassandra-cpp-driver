#!/bin/bash
##
#  Copyright (c) DataStax, Inc.
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
##

configure_testing_environment() {
  if ! grep -lq "127.254.254.254" /etc/hosts; then
    printf "\n\n%s\n" "127.254.254.254  cpp-driver.hostname." | sudo tee -a /etc/hosts
  fi
  sudo cat /etc/hosts
}

install_libuv() {(
  git clone https://github.com/libuv/libuv.git
  cd libuv
  git checkout v${LIBUV_VERSION}
  sh autogen.sh
  ./configure --prefix=${HOME}/libuv-${LIBUV_VERSION}
  make
  make install
)}

install_openssl() {
  true # Already installed on image
}

install_zlib() {
  true # Already installed on image
}
