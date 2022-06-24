project('jakt-lsp', 'cpp', default_options : [ 'cpp_std=c++20' ])

cmake = import('cmake')

fmtlib = cmake.subproject('fmt')
fmtdep = fmtlib.dependency('fmt')

inc = include_directories('.')

executable('jakt-lsp', sources : [
  'main.cpp', 
  'json.cpp',
  'rpc/rpc.cpp',], include_directories : inc,
    dependencies : [fmtdep])
