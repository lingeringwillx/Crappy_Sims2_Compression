import os
import shutil
import subprocess
import time
import sys

#call different executables and test their performance on one file

class Test:
    def __init__(self, exe, file_name):
        self.exe = exe
        self.new_path = os.path.join(test_directory, file_name)
        self.new_size = 0
        self.duration = 0
        
def log(text=''):
    print(text)
    with open(log_file, 'a') as fs:
        fs.write(text + '\n')
        
def get_size_text(size):
    if size > 1000:
        return '{:.2f} MB'.format(size / 1024)
    else:
        return '{:.2f} KB'.format(size)
        
def get_duration_text(duration):
    if duration > 1:
        return '{:.2f}s '.format(duration)
    else:
        return '{:.2f}ms'.format(duration * 1000)
        
test_directory = 'benchmark'
log_file = 'benchmark.txt'
    
exes = sys.argv[1:-1]
file = sys.argv[-1]

old_size = os.path.getsize(file) / 1024 #KB

if os.path.isfile(log_file):
    os.remove(log_file)
    
if not os.path.isdir('benchmark'):
    os.mkdir('benchmark')

tests = []
for i, exe in enumerate(exes):
    if os.path.isfile(exe) or os.path.isfile(exe + '.exe'):
        tests.append(Test(exe, file.rsplit('.')[0] + str(i) + '.package'))
    else:
        print(exe + ' not found\n')
        sys.exit()
        
print()

for test in tests:
    shutil.copy(file, test.new_path)
    
    t = time.perf_counter()
    
    subprocess.run([test.exe, test.new_path], stdout=subprocess.DEVNULL)
    
    test.duration = time.perf_counter() - t
    test.new_size = os.path.getsize(test.new_path) / 1024
    
    os.remove(test.new_path)
    
    log('{}: {} {:.2f}% {}'.format(test.exe, get_size_text(test.new_size), test.new_size / old_size * 100, get_duration_text(test.duration)))
    
os.rmdir(test_directory)

print()
