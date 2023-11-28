//Hash chain pattern matcher

/*the hash chain is similar to a hash map, the most recent values are stored in head, and past values are stored in prev
it requires very little memory allocation/deallocation, and the allocation is only done once, which makes it very efficient*/

//Compression: Good
//Speed: Good
//Memory Usage: Good
//Difficulty: Hard

#include <vector>

namespace qfs {

	using namespace std;

	typedef unsigned int uint;
	typedef vector<unsigned char> bytes;
	
	const uint GOOD_LENGTH = 32;
	const uint MAX_LOOPS = 32;

	template<typename T1, typename T2>
	T1 getMin(T1 a, T2 b) {
		return a <= b ? a : b;
	}

	struct Match {
		uint location;
		uint length;
		uint offset;
	};
	
	class Table {
		private:
			bytes& src;
			uint lastPos = 0;

			/*head is a hash map where the key is a hash created from a number of bytes in src
			and the value is the last position where the bytes could be found*/
			vector<uint> head = vector<uint>(0x10000, 0xFFFFFFFF);

			/*prev is a an array where the index is the position masked by the length of the sliding window
			and the value is the previous position that resolves to the same hash*/
			vector<uint> prev = vector<uint>(0x20000);
			
			uint getHash(uint pos) {
				return ((uint) src[pos++] << 8) + src[pos];
			}
			
			//get the previous position that resolves to the same hash, or 0xFFFFFFFF if the position is invalid
			uint getPrevPos(uint prevPos, uint pos) {
				uint nextPrevPos = prev[prevPos & 0x1FFFF];
				if(pos - nextPrevPos > 131072 || nextPrevPos >= prevPos) {
					return 0xFFFFFFFF;
				}
				
				return nextPrevPos;
			}
			
		public:
			Table(bytes& buffer): src(buffer) {}
			
			//add all bytes between lastPos and pos to the hash chain
			void addTo(uint pos) {
				for(lastPos; lastPos <= pos; lastPos++) {
					uint hashVal = getHash(lastPos);
					prev[lastPos & 0x1FFFF] = head[hashVal];
					head[hashVal] = lastPos;
				}
			}
			
			//check past values and find the longest matching pattern
			Match getLongestMatch(uint pos) {
				addTo(pos);
				Match longestMatch = Match{0, 0, 0};
				
				//it's important to limit the number of loops here for the sake of speed
				uint prevPos = getPrevPos(pos, pos);
				uint n_loops = 0;
				
				while(prevPos != 0xFFFFFFFF && n_loops < MAX_LOOPS) {
					uint length = 2;
					uint maxLen = getMin(src.size() - pos, 1028);
					
					//find out how long the match is
					//depending on the hashing function you might also need to check the first 2-3 bytes here
					while(length < maxLen && src[prevPos + length] == src[pos + length]) {
						length++;
					}
					
					uint offset = pos - prevPos;
					
					//check if the length and offset are valid
					if(length > longestMatch.length && ((length >= 3 && offset <= 1024) || (length >= 4 && offset <= 16384) || length >= 5)) {
						longestMatch = Match{pos, length, offset};
						
						if(length >= GOOD_LENGTH) {
							break;
						}
					}
					
					prevPos = getPrevPos(prevPos, pos);
					n_loops++;
				}
				
				return longestMatch;
			}
	};

}