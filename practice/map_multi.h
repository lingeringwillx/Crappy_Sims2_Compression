//Array of arrays pattern matcher

//Compression: Good
//Speed: Okay
//Memory Usage: Bad
//Difficulty: Medium

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
	
	//hashtable where the keys are 2 bytes sequences from src converted to integers, and the values are a list of the positions where the 2 bytes sequence could be found
	class Table {
		private:
			bytes& src;
			vector<vector<uint>> map = vector<vector<uint>>(65536, vector<uint>());
			uint lastPos = 0;
			
			uint getHash(uint pos) {
				return ((uint) src[pos++] << 8) + src[pos];
			}
			
		public:
			Table(bytes& buffer): src(buffer) {}
			
			//add all bytes between [lastPos, pos) to the table
			void addTo(uint pos) {
				for(lastPos; lastPos < pos; lastPos++) {
					map[getHash(lastPos)].push_back(lastPos);
				}
			}
			
			//check past values and find the longest matching pattern
			Match getLongestMatch(uint pos) {
				//add values to the table
				if(pos > 0) {
					addTo(pos - 1);
				}
				
				auto& positions = map[getHash(pos)];
				Match longestMatch = Match{0, 0, 0};
				
				//loop from the end
				//it's important to limit the number of loops here for the sake of speed
				auto end = positions.rend();
				
				if(positions.size() > MAX_LOOPS) {
					end = positions.rbegin() + MAX_LOOPS;
				}
				
				for(auto iter = positions.rbegin(); iter < end; iter++) {
					uint prevPos = *iter;
					
					uint length = 2;
					uint offset = pos - prevPos;
					uint maxLen = getMin(src.size() - pos, 1028);

					if(offset > 131072) {
						break;
					}
					
					//find out how long the match is
					while(length < maxLen && src[prevPos + length] == src[pos + length]) {
						length++;
					}
					
					//check if the length and offset are valid
					if(length > longestMatch.length && ((length >= 3 && offset <= 1024) || (offset <= 16384 && length >= 4) || length >= 5)) {
						longestMatch = Match{pos, length, offset};
						
						if(length >= GOOD_LENGTH) {
							break;
						}
					}
				}
				
				return longestMatch;
			}
	};

}
