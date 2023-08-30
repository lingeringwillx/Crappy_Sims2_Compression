## Decompression:

1- Create a new array to hold the decompressed data. The size of the decompressed data can be found in bytes 6 to 9 in the compression header (big endian), or in the directory of compressed entries (4-bytes little endian).

2- Read the first control character.

3- Read additional control characters based on the value of the first control character

4- Use bit operations to get information about the number of bytes to copy as is, the offset, and the number of bytes to copy from the offset.

5- Copy the number of bytes specified to be copied as is from the compressed data to the decompressed data.

6- Go back to the offset in the decompressed data and copy the number of bytes specified to the end.

Note: The copying in step 6 has to be done one byte at a time, otherwise you will get a problem if the offset is less than the length (The compression allows this).

**The three variables that we need to extract from the control characters are:**

**literal/plain** = The number of bytes to copy from the compressed data as is.

**offset** = The offset in the *decompressed* data to copy the bytes from (i.e. current position - offset).

**count** = The number of bytes to copy from the offset in the *decompressed* data

Typical structure of decompressed data:

When there is an offset copy: `literals_from_compressed_data  bytes_from_offset  literals_from_compressed_data  bytes_from_offset...`

When copying plainly: `literals_from_compressed_data  literals_from_compressed_data ...`

#### Control Characters:

#### Short (0x00 - 0x7F)

Has two control characters b0 and b1. Their bits are listed in order below.

The following bit operations can be applied to get the numbers that we need:

```C
//Bits: 0oocccpp oooooooo

plain = b0 & 0x03 //0-3 (mask off)
count = ((b0 & 0x1C) >> 2) + 3 //3-11 ((mask off & shift right b0) + 3)
offset: ((b0 & 0x60) << 3) + b1 + 1 //1-1024 ((mask off & shift left b0) + b1 + 1)
```

#### Medium (0x80 - 0xBF)

Has three control characters.

```C
//Bits: 10cccccc ppoooooo oooooooo

plain = ((b1 & 0xC0) >> 6 //0-3 (mask off & shift right b1)
count = (b0 & 0x3F) + 4 //4-67 (mask off b1 + 4)
offset = ((b1 & 0x3F) << 8) + b2 + 1 //1-16384 ((mask off & shift left b1) + b2 + 1)
```

#### Long (0xC0 - 0xDF)

Has four control characters.

```C
//Bits: 110occpp oooooooo oooooooo cccccccc

plain = b0 & 0x03 //0-3 (mask off)
count = ((b0 & 0x0C) << 6) + b3 + 5 //5-1028 ((mask off & shift left b0) + b3 + 5)
offset: ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1 //1-131072 ((mask off & shift left b0) + (shift left b1) + b2 + 1)
```
#### Literal (0xE0 - 0xFB)

Has only one control character. This one only involves literal/plain copy with no copying from the offset.

```C
//Bits: 111ppp00

plain = ((b0 & 0x1F) << 2) + 4 //4-112 ((mask off & shift left b0) + 4)
count = 0
offset = 0
```

#### EOF (0xFD - 0xFF)

Has only one control character. This is used for the remaining portion of the compressed data at the end.

```C
//Bits: Bits: 111111pp

plain = b0 & 0x03 //0-3 (mask off b0)
copy = 0
offset = 0
```

## Compression

1- Create an array to hold the compressed data. The size of the array is initially going to be about the same as the original uncompressed data.

Note: For small files, the compressed data could end up being longer than the uncompressed data (9 bytes header + 1 control character + no reduced size from compression). You would need to account for this either by creating an array that's slighty longer than the uncompressed data, or by simply discarding the compressed output and keeping the file uncompressed.

2- Find common patterns within the file. There isn't one way to achieve this, but rather a variey of ways. A common way is to create a map/dicationary containing the patterns and their respective locations.

3- Loop over the file and check the pattern in the current location against the previous locations. Find the longest matching pattern, or at least find a match of a good length. The match has to be within the length and offset boundaries specified by the compression algorithm.

4- Once you found a match, add literal control characters a long with their bytes to the compressed data until you've reached the location of the match, then convert the length and offset of the match to control characters and add them to the compressed output.

Note: Start at byte 9 to leave room for the compression header.

5- Repeat this until you've added the last match. After that add literal and EOF control characters along with their respective bytes until you reach the end of the uncompressed data.

6- Write the compression header to the first 9 bytes of the compressed data.

7- Slice the array to the size that you've got after compression.

Typical structure of compressed data: `control_characters literals_from_decompressed_data  control_characters  literals_from_decompressed_data...`

#### Control Characters

Note: In most modder code the subtraction for the offset (offset - 1) is done before the bit operations.

#### Short

The minimum length for the matching pattern should be 3 (otherwise this compression is not useful).

```C
//Bits: 0oocccpp oooooooo

// 0 <= plain <= 3, 3 <= count <= 10, 1 <= offset <= 1024
b0 = (((offset - 1) >> 3) & 0x60) | ((count - 3) << 2) | plain // (shift right & mask off (offset - 1)) + (shift left (count - 3))
b1 = offset - 1 // first 8 bits of (offset - 1)
```

#### Medium

The minimum length for the matching pattern should be 4 (otherwise this compression is not useful).

```C
//Bits: 10cccccc ppoooooo oooooooo

// 0 <= plain <= 3, 4 <= count <= 67, 1 <= offset <= 16384
b0 = 0x80 | (count - 4) // mask on bit 1 + (count - 4)
b1 = (plain << 6) | ((offset - 1) >> 8) // shift plain left + shift (offset - 1) right
b2 = offset - 1 // first 8 bits of (offset - 1)
```

#### Long

The minimum length for the matching pattern should be 5 (otherwise this compression is not useful).

```C
//Bits: 110occpp oooooooo oooooooo cccccccc

// 0 <= plain <= 3, 5 <= count <= 1028, 1 <= offset <= 131072
b0 = 0xC0 | (((offset - 1) >> 12) & 0x10) | ((count - 5) >> 6) & 0x0C) | plain // (mask on bit 1-2 + (shift right & mask off (offset - 1)) + (shift right & mask off (count - 5)) + plain
b1 = (offset - 1) >> 8 // shift right (offset - 1)
b2 = (offset - 1) // first 8 bits of (offset - 1)
b3 = count - 5 // first 8 bits of (count - 5)
```
#### Literal

Copy as is from the uncompressed data without compression. Can be added multiple times until you're 1-3 bytes behind the location of a match.

Note: This must be a multiple of four due to the 2 bit right shift truncating the last two bits.

```C
//Bits: 111ppp00

// 4 <= plain <= 112, count = 0, offset = 0
b0 = 0xE0 | ((plain - 4) >> 2) // mask on bits 1-3 + (shift right (plain - 4))
```

#### EOF

Added to the end of the compressed data if you still need to add 1-3 bytes to be copied as is.

```C
//Bits: Bits: 111111pp

// 0 <= plain <= 3, count = 0, offset = 0
b0 = 0xFC | plain // mask on bits 1-6
```
