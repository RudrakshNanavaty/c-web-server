project(
  'web-server',
  'c',
  meson_version : '>= 1.3.0',
  version : '0.1',
  default_options : ['warning_level=3'],
)

dependencies = [
  dependency('threads'),
]

inc = include_directories('include')

libhttp = static_library(
  'http',
  'src/http.c',
  include_directories : inc,
)

exe = executable(
  'web-server',
  ['src/main.c', 'src/server.c'],
  include_directories : inc,
  dependencies : dependencies,
  link_with : libhttp,
  install : true,
)

run_target('run', command: [exe])
