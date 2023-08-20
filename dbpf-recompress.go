package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"os"
	"runtime"
	"time"
)

//----------
// Section: Required parsing/unparsing functions
//----------

//convert 4 bytes to a 32-bit unsigned little endian integer
func unpackInt32le(b []byte) int {
	return int(binary.LittleEndian.Uint32(b))
}

//convert a 32-bit unsigned integer to its byte representation
func packInt32le(n int) []byte {
	b := make([]byte, 4)
	binary.LittleEndian.PutUint32(b, uint32(n))
	return b
}

//convert 3 bytes to a 24-bit unsigned big endian integer
func unpackInt24bg(b []byte) int {
	b2 := make([]byte, 1, 4)
	b2 = append(b2, b...)
	return int(binary.BigEndian.Uint32(b2))
}

//convert a 24-bit unsigned integer to its byte representation
func packInt24bg(n int) []byte {
	b := make([]byte, 4)
	binary.BigEndian.PutUint32(b, uint32(n))
	return b[1:4]
}

//----------
// Section: Stream struct definition
//----------

type Stream struct {
	buf []byte
	pos int
}

//constructor
func newStream(buffer []byte) Stream {
	return Stream{buffer, 0}
}

func (stream *Stream) Read(size int) []byte {
	if stream.pos + size > len(stream.buf)  {
		size = len(stream.buf) - stream.pos
	}
	
	b := stream.buf[stream.pos:stream.pos + size]
	stream.pos += size
	
	return b
}

func (stream *Stream) Write(b []byte) {
	var i int
	for i = 0; i < len(b) && stream.pos + i < len(stream.buf); i++ {
		stream.buf[stream.pos + i] = b[i]
	}
	
	stream.buf = append(stream.buf, b[i:]...)
	stream.pos += len(b)
}

//read a 32-bit unsigned little endian integer from the stream
func (stream *Stream) ReadInt32le() int {
	return unpackInt32le(stream.Read(4))
}

//write a 32-bit unsigned little endian integer to the stream
func (stream *Stream) WriteInt32le(n int) {
	stream.Write(packInt32le(n))
}

//read a 24-bit unsigned big endian integer from the stream
func (stream *Stream) ReadInt24bg() int {
	return unpackInt24bg(stream.Read(3))
}

//write a 24-bit unsigned big endian integer to the stream
func (stream *Stream) WriteInt24bg(n int) {
	stream.Write(packInt24bg(n))
}

//----------
// Section: DBPF Package Code
//----------

//the word package is used by golang
type Pack struct {
	indexVersion int
	entries []Entry
}

type Entry struct {
	Stream
	typeId int
	groupId int
	instanceId int
	resourceId int
	location int
	compressed bool
	repeated bool //entry appears in package file more than one time, there shouldn't be more than one compressed entry with the same TGIR
}

//constructor
func newEntry(content []byte, typeId int, groupId int, instanceId int, resourceId int) Entry {
	return Entry{newStream(content), typeId, groupId, instanceId, resourceId, 0, false, false}
}

//compress the entry if it's not compressed, otherwise do nothing
func (entry *Entry) compress() {
	if !entry.compressed && !entry.repeated {
		content, success := compress(entry.buf)
		if success {
			entry.buf = content
			entry.compressed = true
		}
	}
}

//decompress the entry if it's not already decompressed, otherwise do nothing
func (entry *Entry) decompress() {
	if entry.compressed {
		content, success := decompress(entry.buf)
		if success {
			entry.buf = content
			entry.compressed = false
		}
	}
}

//for holding the data of the directory of compressed files
type CompressedEntry struct {
	typeId int
	groupId int
	instanceId int
	resourceId int
}

func unpackPackage(b []byte) Pack {
	//read header
	stream := newStream(b)
	stream.pos = 36
	
	indexEntryCount := stream.ReadInt32le()
	indexLocation := stream.ReadInt32le()
	
	stream.pos += 16
	indexVersion := stream.ReadInt32le()
	
	//read index
	stream.pos = indexLocation
	entries := make([]Entry, 0, indexEntryCount)
	
	clst := Entry{}
	clstFound := false
	for i := 0; i < indexEntryCount; i++ {
		typeId := stream.ReadInt32le()
		groupId := stream.ReadInt32le()
		instanceId := stream.ReadInt32le()
		
		resourceId := 0
		if indexVersion == 2 {
			resourceId = stream.ReadInt32le()
			
		location := stream.ReadInt32le()
		size := stream.ReadInt32le()
		
		oldPos := stream.pos
		
		stream.pos = location
		content := stream.Read(size)
		
		if typeId == 0xE86B1EEF {
			clst = newEntry(content, typeId, groupId, instanceId, resourceId)
			clstFound = true
			
		} else {
			entries = append(entries, newEntry(content, typeId, groupId, instanceId, resourceId))
		}
		
		stream.pos = oldPos
		}
	}
	
	if clstFound {
		//read CLST 
		compressedEntries := make(map[CompressedEntry]bool)
		for clst.pos < len(clst.buf) {
			typeId := clst.ReadInt32le()
			groupId := clst.ReadInt32le()
			instanceId := clst.ReadInt32le()
			
			resourceId := 0
			if indexVersion == 2 {
				resourceId = clst.ReadInt32le()
			}
			
			compressedEntry := CompressedEntry{typeId, groupId, instanceId, resourceId}
			compressedEntries[compressedEntry] = true
			
			clst.pos += 4
		}
		
		//check if each entry is compressed
		for i := 0; i < len(entries); i++ {
			_, found := compressedEntries[CompressedEntry{entries[i].typeId, entries[i].groupId, entries[i].instanceId, entries[i].resourceId}]
			
			if found {
				entries[i].pos = 4
				if bytes.Equal(entries[i].Read(2), []byte{0x10, 0xFB}) {
					entries[i].compressed = true
				}
			}
		}
	}
	
	//Check for repeated entries
	//using CompressedEntry for any entry even if not compressed, will only be used for lookups
	entriesMap := make(map[CompressedEntry]int)
	for i := 0; i < len(entries); i++ {
		entry := CompressedEntry{entries[i].typeId, entries[i].groupId, entries[i].instanceId, entries[i].resourceId}
		j, found := entriesMap[entry]
		
		if found {
			entries[i].repeated = true
			entries[j].repeated = true
		}
		
		entriesMap[entry] = i
	}
	
	return Pack{indexVersion, entries}
}

func packPackage(pack *Pack) []byte {
	//write header
	stream := newStream([]byte{})
	stream.Write([]byte("DBPF"))
	stream.WriteInt32le(1)
	stream.WriteInt32le(1)
	stream.WriteInt32le(0)
	stream.WriteInt32le(0)
	stream.WriteInt32le(0)
	stream.WriteInt32le(0)
	stream.WriteInt32le(0)
	stream.WriteInt32le(7)
	stream.WriteInt32le(0) //entry count, update later
	stream.WriteInt32le(0) //index location, update later
	stream.WriteInt32le(0) //index size, update later
	stream.WriteInt32le(0)
	stream.WriteInt32le(0)
	stream.WriteInt32le(0)
	stream.WriteInt32le(pack.indexVersion)
	stream.Write(make([]byte, 32))
	
	//check if package has compressed entries
	hasCompressedEntries := false
	for i := 0; i < len(pack.entries); i++ {
		if pack.entries[i].compressed {
			hasCompressedEntries = true
			break
		}
	}
	
	//make new CLST
	if hasCompressedEntries {
		clst := newEntry([]byte{}, 0xE86B1EEF, 0xE86B1EEF, 0x286B1F03, 0)
		for i := 0; i < len(pack.entries); i++ {
			clst.WriteInt32le(pack.entries[i].typeId)
			clst.WriteInt32le(pack.entries[i].groupId)
			clst.WriteInt32le(pack.entries[i].instanceId)
			
			if pack.indexVersion == 2 {
				clst.WriteInt32le(pack.entries[i].resourceId)
			}
			
			pack.entries[i].pos = 6
			uncompressedSize := pack.entries[i].ReadInt24bg()
			clst.WriteInt32le(uncompressedSize)
		}
		
		pack.entries = append(pack.entries, clst)
	}
	
	//write entries
	for i := 0; i < len(pack.entries); i++ {
		pack.entries[i].location = stream.pos
		stream.Write(pack.entries[i].buf)
	}
	
	//write index
	indexStart := stream.pos
	
	for i := 0; i < len(pack.entries); i++ {
		stream.WriteInt32le(pack.entries[i].typeId)
		stream.WriteInt32le(pack.entries[i].groupId)
		stream.WriteInt32le(pack.entries[i].instanceId)
		
		if pack.indexVersion == 2 {
			stream.WriteInt32le(pack.entries[i].resourceId)
		}
		
		stream.WriteInt32le(pack.entries[i].location)
		stream.WriteInt32le(len(pack.entries[i].buf))
	}
	
	indexEnd := stream.pos
	
	//update header
	stream.pos = 36
	stream.WriteInt32le(len(pack.entries))
	stream.WriteInt32le(indexStart)
	stream.WriteInt32le(indexEnd - indexStart)
	
	return stream.buf
}

//----------
// Section: Compression Utils
//----------

//copy length bytes from src starting from srcPos to dst at dstPos
func copyBytes(src []byte, srcPos int, dst []byte, dstPos int, length int) {
	//copying 1 byte at a time is required for the compression and decompression to work, i.e. when src = dst and the pos is close to the end
	for i := 0; i < length; i++ {
		dst[dstPos + i] = src[srcPos + i]
	}
}

//to hold information on repeated patterns found in bytes
type Match struct {
	location int
	length int
	offset int //offset where match could be found, i.e. location - offset
}

//for profiling
type Timer struct {
	sum time.Duration
	t time.Time
}

func (timer *Timer) start() {
	timer.t = time.Now()
}

func (timer *Timer) stop() {
	timer.sum += time.Now().Sub(timer.t)
}

//----------
// Section: Compression Code
//----------

//returns compressed output if successful
func compress(src []byte) ([]byte, bool) {
	
	//----------
	// Section: Pattern Matching
	//----------

	//3 byte dictionary for fast lookups of patterns, values are a list of the locations where the pattern could be found
	dict := make(map[[3]byte][]int)
	
	var pattern [3]byte
	for i := 0; i <= len(src) - 3; i++ {
		pattern = [3]byte(src[i:i + 3])
		dict[pattern] = append(dict[pattern], i)
	}
	
	var longestMatches []Match
	
	//search for the longest matches
	for i := 1; i <= len(src) - 3; i++ {
		locations := dict[[3]byte(src[i:i + 3])]
		
		if len(locations) > 1 { //if the pattern exists in more than one place
			longestMatch := Match{}
			
			//binary search to find the place with the 3 byte pattern that's above the maximum possible offset
			startIndex := 0
			minIndex := i - 131072
			
			if minIndex > 0 && locations[0] < minIndex {
				high := len(locations) - 1
				low := 0 
				
				for low < high {
					startIndex = (high + low) / 2
					
					if locations[startIndex] > minIndex {
						high = startIndex
					} else {
						low = startIndex + 1
					}
				}
				
				startIndex = high
			}
			
			matchFound := false
			//check all of the patterns before the current location and find out which one is the longest
			for index := startIndex; index < len(locations) && locations[index] < i; index++ {
				//i = the current location, the next 3 bytes are the pattern that we are looking up
				//j = a location where the same 3 bytes could be found
				//k = increment to add to both i and j to check if later bytes match
				
				j := locations[index]
				//location, length, offset
				match := Match{i, 3, i - j}
				
				//find out how long the pattern is
				for k := 3; k < len(src) - i; k++ {
					if src[i + k] == src[j + k] && match.length < 1028 {
						match.length++
					} else {
						break
					}
				}
				
				if match.length >= longestMatch.length && (match.offset <= 1024 || (match.offset <= 16384 && match.length >= 4) || (match.offset <= 131072 && match.length >= 5)) {
					longestMatch = match //current longest match for this pattern
					matchFound = true
					
					//no need to keep going if we found a pattern with the largest possible length
					if longestMatch.length == 1028 {
						break
					}
				}
				
				//future matches will be smaller so quit
				if i + match.length == len(src) {
					break
				}
			}
			
			//if we found a match
			if matchFound {
				longestMatches = append(longestMatches, longestMatch)
				
				//jump to after the pattern, no need to find matches for bytes within the pattern
				//for loop adds 1 on its own so we subtract one here
				i += longestMatch.length - 1 
			}
		}
	}
	
	/*for i := 0; i < len(longestMatches); i++ {
		fmt.Println(longestMatches[i])
	}*/
	
	//----------
	// Section: Actual Compression
	//----------
	
	//compress src
	bufferSize := len(src) - 1
	
	//maximum possible size of the compressed entry due to the fact that the compressed size in the header is only 3 bytes long
	if bufferSize > 16777215 {
		bufferSize = 16777215
	}
	
	dst := make([]byte, bufferSize)  //dst should be smaller than src, otherwise there is no advantage to compression
	srcPos := 0
	dstPos := 9
	
	//plain = number of bytes to copy from src as is with no compression
	//copy = number of bytes to compress
	//offset = offset from current location in decompressed input to where the pattern could be found
	
	for i := 0; i < len(longestMatches); i++ {
		//copy bytes from src to dst until the location of the match is reached
		plain := longestMatches[i].location - srcPos
		for plain > 3 {
			//this can only be a multiple of 4, due to the 2-bit right shift in the control character
			plain -= plain % 4
			
			//max possible value is 128
			if plain > 128 {
				plain = 128
			}
			
			if dstPos + plain + 1 > len(dst) {
				return []byte{}, false
			}
			
			// 111ppppp
			b0 := byte(0XE0 + plain >> 2 - 1)
			
			dst[dstPos] = b0
			dstPos++
			
			//add the characters that need to be copied
			copyBytes(src, srcPos, dst, dstPos, plain)
			srcPos += plain
			dstPos += plain
			
			plain = longestMatches[i].location - srcPos
		}
		
		copy := longestMatches[i].length
		offset := longestMatches[i].offset - 1 //subtraction is a part of the transformation for the offset, I think...
		var b0, b1, b2, b3 byte //control characters containing encoded plain, copy, and offset
		
		//apply weird QFS transformations
		
		// 0oocccpp oooooooo
		if copy <= 10 && offset < 1024 {
			if dstPos + plain + 2 > len(dst) {
				return []byte{}, false
			}
			
			b0 = byte((offset >> 3 & 0x60) + ((copy - 3) << 2) + plain)
			b1 = byte(offset)
			
			dst[dstPos] = b0
			dst[dstPos + 1] = b1
			dstPos += 2
			
		// 10cccccc ppoooooo oooooooo
		} else if copy <= 67 && offset < 16384 {
			if dstPos + plain + 3 > len(dst) {
				return []byte{}, false
			}
			
			b0 = byte(0x80 + (copy - 4))
			b1 = byte(plain << 6 + offset >> 8)
			b2 = byte(offset)
			
			dst[dstPos] = b0
			dst[dstPos + 1] = b1
			dst[dstPos + 2] = b2
			dstPos += 3
			
		// 110occpp oooooooo oooooooo cccccccc
		} else if copy <= 1028 && offset < 131072 {
			if dstPos + plain + 4 > len(dst) {
				return []byte{}, false
			}
			
			b0 = byte(0xC0 + (offset >> 12 & 0x10) + ((copy - 5) >> 6 & 0x0C) + plain)
			b1 = byte(offset >> 8)
			b2 = byte(offset)
			b3 = byte(copy - 5)
			
			dst[dstPos] = b0
			dst[dstPos + 1] = b1
			dst[dstPos + 2] = b2
			dst[dstPos + 3] = b3
			dstPos += 4
		}
		
		if plain > 0 {
			copyBytes(src, srcPos, dst, dstPos, plain)
			srcPos += plain
			dstPos += plain
		}
		
		srcPos += copy
	}
	
	//copy the remaining bytes at the end
	plain := len(src) - srcPos
	for plain > 3 {
		plain -= plain % 4
		
		if plain > 128 {
			plain = 128
		}
		
		if dstPos + plain + 1 > len(dst) {
			return []byte{}, false
		}
		
		// 111ppppp
		b0 := byte(0XE0 + plain >> 2 - 1)
		
		dst[dstPos] = b0
		dstPos++
		
		copyBytes(src, srcPos, dst, dstPos, plain)
		srcPos += plain
		dstPos += plain
		
		plain = len(src) - srcPos
	}
	
	if plain > 0 {
		if dstPos + plain + 1 > len(dst) {
			return []byte{}, false
		}
		
		// 111111pp
		b0 := byte(plain + 0xFC)
		
		dst[dstPos] = b0
		dstPos++
		
		copyBytes(src, srcPos, dst, dstPos, plain)
		srcPos += plain
		dstPos += plain
	}
	
	//make QFS compression header
	copyBytes(packInt32le(dstPos), 0, dst, 0, 4) //compressed size, little endian
	copyBytes([]byte{0x10, 0xFB}, 0, dst, 4, 2) // 0x10FB
	copyBytes(packInt24bg(len(src)), 0, dst, 6, 3) //uncompressed size, big endian
	
	return dst[:dstPos], true
}

//----------
// Section: Decompression
//----------

//decompress src
func decompress(src []byte) ([]byte, bool) {
	uncompressedSize := unpackInt24bg(src[6:9])
	
	dst := make([]byte, uncompressedSize)
	srcPos := 9
	dstPos := 0
	
	var b0, b1, b2, b3 int //control characters
	var copy, offset, plain int
	
	for srcPos < len(src) {
		if srcPos + 1 > len(src) {
			return []byte{}, false
		}
		
		b0 = int(src[srcPos])
		srcPos++
		
		if b0 < 0x80 {
			if srcPos + 1 > len(src) {
				return []byte{}, false
			}
			
			b1 = int(src[srcPos])
			srcPos++
			
			plain = b0 & 0x03 //0-3
			copy = ((b0 & 0x1C) >> 2) + 3 //3-10
			offset = ((b0 & 0x60) << 3) + b1 + 1 //1-1024
			
		} else if b0 < 0xC0 {
			if srcPos + 2 > len(src) {
				return []byte{}, false
			}
			
			b1 = int(src[srcPos])
			b2 = int(src[srcPos + 1])
			srcPos += 2
			
			plain = (b1 & 0xC0) >> 6 //0-3
			copy = (b0 & 0x3F) + 4 //4-67
			offset = ((b1 & 0x3F) << 8) + b2 + 1 //1-16384
			
		} else if b0 < 0xE0 {
			if srcPos + 3 > len(src) {
				return []byte{}, false
			}
			
			b1 = int(src[srcPos])
			b2 = int(src[srcPos + 1])
			b3 = int(src[srcPos + 2])
			srcPos += 3
			
			plain = b0 & 0x03 //0-3
			copy = ((b0 & 0x0C) << 6) + b3 + 5 //5-1028
			offset = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1 //1-131072
			
		} else if b0 < 0xFC {
			plain = (b0 & 0x1F) << 2 + 4 //4-128
			copy = 0
			offset = 0
			
		} else {
			plain = b0 - 0xFC //0-3
			copy = 0
			offset = 0
		}
		
		if srcPos + plain > len(src) || dstPos + plain + copy > len(dst) {
			return []byte{}, false
		}
		
		copyBytes(src, srcPos, dst, dstPos, plain)
		srcPos += plain
		dstPos += plain
		
		//copy bytes from an earlier location in the decompressed output
		copyBytes(dst, dstPos - offset, dst, dstPos, copy)
		dstPos += copy
	}
	
	return dst, true
}

//----------
// Section: Main Program
//----------

func parallelCompress(entries []Entry, ch chan []Entry) {
	for i := 0; i < len(entries); i++ {
		entries[i].decompress()
		entries[i].compress()
	}
	
	ch <- entries
}

func main() {
	file, _ := os.ReadFile(os.Args[1])
	pack := unpackPackage(file)
	
	start := time.Now()
	
	//non-parallel compression
	/*for i := 0; i < len(pack.entries); i++ {
		pack.entries[i].decompress()
		pack.entries[i].compress()
	}*/
	
	ch := make(chan []Entry, runtime.NumCPU())
	
	//divide entries into a number of portions equal to the number of cores in the system
	totalLength := 0
	for i := 0; i < len(pack.entries); i++ {
		totalLength += len(pack.entries[0].buf)
	}
	
	partitionLength := totalLength / runtime.NumCPU()
	i := 0
	j := 0
	
	//begin parallel compression
	for nCores := 0; nCores < runtime.NumCPU() - 1; nCores++ {
		length := 0
		for j < len(pack.entries) {
			length += len(pack.entries[0].buf)
			
			if length >= partitionLength {
				break
			}
			
			j++
		}
		
		go parallelCompress(pack.entries[i:j], ch)
		
		i = j
	}
	
	//put the all remaining entries here
	go parallelCompress(pack.entries[i:], ch)
	
	for i := 0; i < runtime.NumCPU(); i++ {
		<- ch
	}
	
	fmt.Println(time.Now().Sub(start))
	
	os.WriteFile("test.package", packPackage(&pack), 'w')
}