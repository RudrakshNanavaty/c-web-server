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

sources = [
  'src/main.c',
  'src/server.c',
  'src/http.c',
]

exe = executable(
  'web-server',
  sources,
  include_directories : inc,
  dependencies : dependencies,
  install : true,
)

run_target('run', command: [exe])

test('basic', exe)
