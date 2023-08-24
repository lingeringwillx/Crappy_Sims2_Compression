#include <unordered_map>
#include <vector>

using namespace std;

typedef unsigned int uint;
typedef vector<unsigned char> bytes;

bytes cCompress(bytes& src);
bytes cDecompress(bytes& src);

void copyBytes(bytes& src, uint srcPos, bytes& dst, uint dstPos, uint length) {
	for(uint i = 0; i < length; i++) {
		dst[dstPos + i] = src[srcPos + i];
	}
}

uint dictHash(bytes& buf, uint pos) {
	uint n = buf[pos] << 16;
	n += buf[pos + 1] << 8;
	n += buf[pos + 2];
	return n;
}

struct Match {
	uint location;
	uint length;
	uint offset;
};

bytes cCompress(bytes& src) {

	//----------
	// Section: Pattern Matching
	//----------

	//3 byte dictionary for fast lookups of patterns, values are a list of the locations where the pattern could be found
	unordered_map<uint, vector<uint>> dict;
	
	for (uint i = 0; i <= src.size() - 3; i++) {
		uint hash = dictHash(src, i);
		
		if(dict.find(hash) == dict.end()) {
			dict[hash] = vector<uint>{};
		}

		dict[hash].push_back(i);
	}

	vector<Match> longestMatches;
	
	//search for the longest matches
	for (uint i = 1; i <= src.size() - 3; i++) {
		vector<uint> locations = dict[dictHash(src, i)];

		if(locations.size() > 1) { //if the pattern exists in more than one place
			Match longestMatch = Match{0, 0, 0};
			
			//binary search to find the place with the 3 byte pattern that's above the maximum possible offset
			uint startIndex = 0;
			uint minIndex = 0;

			if(i > 131072) {
				minIndex = i - 131072;
			}
			
			if(minIndex > 0 && locations[0] < minIndex) {
				uint high = locations.size() - 1;
				uint low = 0;
				
				while(low < high) {
					startIndex = (high + low) / 2;
					
					if(locations[startIndex] > minIndex) {
						high = startIndex;
					} else {
						low = startIndex + 1;
					}
				}
				
				startIndex = high;
			}
			
			bool matchFound = false;
			//check all of the patterns before the current location and find out which one is the longest
			for(uint index = startIndex; index < locations.size() && locations[index] < i; index++) {
				//i = the current location, the next 3 bytes are the pattern that we are looking up
				//j = a location where the same 3 bytes could be found
				//k = increment to add to both i and j to check if later bytes match
				
				uint j = locations[index];
				uint length = 3;
				uint offset = i - j;
				
				//find out how long the pattern is
				for(uint k = 3; k < src.size() - i; k++) {
					if(src[i + k] == src[j + k] && length < 1028) {
						length++;
					} else {
						break;
					}
				}
				
				if(length >= longestMatch.length && (offset <= 1024 || (offset <= 16384 && length >= 4) || (offset <= 131072 && length >= 5))) {
					longestMatch = Match{i, length, offset}; //current longest match for this pattern
					matchFound = true;
					
					//no need to keep going if we found a pattern with the largest possible length
					if(longestMatch.length == 1028) {
						break;
					}
				}
				
				//future matches will be smaller so quit
				if(i + length == src.size()) {
					break;
				}
			}
			
			//if we found a match
			if(matchFound) {
				longestMatches.push_back(longestMatch);
				
				//jump to after the pattern, no need to find matches for bytes within the pattern
				//for loop adds 1 on its own so we subtract one here
				i += longestMatch.length - 1; 
			}
		}
	}
	
	/*for(uint i = 0; i < longestMatches.size(); i++) {
		cout << longestMatches[i].location << " " << longestMatches[i].length << " " << longestMatches[i].offset << endl;
	}*/
	
	//----------
	// Section: Actual Compression
	//----------
	
	//compress src
	//compressed output has to smaller than the decompressed output, otherwise it's not useful
	uint bufferSize = src.size() - 1;
	
	//maximum possible size of the compressed entry due to the fact that the compressed size in the header is only 3 bytes long
	if(bufferSize > 16777215) {
		bufferSize = 16777215;
	}
	
	bytes dst = bytes(bufferSize);

	uint srcPos = 0;
	uint dstPos = 9;
	
	//plain = number of bytes to copy from src as is with no compression
	//nCopy = number of bytes to compress
	//offset = offset from current location in decompressed input to where the pattern could be found
	
	for(uint i = 0; i < longestMatches.size(); i++) {
		//copy bytes from src to dst until the location of the match is reached
		uint plain = longestMatches[i].location - srcPos;
		while(plain > 3) {
			//this can only be a multiple of 4, due to the 2-bit right shift in the control character
			plain -= plain % 4;
			
			//max possible value is 112
			if(plain > 112) {
				plain = 112;
			}
			
			if(dstPos + plain + 1 > dst.size()) {
				return bytes(0);
			}
			
			// 111ppppp
			unsigned char b0 = 0XE0 + (plain >> 2) - 1;
			
			dst[dstPos] = b0;
			dstPos++;
			
			//add the characters that need to be copied
			copyBytes(src, srcPos, dst, dstPos, plain);
			srcPos += plain;
			dstPos += plain;
			
			plain = longestMatches[i].location - srcPos;
		}
		
		uint nCopy = longestMatches[i].length;
		uint offset = longestMatches[i].offset - 1; //subtraction is a part of the transformation for the offset, I think...
		unsigned char b0, b1, b2, b3; //control characters containing encoded plain, nCopy, and offset
		
		//apply weird QFS transformations
		
		// 0oocccpp oooooooo
		if(nCopy <= 10 && offset < 1024) {
			if(dstPos + plain + 2 > dst.size()) {
				return bytes(0);
			}
			
			b0 = (offset >> 3 & 0x60) + ((nCopy - 3) << 2) + plain;
			b1 = offset;
			
			dst[dstPos] = b0;
			dst[dstPos + 1] = b1;
			dstPos += 2;
			
		// 10cccccc ppoooooo oooooooo
		} else if(nCopy <= 67 && offset < 16384) {
			if(dstPos + plain + 3 > dst.size()) {
				return bytes(0);
			}
			
			b0 = 0x80 + (nCopy - 4);
			b1 = (plain << 6) + (offset >> 8);
			b2 = offset;
			
			dst[dstPos] = b0;
			dst[dstPos + 1] = b1;
			dst[dstPos + 2] = b2;
			dstPos += 3;
			
		// 110occpp oooooooo oooooooo cccccccc
		} else if(nCopy <= 1028 && offset < 131072) {
			if(dstPos + plain + 4 > dst.size()) {
				return bytes(0);
			}
			
			b0 = 0xC0 + (offset >> 12 & 0x10) + ((nCopy - 5) >> 6 & 0x0C) + plain;
			b1 = offset >> 8;
			b2 = offset;
			b3 = nCopy - 5;
			
			dst[dstPos] = b0;
			dst[dstPos + 1] = b1;
			dst[dstPos + 2] = b2;
			dst[dstPos + 3] = b3;
			dstPos += 4;
		}
		
		if(plain > 0) {
			copyBytes(src, srcPos, dst, dstPos, plain);
			srcPos += plain;
			dstPos += plain;
		}
		
		srcPos += nCopy;
	}
	
	//copy the remaining bytes at the end
	uint plain = src.size() - srcPos;
	while(plain > 3) {
		plain -= plain % 4;
		
		if(plain > 112) {
			plain = 112;
		}
		
		if(dstPos + plain + 1 > dst.size()) {
			return bytes();
		}
		
		// 111ppppp
		unsigned char b0 = 0XE0 + (plain >> 2) - 1;
		
		dst[dstPos] = b0;
		dstPos++;
		
		copyBytes(src, srcPos, dst, dstPos, plain);
		srcPos += plain;
		dstPos += plain;
		
		plain = src.size() - srcPos;
	}
	
	if(plain > 0) {
		if(dstPos + plain + 1 > dst.size()) {
			return bytes(0);
		}
		
		// 111111pp
		unsigned char b0 = plain + 0xFC;
		
		dst[dstPos] = b0;
		dstPos++;
		
		copyBytes(src, srcPos, dst, dstPos, plain);
		srcPos += plain;
		dstPos += plain;
	}
	
	//make QFS compression header
	uint pos = 0;
	putInt32le(dst, pos, dstPos);

	dst[4] = 0x10;
	dst[5] = 0xFB;

	pos = 6;
	putInt24bg(dst, pos, src.size());

	return bytes(dst.begin(), dst.begin() + dstPos);
}

//----------
// Section: Decompression
//----------

//decompress src
bytes cDecompress(bytes& src) {
	uint srcPos = 6;
	uint uncompressedSize = getInt24bg(src, srcPos);
	
	bytes dst = bytes(uncompressedSize);
	srcPos = 9;
	uint dstPos = 0;
	
	uint b0, b1, b2, b3; //control characters
	uint nCopy, offset, plain;
	
	while(srcPos < src.size()) {
		if(srcPos + 1 > src.size()) {
			return bytes(0);
		}
		
		b0 = src[srcPos];
		srcPos++;

		if(b0 < 0x80) {
			if(srcPos + 1 > src.size()) {
				return bytes(0);
			}
			
			b1 = src[srcPos];
			srcPos++;
			
			plain = b0 & 0x03; //0-3
			nCopy = ((b0 & 0x1C) >> 2) + 3; //3-10
			offset = ((b0 & 0x60) << 3) + b1 + 1; //1-1024
			
		} else if(b0 < 0xC0) {
			if(srcPos + 2 > src.size()) {
				return bytes(0);
			}
			
			b1 = src[srcPos];
			b2 = src[srcPos + 1];
			srcPos += 2;
			
			plain = (b1 & 0xC0) >> 6; //0-3
			nCopy = (b0 & 0x3F) + 4; //4-67
			offset = ((b1 & 0x3F) << 8) + b2 + 1; //1-16384
			
		} else if(b0 < 0xE0) {
			if(srcPos + 3 > src.size()) {
				return bytes(0);
			}
			
			b1 = src[srcPos];
			b2 = src[srcPos + 1];
			b3 = src[srcPos + 2];
			srcPos += 3;
			
			plain = b0 & 0x03; //0-3
			nCopy = ((b0 & 0x0C) << 6) + b3 + 5; //5-1028
			offset = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1; //1-131072
			
		} else if(b0 < 0xFC) {
			plain = ((b0 & 0x1F) << 2) + 4; //4-112
			nCopy = 0;
			offset = 0;

		} else {
			plain = b0 - 0xFC; //0-3
			nCopy = 0;
			offset = 0;
		}
		
		if(srcPos + plain > src.size() || dstPos + plain + nCopy > dst.size()) {
			return bytes(0);
		}
		
		copyBytes(src, srcPos, dst, dstPos, plain);
		srcPos += plain;
		dstPos += plain;
		
		//copy bytes from an earlier location in the decompressed output
		copyBytes(dst, dstPos - offset, dst, dstPos, nCopy);
		dstPos += nCopy;
	}
	
	return dst;
}