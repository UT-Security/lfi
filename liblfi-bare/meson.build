srcs = files(
  'tux.c',
  'buf.c',
  'proc.c',
  'elfload.c',
  'elfsym.c',
  'sys.c',
  'err.c',
)

subdir('pal')
subdir('arch/' + xcpuarch)

cwalk = subproject('cwalk')

liblfi_bare = static_library(
  'lfibare',
  sources: srcs,
  c_args: c_args_hardened + c_args_warnings + defines + ['-fvisibility=hidden'],
  include_directories: include_directories('include', 'arch/' + xcpuarch, 'arch/'),
  dependencies: [
    cwalk.get_variable('cwalk_dep').as_link_whole(),
  ],
  install: true,
)

install_headers('include/lfi.h')
install_headers('include/lfi_tux.h')
install_headers('include/lfi_arch.h')

liblfi_bare_dep = declare_dependency(link_with: liblfi_bare, include_directories: include_directories('include'))
