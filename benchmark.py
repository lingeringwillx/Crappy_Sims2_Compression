import os
import shutil
import subprocess
import time
import sys

test_directory = 'benchmark'
log_file = 'benchmark.txt'

if os.path.isfile(log_file):
    os.remove(log_file)

def log(text=''):
    print(text)
    with open(log_file, 'a') as fs:
        fs.write(text + '\n')
        
class Test:
    def __init__(self, name, args, file_name):
        self.name = name
        self.args = args
        self.file_name = os.path.join(test_directory, file_name) + '.package'
        self.new_size = 0
        self.duration = 0
        
file = sys.argv[1]
old_size = os.path.getsize(file) / 1024 #KB

if not os.path.isdir('benchmark'):
    os.mkdir('benchmark')

tests = [Test('Decompress', ['-d'], 'decompressed'), Test('Decompress Parallel', ['-d', '-p'], 'decompressedparallel')]

for i in range(1, 11, 2):
    tests.append(Test('Level {}'.format(i), ['-l{}'.format(i)], 'level{}'.format(i)))
    
for i in range(1, 11, 2):
    tests.append(Test('Level {} Parallel'.format(i), ['-l{}'.format(i), '-p'], 'level{}parallel'.format(i)))

for i in range(1, 11, 2):
    tests.append(Test('Level {} Recompress'.format(i), ['-l{}'.format(i), '-r'], 'level{}recompress'.format(i)))
    
for i in range(1, 11, 2):
    tests.append(Test('Level {} P + R'.format(i), ['-l{}'.format(i), '-r', '-p'], 'level{}parallelrecompress'.format(i)))
    
for test in tests:
    shutil.copy(file, test.file_name)
    
    print('{}...'.format(test.name))
    
    t = time.perf_counter()
    
    subprocess.run(['dbpf-recompress.exe'] + test.args + [test.file_name])
    test.duration = time.perf_counter() - t
    
    test.new_size = os.path.getsize(test.file_name) / 1024
        
    os.remove(test.file_name)
    
os.rmdir(test_directory)

print()
    
if old_size > 1000:
    log('Original File Size: {:.2f} MB\n'.format(old_size / 1024))
else:
    log('Original File Size: {:.2f} KB\n'.format(old_size))
    
log('{:<21}{:<8}{:<11}Compression Ratio\n'.format('Test', 'Time', 'New Size'))

#decompression + levels non-parallel
for test in tests[0:7] + tests[12:17]:
    if test.duration > 1:
        duration_text = '{:.2f}s '.format(test.duration)
    else:
        duration_text = '{:.2f}ms'.format(test.duration / 1000)
        
    if test.new_size > 1000:
        size_text = '{:.2f} MB'.format(test.new_size / 1024)
    else:
        size_text = '{:.2f} KB'.format(test.new_size)
        
    log('{:<21}{:<8}{:<11}{:.2f}%'.format(test.name, duration_text, size_text, test.new_size / old_size * 100))
    
log('\n==================================================\n')

#we only care about the speed for the rest of the tests
log('{:<21}Time\n'.format('Test'))

for i in range(2, 7):
    for test in tests[i:len(tests):5]:
        if test.duration > 1:
            duration_text = '{:.2f}s '.format(test.duration)
        else:
            duration_text = '{:.2f}ms'.format(test.duration / 1000)
            
        log('{:<21}{}'.format(test.name, duration_text))
        
    log()