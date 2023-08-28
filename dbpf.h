#include "compression.h"

#include <fstream>
#include <iostream>
#include <map>
#include <set>
//#include <string>
#include <vector>

using namespace std;

typedef unsigned int uint;
typedef vector<unsigned char> bytes;

bytes read(ifstream& file, uint pos, uint size) {
	bytes buf = bytes(size);
	file.seekg(pos, ios::beg);
	file.read(reinterpret_cast<char *>(buf.data()), size);
	return buf;
}

uint getSize(ifstream& file) {
	uint pos = file.tellg();
	file.seekg(0, ios_base::end);
	uint size = file.tellg();
	file.seekg(pos, ios_base::beg);
	return size;
}

//convert 4 bytes from buf at pos to integer and increment pos (little endian)
uint getInt32le(bytes& buf, uint& pos) {
	uint n = buf[pos];
	n += buf[pos + 1] << 8;
	n += buf[pos + 2] << 16;
	n += buf[pos + 3] << 24;

	pos += 4;
	return n;
}

//put integer in buf at pos and increment pos (little endian)
void putInt32le(bytes& buf, uint& pos, uint n) {
	buf[pos] = n;
	buf[pos + 1] = n >> 8;
	buf[pos + 2] = n >> 16;
	buf[pos + 3] = n >> 24;

	pos += 4;
}

//convert 3 bytes from buf at pos to integer and increment pos (big endian)
uint getInt24bg(bytes& buf, uint& pos) {
	uint n = buf[pos] << 16;
	n += buf[pos + 1] << 8;
	n += buf[pos + 2];

	pos += 3;
	return n;
}

//put integer in buf at pos and increment pos (big endian)
void putInt24bg(bytes& buf, uint& pos, uint n) {
	buf[pos] = n >> 16;
	buf[pos + 1] = n >> 8;
	buf[pos + 2] = n;

	pos += 3;
}

//representing one entry (file) inside the package
class Entry {
	public:
		bytes content;
		uint type;
		uint group;
		uint instance;
		uint resource;
		uint location;
		uint compressed;
		uint repeated;
		
	Entry(bytes& buf, uint typeId, uint groupId, uint instanceId, uint resourceId) {
		content = buf;
		type = typeId;
		group = groupId;
		instance = instanceId;
		resource = resourceId;
		location = 0;
		compressed = false;
		repeated = false;
	}

	void compressEntry(int level) {
		if(!compressed && !repeated) {
			bytes newContent = bytes((content.size() - 1)); //must be smaller than the original, otherwise there is no benefit
			int length = try_compress(&content[0], content.size(), &newContent[0], level);
			
			if(length > 0) {
				content = bytes(newContent.begin(), newContent.begin() + length);
				compressed = true;
			}
		}
	}

	void decompressEntry() {
		if(compressed) {
			uint tempPos = 6;
			bytes newContent = bytes((getInt24bg(content, tempPos))); //uncompressed
			bool success = decompress(&content[0], content.size(), &newContent[0], newContent.size(), false);
			
			if(success) {
				content = newContent;
				compressed = false;
			} else {
				cout << "Failed to decompress entry" << endl;
			}
		}
	}
};

//for holding info from the DIR/CLST
struct CompressedEntry {
	uint type;
	uint group;
	uint instance;
	uint resource;
};

//for use by sets and maps
bool operator< (CompressedEntry entry, CompressedEntry entry2) {
	if(entry.type != entry2.type) {
		return entry.type < entry2.type;
	}
	
	if(entry.group != entry2.group) {
		return entry.group < entry2.group;
	}
	
	if(entry.instance != entry2.instance) {
		return entry.instance < entry2.instance;
	}
	
	if(entry.resource != entry2.resource) {
		return entry.resource < entry2.resource;
	}
	
	return false;
}

//representing one package file
class Package {
	public:
		uint indexVersion;
		vector<Entry> entries;
		
	Package(uint indexVersionNumber, vector<Entry>& entriesVector) {
		indexVersion = indexVersionNumber;
		entries = entriesVector;
	}
};

//get package from buffer
Package getPackage(ifstream& file) {
	uint fileSize = getSize(file);
	
	if(fileSize < 64) {
		cout << "File header not found" << endl;
		return Package(-1, vector<Entry>());
	}
	
	//header
	bytes buffer = read(file, 0, 64);

	uint pos = 36;
	uint entryCount = getInt32le(buffer, pos);
	uint indexLocation = getInt32le(buffer, pos);
	uint indexSize = getInt32le(buffer, pos);

	pos += 12;
	uint indexVersion = getInt32le(buffer, pos);

	vector<Entry> entries;
	entries.reserve(entryCount);

	bool hasClst = false;
	bytes clstContent;
	
	if(indexVersion > 2) {
		cout << "Unrecognized index version" << endl;
		return Package(-1, vector<Entry>());
	}
	
	if(indexLocation > fileSize || indexLocation + indexSize > fileSize) {
		cout << "File index outside of bounds" << endl;
		return Package(-1, vector<Entry>());
	}
	
	uint entryCountToIndexSize = 0;
	if(indexVersion == 2) {
		entryCountToIndexSize = entryCount * 4 * 6;
	} else {
		entryCountToIndexSize = entryCount * 4 * 5;
	}
	
	if(entryCountToIndexSize > indexSize) {
		cout << "Entry count larger than index" << endl;
		return Package(-1, vector<Entry>());
	}
	
	//index & entries
	buffer = read(file, indexLocation, indexSize);
	pos = 0;

	for(uint i = 0; i < entryCount; i++) {
		uint type = getInt32le(buffer, pos);
		uint group = getInt32le(buffer, pos);
		uint instance = getInt32le(buffer, pos);
		uint resource = 0;

		if(indexVersion == 2) {
			resource = getInt32le(buffer, pos);
		}

		uint location = getInt32le(buffer, pos);
		uint size = getInt32le(buffer, pos);
		
		if(location > fileSize || location + size > fileSize) {
			cout << "Entry location outside of bounds" << endl;
			return Package(-1, vector<Entry>()); 
		}
		
		if(type == 0xE86B1EEF) {
			clstContent = read(file, location, size);
			hasClst = true;
		} else {
			bytes content = read(file, location, size);
			Entry entry = Entry(content, type, group, instance, resource);
			entries.push_back(entry);
		}
	}

	//directory of compressed files
	if(hasClst) {
		set<CompressedEntry> CompressedEntries;

		pos = 0;
		while(pos < clstContent.size()) {
			uint type = getInt32le(clstContent, pos);
			uint group = getInt32le(clstContent, pos);
			uint instance = getInt32le(clstContent, pos);
			uint resource = 0;

			if(indexVersion == 2) {
				resource = getInt32le(clstContent, pos);
			}

			CompressedEntries.insert(CompressedEntry{type, group, instance, resource});
			pos += 4;
		}

		//check if entries are compressed
		for(auto& entry: entries) {
			if(CompressedEntries.find(CompressedEntry{entry.type, entry.group, entry.instance, entry.resource}) != CompressedEntries.end()) {
				if(entry.content[4] == 0x10 && entry.content[5] == 0xfb) {
					entry.compressed = true;
				}
			}
		}
	}
	
	//check if entries with repeated TGIRs exist (we don't want to compress those)
	map<CompressedEntry, uint> entriesMap;
	for(uint i = 0; i < entries.size(); i++) {
		if(entriesMap.find(CompressedEntry{entries[i].type, entries[i].group, entries[i].instance, entries[i].resource}) != entriesMap.end()) {
			uint j = entriesMap[CompressedEntry{entries[i].type, entries[i].group, entries[i].instance, entries[i].resource}];

			entries[i].repeated = true;
			entries[j].repeated = true;

		} else {
			entriesMap[CompressedEntry{entries[i].type, entries[i].group, entries[i].instance, entries[i].resource}] = i;
		}
	}

	return Package(indexVersion, entries);
}

//put package in buffer
bytes putPackage(Package& package) {
	//if it has compressed entries then create a new directory of compressed files
	bool hasCompressedEntries = false;
	for(auto& entry: package.entries) {
		if(entry.compressed) {
			hasCompressedEntries = true;
			break;
		}
	}

	if(hasCompressedEntries) {
		bytes clstContent;
		uint pos = 0;

		if(package.indexVersion == 2) {
			clstContent = bytes(package.entries.size() * 4 * 5);
		} else {
			clstContent = bytes(package.entries.size() * 4 * 4);
		}
		
		Entry clst = Entry(clstContent, 0xE86B1EEF, 0xE86B1EEF, 0x286B1F03, 0);

		for(auto& entry: package.entries) {
			if(entry.compressed) {
				putInt32le(clst.content, pos, entry.type);
				putInt32le(clst.content, pos, entry.group);
				putInt32le(clst.content, pos, entry.instance);

				if(package.indexVersion == 2) {
					putInt32le(clst.content, pos, entry.resource);
				}

				uint tempPos = 6;
				putInt32le(clst.content, pos, getInt24bg(entry.content, tempPos)); //uncompressed size
			}
		}

		package.entries.push_back(clst);
	}

	//calculate file length
	uint fileLength = 96; //header size

	//entries size
	for(auto& entry: package.entries) {
		fileLength += entry.content.size();
	}

	//index size
	if(package.indexVersion == 2) {
		fileLength += package.entries.size() * 4 * 6;
	} else {
		fileLength += package.entries.size() * 4 * 5;
	}

	//write header
	bytes buffer = bytes(fileLength);
	
	uint pos = 0;

	buffer[0] = 'D';
	buffer[1] = 'B';
	buffer[2] = 'P';
	buffer[3] = 'F';

	pos = 4;

	putInt32le(buffer, pos, 1);
	putInt32le(buffer, pos, 1);
	putInt32le(buffer, pos, 0);
	putInt32le(buffer, pos, 0);
	putInt32le(buffer, pos, 0);
	putInt32le(buffer, pos, 0);
	putInt32le(buffer, pos, 0);
	putInt32le(buffer, pos, 7);
	putInt32le(buffer, pos, 0);
	putInt32le(buffer, pos, 0);
	putInt32le(buffer, pos, 0);
	putInt32le(buffer, pos, 0);
	putInt32le(buffer, pos, 0);
	putInt32le(buffer, pos, 0);
	putInt32le(buffer, pos, package.indexVersion);

	pos += 32;

	//write entries, and save the location for the index
	for(auto& entry: package.entries) {
		entry.location = pos;
		copy(entry.content.begin(), entry.content.end(), buffer.begin() + pos);
		pos += entry.content.size();
	}

	//write the index
	uint indexStart = pos;

	for(auto& entry: package.entries) {
		putInt32le(buffer, pos, entry.type);
		putInt32le(buffer, pos, entry.group);
		putInt32le(buffer, pos, entry.instance);

		if(package.indexVersion == 2) {
			putInt32le(buffer, pos, entry.resource);
		}

		putInt32le(buffer, pos, entry.location);
		putInt32le(buffer, pos, entry.content.size());
	}

	uint indexEnd = pos;

	//update the header with index info
	pos = 36;
	putInt32le(buffer, pos, package.entries.size()); //index entry count
	putInt32le(buffer, pos, indexStart); //index location
	putInt32le(buffer, pos, indexEnd - indexStart); //index size

	return buffer;
}
