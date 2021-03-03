# Copyright (c) 2021 Ilya Maximets <i.maximets@ovn.org>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

project(
    'one-socket', 'C',
    version: '0.0.1',
    license: 'Apache License 2.0',
    default_options: ['buildtype=release', 'default_library=static'],
    meson_version: '>= 0.47'
)

cc = meson.get_compiler('c')

conf_data = configuration_data()
conf_data.set('version', meson.project_version())
configure_file(
    input: 'config.h.in',
    output: 'config.h',
    configuration: conf_data
)

incdir = include_directories('.', 'include', 'lib')

warning_flags = [
    '-Wno-address-of-packed-member',
]

foreach arg: warning_flags
    if cc.has_argument(arg)
        add_project_arguments(arg, language: 'c')
    endif
endforeach

lib_src = [
    'include/socketpair-broker/helper.h',
    'lib/socketpair-broker-helper.c',
    'lib/socket-util.h',
    'lib/socket-util.c',
]

libspbroker = library('spbroker', lib_src,
                      install: true,
                      include_directories: incdir,
                      version : meson.project_version())

pkg = import('pkgconfig')
pkg.generate(libspbroker)

subdir('test')

thread_dep = dependency('threads')

headers = [
    'include/socketpair-broker/proto.h',
    'include/socketpair-broker/helper.h',
]
install_headers(headers, subdir: 'socketpair-broker')

src = [
    'lib/broker.c',
    'lib/polling.c',
    'lib/socket-util.c',
    'lib/worker.c',
    'one-socket.c',
]

executable(
    'one-socket',
    sources: src,
    include_directories: incdir,
    install: true,
    dependencies: thread_dep,
    link_with: libspbroker
)
