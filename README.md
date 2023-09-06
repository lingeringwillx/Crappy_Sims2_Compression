#### Sims 2 Compression Experiments

<img src="https://github.com/lingeringwillx/CrappySims2Compression/assets/111698406/5e1e045d-ab02-48c0-9a69-f8fb5ab57cbc" width="400">

<br/>Current build can be compiled with Visual C++ Build Tools. Run `compile.bat` to compile.

Usage: `dbpf-recompress -args package_file`

Benchmark: `python benchmark.py package_file`

There is now an experimental release that could be used as a drop-in replacement for The Compressorizer's original executable. It achieves faster compression in the following ways:

1- Utilizing all of the cores of your PC for compression, so if you have a dual core CPU then compression will be roughly 2x faster, and if you have a quad core CPU then it will be 4x faster, etc.

2- By using level 5 compression parameters instead of level 9.

To use it, just download the .exe file and put it in the same directory as The Compressorizer, overwriting the old file.

Alternatively, you can just drag and drop your file or folder to the executable and it will be compressed.

#### Parameters

| Flag | Name | Description
| - | - | - |
| `-l` | Compression Level | Can be `-l1`, `-l3`, `-l5`, `-l7`, or `-l9`. Higher values mean stronger but slower compression |
| `-r` | Recompress | Decompress the file then compress it again, can provide a bigger compression if the older compression is weak, but will slow down the compression (Note: This is always enabled in the release version) |
| `-p` | Parallel | Use all cores for compresson, will speed up the compression but will use 100% of CPU during compression (Note: This is always enabled in the release version) |
| `-d` | Decompress | Decompress without compression |
| `-q` | Quiet | Don't output anything to the console except for errors |

### Resources
[Niotso Wiki](http://wiki.niotso.org/RefPack): Generic information on the compression used by some old EA games.

[Explanation of The LZ77/LZSS Compression](https://go-compression.github.io/algorithms/lzss/): QFS/Refpack is based on this compression.

[Interactive LZ77 Encoder/Decoder](https://go-compression.github.io/interactive/lz/lz/)

[DBPF Package Information](https://modthesims.info/wiki.php?title=DBPF): Contains information on the game's .package format.

[ModTheSims](https://modthesims.info/wiki.php?title=DBPF/Compression): Likely has some subtle mistakes here and there but still a good resource.

[Rust implementation by @actionninja](https://github.com/actioninja/refpack-rs): Has a lot of information on the compression algorithm.

[Old Mystical C Implementation by BenK](http://www.moreawesomethanyou.com/smf/index.php/topic,8279.0.html): Old hard to read implementation based on zlib code. This is the strongest version of the compression out there and it's the same one used by The Compressorizer.

[Java implementation by @memo33](https://github.com/memo33/jDBPFX/blob/master/src/jdbpfx/util/DBPFPackager.java)

[C# implementation by @0xC0000054](https://github.com/0xC0000054/DBPFSharp/blob/main/src/DBPFSharp/QfsCompression.cs)

[Explanation of zlib](https://www.euccas.me/zlib/): The Compressorizer's code is heavily based on this library.
