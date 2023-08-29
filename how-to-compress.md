## Decompression:

1- Create a new array to hold the decompressed data. The size of the decompressed data can be found in bytes 6 to 9 in the compression header (big endian), or in the directory of compressed entries (4-bytes little endian).

2- Read the first control character.

3- Read additional control characters based on the value of the first control character

4- Use bit operations to get information about the number of bytes to copy as is, the offset, and the number of bytes to copy from the offset.

5- Copy the number of bytes specified to be copied as is from the compressed data to the decompressed data.

6- Go to the back to the offset in the decompressed data and copy the number of bytes specified to the end

Note: Step 6 has to be done one byte at a time, otherwise you will get problem if the offset is less than the length (The compression allows this).

-----

The three variable that we need to extract from the control characters are:

literal/plain = The number of bytes to copy from compressed data as is.

offset = The offset in the decompressed data to copy bytes from (i.e. current position - offset).

copy = The number of bytes to copy from the offset in the decompressed data

#### Control Characters:

#### Short (0x00 - 0x7F)

Has two control characters b0 and b1. Their bits are listed in order below.

The following bit operations can be applied to get the numbers that we need:

```C
//Bits: 0oocccpp oooooooo

plain = b0 & 0x03 //0-3 (mask off)
copy = ((b0 & 0x1C) >> 2) + 3 //3-11 ((mask off b0 & shift right) + minimum 3)
offset: ((b0 & 0x60) << 3) + b1 + 1 //1-1024 ((mask off b0 & shift left) + b1 + minimum 1)
```

#### Medium (0x80 - 0xBF)

Has three control characters.

```C
//Bits: 10cccccc ppoooooo oooooooo

plain = ((b1 & 0xC0) >> 6 //0-3 (mask off b1 & shift right)
copy = (b0 & 0x3F) + 4 //4-67 (mask off b1 + minimum 4)
offset: ((b1 & 0x3F) << 8) + b2 + 1 //1-16384 ((mask off b1 & shift left) + b2 + minimum 1)
```

#### Long (0xC0 - 0xDF)

Has four control characters.

```C
//Bits: 110occpp oooooooo oooooooo cccccccc

plain = b0 & 0x03 //0-3 (mask off)
copy = ((b0 & 0x0C) << 6) + b3 + 5 //5-1028 ((mask off b0 & shift left) + b3 + minimum 5)
Copy offset: ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1 //1-131072 ((mask off b0 & shift left) + (shift b1 left) + b2 + minimum 1)
```
#### Literal (0xE0 - 0xFB)

Has only one control character. This one only involves literal/plain copy with no copying from the offset. It's the most confusing.

```C
//Bits: 111ppppp

plain = ((b0 & 0x1F) << 2) + 4 //4-112 ((mask off b0 & shift left) + minimum 4)
copy = 0
offset = 0
```

#### EOF (0xFD - 0xFF)

Has only one control character. This one is usually used for the end of the compressed data. Although there is nothing preventing it from appearing elsewhere.

```C
//Bits: Bits: 111111pp

plain = b0 & 0x03 //0-3 (mask off b0)
copy = 0
offset = 0
```

## Compression

Under Construction.
