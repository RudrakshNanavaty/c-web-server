project(
  'web-server',
  'c',
  meson_version : '>= 1.3.0',
  version : '0.1',
  default_options : ['warning_level=3'],
)

dependencies = [
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

http_tests = executable(
  'http_tests',
  'tests/test_http.c',
  include_directories : inc,
  link_with : libhttp,
)

run_target('run', command: [exe])

test('http', http_tests)
