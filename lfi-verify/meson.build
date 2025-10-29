disarm = subproject('disarm').get_variable('disarm64')
fadec = subproject('fadec').get_variable('fadec')
srcs = files(
    'src/arm64/verify.c',
    'src/x64/verify.c',
    'src/x64/bdd.c',
    'src/x64/bddinc.S',
    'src/x64_large/verify.c',
    'src/x64_large/bdd.c',
    'src/x64_large/bddinc.S',
    'src/riscv64/verify.c',
)

c_defines = []
c_defines += '-DARCH_ARM64'
c_defines += '-DARCH_X64'
c_defines += '-DARCH_RISCV64'
c_defines += '-ggdb'
c_defines += '-O0'

liblfiv = static_library(
  'lfiv',
  srcs,
  c_args: c_defines,
  dependencies: [disarm.as_link_whole(), fadec.as_link_whole()],
  include_directories: include_directories('src/include'),
  install: true,
)

liblfiv_dep = declare_dependency(link_with: liblfiv, include_directories: include_directories('src/include'))


lfiv_exec = executable(
  'lfi-verify',
  files('tools/lfi-verify/main.c'),
  link_with: [liblfiv],
  include_directories: include_directories('src/include'),
  c_args: c_defines,
  dependencies: [argp],
  install: true,
)

install_headers('src/include/lfiv.h')

subdir('test')
