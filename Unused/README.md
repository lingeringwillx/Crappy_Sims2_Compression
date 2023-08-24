My own attempt at rewriting the compression code for The Sims 2.

It attempts to compress the file as much as possible. The compressed output is only a few kilobytes bigger than that of the compressorizer.

It's very slow on large files and uses too much memory, hence it was dropped.

This project utilizes a lot programming techniques such as unparsing & parsing of binary files, memory streams, pattern matching through map lookups, binary search, LZSS-like compression, bit masks and bit shifting, and parallelism.