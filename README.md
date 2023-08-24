#### Sims 2 Compression Experiments

<img src="https://github.com/lingeringwillx/CrappySims2Compression/assets/111698406/5e1e045d-ab02-48c0-9a69-f8fb5ab57cbc" width="400">

<br/>Current build can be compiled with Visual C++ Build Tools. Run `compile.bat` to compile.

Currently not memory efficient.

Use: `dbpf-recompress -args package_file`

Benchmark: `python benchmark.py package_file`

#### Parameters

| Flag | Behavior | Description
| - | - | - |
| `-l` | Compression Level | Can be `-l1`, `-l3`, `-l5`, `-l7`, or `-l9`. Higher values mean stronger but slower compression |
| `-r` | Recompress | Decompress the file then compress it again, can provide a bigger compression if the older compression is weak, but will slow down the compression |
| `-p` | Parallel | Use all cores for compresson, will speed up the compression but will use 100% of CPU during compression |
| `-d` | Decompress | Decompress without compression, if flag `-r` is also set then the file will also be compressed again |

### Resources
[Niotso Wiki](http://wiki.niotso.org/RefPack): Generic information on the compression used by some old EA games.

[Explantion of The LZ77/LZSS Compression](https://go-compression.github.io/algorithms/lzss/): QFS/Refpack is based on this compression.

[Interactive LZ77 Encoder/Decoder](https://go-compression.github.io/interactive/lz/lz/)

[DBPF Package Information](https://modthesims.info/wiki.php?title=DBPF): Contains information on the game's .package format.

[ModTheSims](https://modthesims.info/wiki.php?title=DBPF/Compression): Likely has some subtle mistakes here and there but still a good resource.

[Implementation by @actionninja](https://github.com/actioninja/refpack-rs): Has a lot of information on the compression algorithm.

[Old Mystical Implementation by BenK](http://www.moreawesomethanyou.com/smf/index.php/topic,8279.0.html): Old hard to read implementation based on zlib code. This is the strongest version of the compression out there and it's the same one used by The Compressorizer.

[Implementation by @memo33](https://github.com/memo33/jDBPFX/blob/master/src/jdbpfx/util/DBPFPackager.java): This version is for SimCity 4 so there is likely some small differences between it and the one used by The Sims 2.

[Implementation by @LazyDuchess](https://github.com/LazyDuchess/OpenTS2/blob/master/Assets/Scripts/OpenTS2/Files/Formats/DBPF/DBPFCompression.cs): I don't know if this one works without issues.

[Explanation of zlib](https://www.euccas.me/zlib/): The Compressorizer's code is heavily based on this library.
