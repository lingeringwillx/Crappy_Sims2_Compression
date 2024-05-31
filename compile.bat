call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

cl /EHsc /std:c++17 /openmp /O2 dbpf-recompress.cpp

del dbpf-recompress.obj

pause
