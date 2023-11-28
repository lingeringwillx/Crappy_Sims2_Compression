//Map for pattern matcher

//Compression: Okay
//Speed: Okay
//Memory Usage: Okay
//Difficulty: Easy

#include <unordered_map>
#include <vector>

namespace qfs {

	using namespace std;

	typedef unsigned int uint;
	typedef vector<unsigned char> bytes;

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
			
			//find match
			Match getLongestMatch(uint pos) {
				//add values to the table
				addTo(pos);
				
				auto iter = map.find(getHash(pos));
				
				if(iter == map.end()) {
					return Match{0, 0, 0};
				}
				
				uint prevPos = iter->second;
				uint offset = pos - prevPos;
				
				if(offset > 131072) {
					return Match{0, 0, 0};
				}
				
				uint length = 3;
				uint maxLen = getMin(src.size() - pos, 1028);
				
				while(length < maxLen && src[prevPos + length] == src[pos + length]) {
					length++;
				}
				
				if(offset <= 1024 || (length >= 4 && offset <= 16384) || length >= 5) {
					return Match{pos, length, offset};
				} else {
					return Match{0, 0, 0};
				}
			}
	};
}