project('nvim-ui', ['c', 'cpp'],
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
  ],
  version: '0.1.0',
  license: 'MIT')

add_project_arguments('-DGETTEXT_PACKAGE="nvim-ui"', language:'cpp')
add_global_arguments('-DVERSION="' + meson.project_version() + '"', language: ['c', 'cpp'])
add_global_arguments('-DPREFIX="' + get_option('prefix') + '"', language: ['c', 'cpp'])
add_global_arguments('-DDATA_DIR="' + get_option('datadir') + '"', language: ['c', 'cpp'])

#msgpack_dep = dependency('msgpack')
libuv_dep = dependency('libuv')
spdlog_dep = dependency('spdlog')
fmt_dep = dependency('fmt')
gtk4_dep = dependency('gtk4')

cmake = import('cmake')

msgpack_cxx = cmake.subproject('msgpack-c')
msgpack_cxx_dep = msgpack_cxx.dependency('msgpackc-cxx')

subdir('res')
subdir('src')
subdir('tests')
subdir('po')

markdown = find_program('markdown_py')

changelog = custom_target('changelog',
  output : 'CHANGELOG.html',
  input : 'CHANGELOG.md',
  command : [markdown, '@INPUT@', '-f', '@OUTPUT@'],
  install : true,
  install_dir : join_paths(get_option('datadir'), 'doc/nvim-ui'))
