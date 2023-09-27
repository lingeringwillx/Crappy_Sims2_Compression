//(Unused) This is a simple implementation of the QFS compression

#include <unordered_map>
#include <vector>

using namespace std;

typedef unsigned int uint;
typedef vector<unsigned char> bytes;

//copy length bytes from src at srcPos to dst at dstPos and increment srcPos and dstPos
//copying one byte at a time is REQUIRED in some cases, otherwise decompression will not work
void copyBytes(bytes& src, uint& srcPos, bytes& dst, uint& dstPos, uint length) {
	for(uint i = 0; i < length; i++) {
		dst[dstPos++] = src[srcPos++];
	}
}

template<typename T1, typename T2>
T1 getMin(T1 a, T2 b) {
    return a <= b ? a : b;
}

struct Match {
	uint location;
	uint length;
	uint offset;
};

//hashtable where the keys are 3 bytes sequences from src converted to integers, and the values are the last position where the 3 bytes sequence could be found
class Table {
	private:
		bytes& src;
		unordered_map<uint, uint> map;
		uint lastPos = 0;
		
		uint getHash(uint pos) {
			return ((uint) src[pos++] << 16) + ((uint) src[pos++] << 8) + src[pos];
		}
		
	public:
		Table(bytes& buffer): src(buffer) {}
		
		//add all bytes between [lastPos, pos) to the table
		void addTo(uint pos) {
			for(lastPos; lastPos < pos; lastPos++) {
				map[getHash(lastPos)] = lastPos;
			}
		}
		
		//find the last match
		Match getMatch(uint pos) {
			auto iter = map.find(getHash(pos));
			
			if(iter == map.end()) {
				return Match{0, 0, 0};
			}
			
			uint lastLoc = iter->second;
			uint offset = pos - lastLoc;
			
			if(offset > 131072) {
				return Match{0, 0, 0};
			}
			
			uint length = 3;
			uint maxLen = getMin(src.size() - pos, 1028);
			
			while(length < maxLen && src[lastLoc + length] == src[pos + length]) {
				length++;
			}
			
			if(offset <= 1024 || (offset <= 16384 && length >= 4) || length >= 5) {
				return Match{pos, length, offset};
			} else {
				return Match{0, 0, 0};
			}
		}
};

//compresses src, returns an empty vector if compression fails
//it will fail if the compressed output >= decompressed input since it's better to store these assets uncompressed
//this typically happens with very small assets
bytes compress(bytes& src) {
	//finding patterns
	Table table = Table(src);
	vector<Match> matches = vector<Match>();
	
	uint i = 0;
	while(i <= src.size() - 3) {
		Match match = table.getMatch(i);
		
		if(match.length >= 3) {
			matches.push_back(match);
			i += match.length;
		} else {
			i++;
		}
		
		table.addTo(i);
	}
	
	//compress src
	//compressed output has to smaller than the decompressed output, otherwise it's not useful
	//maximum possible size of the compressed entry is 0xFFFFFF + 1 due to the fact that the compressed size in the header is only 3 bytes long
	bytes dst = bytes(getMin(src.size() - 1, 16777216));
	
	uint srcPos = 0;
	uint dstPos = 9;
	
	//plain = number of bytes to copy from src as is with no compression
	//nCopy = number of bytes to compress
	//offset = offset from current location in decompressed input to where the pattern could be found
	
	for(uint i = 0; i < matches.size(); i++) {
		//copy bytes from src to dst until the location of the match is reached
		uint plain = matches[i].location - srcPos;
		while(plain > 3) {
			//max possible value is 112
			//this can only be a multiple of 4, due to the 2-bit right shift in the control character
			plain = getMin(plain - plain % 4, 112);
			
			if(dstPos + plain + 1 > dst.size()) {
				return bytes();
			}
			
			// 111ppppp
			dst[dstPos++] = 0b11100000 + ((plain - 4) >> 2);
			
			copyBytes(src, srcPos, dst, dstPos, plain);
			
			plain = matches[i].location - srcPos;
		}
		
		uint nCopy = matches[i].length;
		uint offset = matches[i].offset - 1; //subtraction is a part of the transformation for the offset, I think...
		
		//apply weird QFS transformations
		if(nCopy <= 10 && offset < 1024) {
			if(dstPos + plain + 2 > dst.size())  {
				return bytes();
			}
			
			// 0oocccpp oooooooo
			dst[dstPos++] = ((offset >> 3) & 0b01100000) + ((nCopy - 3) << 2) + plain;
			dst[dstPos++] = offset;
			
		} else if(nCopy <= 67 && offset < 16384) {
			if(dstPos + plain + 3 > dst.size())  {
				return bytes();
			}
			
			// 10cccccc ppoooooo oooooooo
			dst[dstPos++] = 0b10000000 + (nCopy - 4);
			dst[dstPos++] = (plain << 6) + (offset >> 8);
			dst[dstPos++] = offset;
			
		} else if(nCopy <= 1028 && offset < 131072) {
			if(dstPos + plain + 4 > dst.size())  {
				return bytes();
			}
			
			// 110occpp oooooooo oooooooo cccccccc
			dst[dstPos++] = 0b11000000 + ((offset >> 12) & 0b00010000) + (((nCopy - 5) >> 6) & 0b00001100) + plain;
			dst[dstPos++] = offset >> 8;
			dst[dstPos++] = offset;
			dst[dstPos++] = nCopy - 5;
		}
		
		copyBytes(src, srcPos, dst, dstPos, plain);
		srcPos += nCopy;
	}
	
	//copy the remaining bytes at the end
	uint plain = src.size() - srcPos;
	while(plain > 3) {
		plain = getMin(plain - plain % 4, 112);
		
		if(dstPos + plain + 1 > dst.size())  {
			return bytes();
		}
		
		// 111ppppp
		dst[dstPos++] = 0b11100000 + ((plain - 4) >> 2);
		
		copyBytes(src, srcPos, dst, dstPos, plain);
		
		plain = src.size() - srcPos;
	}
	
	if(plain > 0) {
		if(dstPos + plain + 1 > dst.size())  {
			return bytes();
		}
		
		// 111111pp
		dst[dstPos++] = 0b11111100 + plain;
		
		copyBytes(src, srcPos, dst, dstPos, plain);
	}
	
	//make QFS compression header
	uint compressedSize = dstPos;
	uint uncompressedSize = src.size();
	
	dst[0] = compressedSize;
	dst[1] = compressedSize >> 8;
	dst[2] = compressedSize >> 16;
	dst[3] = compressedSize >> 24;
	dst[4] = 0x10;
	dst[5] = 0xFB;
	dst[6] = uncompressedSize >> 16;
	dst[7] = uncompressedSize >> 8;
	dst[8] = uncompressedSize;
	
	return bytes(dst.begin(), dst.begin() + dstPos);
}

//decompresses src, returns an empty vector if decompression fails
//it should only fail on broken compressed assets
bytes decompress(bytes& src) {
	uint srcPos = 6;
	uint uncompressedSize = ((uint) src[srcPos++] << 16) + ((uint) src[srcPos++] << 8) + ((uint) src[srcPos++]);
	
	bytes dst = bytes(uncompressedSize);
	uint dstPos = 0;
	
	uint b0, b1, b2, b3; //control characters
	uint nCopy, offset, plain;
	
	while(srcPos < src.size()) {
		if(srcPos + 1 > src.size()) {
			return bytes();
		}
		
		b0 = src[srcPos++];

		if(b0 < 0x80) {
			if(srcPos + 1 > src.size()) {
				return bytes();
			}
			
			b1 = src[srcPos++];
			
			// 0oocccpp oooooooo
			plain = b0 & 0b00000011; //0-3
			nCopy = ((b0 & 0b00011100) >> 2) + 3; //3-10
			offset = ((b0 & 0b01100000) << 3) + b1 + 1; //1-1024
			
		} else if(b0 < 0xC0) {
			if(srcPos + 2 > src.size()) {
				return bytes();
			}
			
			b1 = src[srcPos++];
			b2 = src[srcPos++];
			
			// 10cccccc ppoooooo oooooooo
			plain = (b1 & 0b11000000) >> 6; //0-3
			nCopy = (b0 & 0b00111111) + 4; //4-67
			offset = ((b1 & 0b00111111) << 8) + b2 + 1; //1-16384
			
		} else if(b0 < 0xE0) {
			if(srcPos + 3 > src.size()) {
				return bytes();
			}
			
			// 110occpp oooooooo oooooooo cccccccc
			b1 = src[srcPos++];
			b2 = src[srcPos++];
			b3 = src[srcPos++];
			
			plain = b0 & 0b00000011; //0-3
			nCopy = ((b0 & 0b00001100) << 6) + b3 + 5; //5-1028
			offset = ((b0 & 0b00010000) << 12) + (b1 << 8) + b2 + 1; //1-131072
			
		} else if(b0 < 0xFC) {
			// 111ppppp
			plain = ((b0 & 0b00011111) << 2) + 4; //4-112
			nCopy = 0;
			offset = 0;

		} else {
			// 111111pp
			plain = b0 & 0b00000011; //0-3
			nCopy = 0;
			offset = 0;
		}
		
		if(srcPos + plain > src.size() || dstPos + plain + nCopy > dst.size())  {
			return bytes();
		}
		
		copyBytes(src, srcPos, dst, dstPos, plain);
		
		//copy bytes from an earlier location in the decompressed output
		uint fromOffset = dstPos - offset;
		copyBytes(dst, fromOffset, dst, dstPos, nCopy);
	}
	
	return dst;
}