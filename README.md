My own attempt at rewriting the compression code for The Sims 2. The non-parallel version is currently 3-4x times slower than the Compressorizer so it's not really useful.

The compressed output is only a few kilobytes bigger than the compressorizer.

This project utilizes a lot programming techniques such as unparsing & parsing of binary files, memory streams, pattern matching through map lookups, binary search, LZSS-like compression, bit masks and bit shifting, and parallelism.
