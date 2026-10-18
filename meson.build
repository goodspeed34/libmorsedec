project('libmorsedec', 'c', version: '0.3.0', license: 'GPL-3.0-or-later',
  meson_version: '>=0.63.0', default_options: ['c_std=c11', 'warning_level=3'])
cc = meson.get_compiler('c')
fftw = dependency('fftw3f', required: false)
if not fftw.found()
  fftw = declare_dependency(dependencies: cc.find_library('fftw3f'),
    compile_args: [], include_directories: include_directories('.'))
endif
math = cc.find_library('m', required: false)
inc = include_directories('include', 'src')
sources = files('src/decoder.c', 'src/frontend.c', 'src/band.c', 'src/signal.c',
  'src/parallel.c', 'src/decode_compat.c', 'src/wfst_decode.c', 'src/statistics.c', 'src/word.c', 'src/message.c')
cargs = ['-DMORSEDEC_BUILD']
thread_deps = []
have_parallel = false
parallel = get_option('parallel')
if not parallel.disabled()
  threads = dependency('threads', required: parallel)
  have_parallel = threads.found() and cc.links('''
    #include <pthread.h>
    static void *worker(void *p) { return p; }
    int main(void) {
      pthread_t thread;
      pthread_mutex_t mutex;
      pthread_cond_t condition;
      pthread_mutex_init(&mutex, 0);
      pthread_cond_init(&condition, 0);
      if (pthread_create(&thread, 0, worker, 0)) return 1;
      return pthread_join(thread, 0);
    }
  ''', dependencies: threads, name: 'channel worker backend')
  if have_parallel
    cargs += '-DMD_PARALLEL'
    thread_deps += threads
  elif parallel.enabled()
    error('Parallel processing requires pthreads (winpthreads on MinGW)')
  endif
endif
if get_option('default_library') == 'static'
  cargs += '-DMORSEDEC_STATIC'
endif
lib = library('morsedec', sources, include_directories: inc,
  dependencies: [fftw, math] + thread_deps, c_args: cargs, gnu_symbol_visibility: 'hidden',
  version: meson.project_version(), soversion: '2', install: true)
consumer_args = []
if get_option('default_library') == 'static'
  consumer_args += '-DMORSEDEC_STATIC'
endif
morsedec_dep = declare_dependency(link_with: lib, include_directories: include_directories('include'),
  compile_args: consumer_args, dependencies: [fftw, math] + thread_deps)
install_headers('include/morsedec.h')
subdir('docs/man')
pkg = import('pkgconfig')
pkg.generate(lib, name: 'libmorsedec', filebase: 'morsedec',
  description: 'Streaming multichannel amateur radio CW decoder',
  extra_cflags: consumer_args, requires_private: 'fftw3f', libraries_private: [math] + thread_deps)
sndfile = dependency('sndfile', required: get_option('tools'))
if sndfile.found() and not get_option('tools').disabled()
  # tui.c contributes only its no-op stubs here: the file decoder shares the
  # reporter but has no terminal view.
  cli = executable('morsedec-audio',
    ['tools/morsedec-audio.c', 'tools/audio-common.c', 'tools/tui.c'],
    dependencies: [morsedec_dep, sndfile, math], install: true)
endif
live_cargs = []
live_deps = [morsedec_dep, math]
live_sources = ['tools/live-common.c', 'tools/audio-common.c', 'tools/tui.c']
if not get_option('tools').disabled()
  # The ncurses terminal interface (-T) is optional and enabled by default.
  # Detection falls back from the wide pkg-config module to the narrow one and
  # finally to the system library, so a missing .pc file does not disable it.
  tui = get_option('tui')
  if not tui.disabled()
    ncurses = dependency('ncursesw', required: false)
    if not ncurses.found()
      ncurses = dependency('ncurses', required: false)
    endif
    if not ncurses.found()
      ncurses = cc.find_library('ncursesw', required: false,
        has_headers: 'ncurses.h')
    endif
    if not ncurses.found()
      ncurses = cc.find_library('ncurses', required: false,
        has_headers: 'ncurses.h')
    endif
    if not ncurses.found() and tui.enabled()
      error('ncurses is required for the -T terminal view; install it or configure with -Dtui=disabled')
    endif
    if ncurses.found()
      live_cargs += '-DMORSEDEC_TUI'
      live_deps += ncurses
    endif
  endif
  live = executable('morsedec-live', ['tools/morsedec-live.c'] + live_sources,
    dependencies: live_deps, c_args: live_cargs, install: true)
  iqlive = executable('morsedec-iqlive', ['tools/morsedec-iqlive.c'] + live_sources,
    dependencies: live_deps, c_args: live_cargs, install: true)
endif
example = executable('morsedec-example', 'examples/decode.c', dependencies: morsedec_dep)
formatter = find_program('clang-format', required: false)
if formatter.found()
  format_files = sources + files('include/morsedec.h', 'src/internal.h', 'src/wfst_decode.h', 'src/wfst_graph.h', 'src/codes.h', 'src/templates.h',
    'tools/morsedec-audio.c', 'tools/morsedec-live.c', 'tools/morsedec-iqlive.c',
    'tools/live-common.c', 'tools/audio-common.c', 'tools/tui.c',
    'tools/audio-common.h', 'tools/tui.h', 'examples/decode.c',
    'benchmarks/check-parallel.c', 'benchmarks/check-patterns.c', 'benchmarks/check-wfst.c', 'benchmarks/parallel-speed.c', 'benchmarks/speed.c')
  run_target('format', command: [formatter, '-i', format_files])
  run_target('format-check', command: [formatter, '--dry-run', '--Werror', format_files])
endif
install_data('COPYING', install_dir: get_option('datadir') / 'licenses' / 'libmorsedec')

# Link a private static copy so the existing internal regression checks can
# exercise hidden helpers without exporting them from the public library.
check_args = cargs + ['-DMORSEDEC_STATIC', '-UNDEBUG']
check_lib = static_library('morsedec-check', sources,
  include_directories: inc, dependencies: [fftw, math] + thread_deps,
  c_args: check_args, install: false)
check_deps = [fftw, math] + thread_deps
pattern_check = executable('check-patterns', 'benchmarks/check-patterns.c',
  include_directories: inc, link_with: check_lib,
  dependencies: check_deps, c_args: check_args)
test('patterns', pattern_check)
wrap_args = []
wrap_links = []
if have_parallel and host_machine.system() == 'linux'
  wrap_args = ['-DMD_WRAP_THREADS']
  wrap_links = ['-Wl,--wrap=pthread_create', '-Wl,--wrap=calloc']
endif
parallel_check = executable('check-parallel', 'benchmarks/check-parallel.c',
  include_directories: inc, link_with: check_lib, dependencies: check_deps,
  c_args: check_args + wrap_args, link_args: wrap_links)
test('parallel', parallel_check, timeout: 120)

executable('parallel-speed', 'benchmarks/parallel-speed.c',
  dependencies: [morsedec_dep, math], build_by_default: false)

allocation_args = []
allocation_links = []
if host_machine.system() == 'linux'
  allocation_args = ['-DMD_WRAP_ALLOC']
  allocation_links = ['-Wl,--wrap=malloc', '-Wl,--wrap=calloc',
    '-Wl,--wrap=realloc', '-Wl,--wrap=free']
endif
wfst_check = executable('check-wfst', 'benchmarks/check-wfst.c',
  include_directories: inc, link_with: check_lib,
  dependencies: check_deps, c_args: check_args + allocation_args,
  link_args: allocation_links)
test('wfst', wfst_check)

python = find_program('python3', required: false)
if python.found()
  test('wfst-graph', python, args: [files('scripts/gen-wfst.py'), '--check'])
endif
