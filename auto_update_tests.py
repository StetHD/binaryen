#!/usr/bin/env python

import os, sys, subprocess, difflib

from scripts.support import run_command, split_wast


print '\n[ checking example testcases... ]\n'

for t in sorted(os.listdir(os.path.join('test', 'example'))):
  output_file = os.path.join('bin', 'example')
  cmd = ['-Isrc', '-g', '-lasmjs', '-lsupport', '-Llib/.', '-pthread', '-o', output_file]
  if t.endswith('.txt'):
    # check if there is a trace in the file, if so, we should build it
    out = subprocess.Popen([os.path.join('scripts', 'clean_c_api_trace.py'), os.path.join('test', 'example', t)], stdout=subprocess.PIPE).communicate()[0]
    if len(out) == 0:
      print '  (no trace in ', t, ')'
      continue
    print '  (will check trace in ', t, ')'
    src = 'trace.cpp'
    with open(src, 'w') as o: o.write(out)
    expected = os.path.join('test', 'example', t + '.txt')
  else:
    src = os.path.join('test', 'example', t)
    expected = os.path.join('test', 'example', '.'.join(t.split('.')[:-1]) + '.txt')
  if src.endswith(('.c', '.cpp')):
    # build the C file separately
    extra = [os.environ.get('CC') or 'gcc',
             src, '-c', '-o', 'example.o',
             '-Isrc', '-g', '-Llib/.', '-pthread']
    print 'build: ', ' '.join(extra)
    subprocess.check_call(extra)
    # Link against the binaryen C library DSO, using an executable-relative rpath
    cmd = ['example.o', '-lbinaryen'] + cmd + ['-Wl,-rpath=$ORIGIN/../lib']
  else:
    continue
  print '  ', t, src, expected
  if os.environ.get('COMPILER_FLAGS'):
    for f in os.environ.get('COMPILER_FLAGS').split(' '):
      cmd.append(f)
  cmd = [os.environ.get('CXX') or 'g++', '-std=c++11'] + cmd
  try:
    print 'link: ', ' '.join(cmd)
    subprocess.check_call(cmd)
    print 'run...', output_file
    proc = subprocess.Popen([output_file], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    actual, err = proc.communicate()
    assert proc.returncode == 0, [proc.returncode, actual, err]
    with open(expected, 'w') as o: o.write(actual)
  finally:
    #os.remove(output_file)
    if sys.platform == 'darwin':
      # Also removes debug directory produced on Mac OS
      shutil.rmtree(output_file + '.dSYM')

print '\n[ success! ]'
