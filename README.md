### Crappy Sims 2 Compression

My own attempt at rewriting the compression code for The Sims 2. The non-parallel version is currently 3-4x times slower than the Compressorizer so it's not really useful. Not sure if it works perfectly on all package files.

It attempts to compress the file as much as possible. There is no limitations imposed on it to make it run faster. The compressed output is only a few kilobytes bigger than the compressorizer.

This project utilizes a lot programming techniques such as unparsing & parsing of binary files, memory streams, pattern matching through map lookups, binary search, LZSS-like compression, bit masks and bit shifting, and parallelism.

### Resources
[Niotso Wiki](http://wiki.niotso.org/RefPack): Generic information on the compression used by some old EA games.

[DBPF Package Information](https://modthesims.info/wiki.php?title=DBPF): Contains information on the game's .package format.

[ModTheSims](https://modthesims.info/wiki.php?title=DBPF/Compression): Likely has some subtle mistakes here and there but still a good resource.

[Implementation by @actionninja](https://github.com/actioninja/refpack-rs): Has a lot of information on the compression algorithm.

[Old Mystical Implementation by BenK](http://www.moreawesomethanyou.com/smf/index.php/topic,8279.0.html): Old hard to read implementation based on zlib code. This is the strongest version of the compression out there and it's the same one used by The Compressorizer.

[Implementation by @memo33](https://github.com/memo33/jDBPFX/blob/master/src/jdbpfx/util/DBPFPackager.java): This version is for SimCity 4 so there is likely some small differences between it and the one used by The Sims 2.

[Implementation by @LazyDuchess](https://github.com/LazyDuchess/OpenTS2/blob/master/Assets/Scripts/OpenTS2/Files/Formats/DBPF/DBPFCompression.cs): I don't know if this one works without issues


