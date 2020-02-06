project('blz4', 'c',
  meson_version : '>=0.45.0',
  default_options : [
    'b_ndebug=if-release',
    'buildtype=release',
    'c_std=c99',
    'default_library=static',
    'warning_level=3',
  ],
  version : '0.2.0',
  license : 'Zlib'
)

lib = library('lz4', 'lz4.c', 'lz4_depack.c')

lz4_dep = declare_dependency(
  include_directories : include_directories('.'),
  link_with : lib,
  version : meson.project_version()
)

executable('blz4', 'blz4.c', 'parg.c', dependencies : lz4_dep)
