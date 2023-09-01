#ifndef DBPF_H
#define DBPF_H

#include "compression.h"
#include "omp.h"

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
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

void write(ofstream& file, bytes& buf) {
	file.write(reinterpret_cast<char *>(buf.data()), buf.size());
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
		uint type;
		uint group;
		uint instance;
		uint resource;
		uint location;
		uint size;
		uint uncompressedSize = 0;
		bool compressed = false;
		bool repeated = false; //appears twice in same package
		
	Entry(uint typeId, uint groupId, uint instanceId, uint resourceId, uint loc, uint len) {
		type = typeId;
		group = groupId;
		instance = instanceId;
		resource = resourceId;
		location = loc;
		size = len;
	}

	bytes compressEntry(bytes& content, int level) {
		if(!compressed && !repeated) {
			bytes newContent = bytes((content.size() - 1)); //must be smaller than the original, otherwise there is no benefit
			int length = try_compress(&content[0], content.size(), &newContent[0], level);
			
			if(length > 0) {
				compressed = true;
				return bytes(newContent.begin(), newContent.begin() + length);
			}
		}
		
		return content;
	}

	bytes decompressEntry(bytes& content) {
		if(compressed) {
			uint tempPos = 6;
			bytes newContent = bytes((getInt24bg(content, tempPos))); //uncompressed
			bool success = decompress(&content[0], content.size(), &newContent[0], newContent.size(), false);
			
			if(success) {
				compressed = false;
				return newContent;
				
			} else {
				cout << "Failed to decompress entry" << endl;
			}
		}
		
		return content;
	}
};

//for holding info from the DIR/CLST
struct CompressedEntry {
	uint type;
	uint group;
	uint instance;
	uint resource;
	uint uncompressedSize;
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

//get package infromation from file
Package getPackage(ifstream& file, string displayPath) {
	file.seekg(0, ios::end);
	uint fileSize = file.tellg();
	file.seekg(0, ios::beg);
		
	if(fileSize < 64) {
		cout << displayPath << ": Header not found" << endl;
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
	entries.reserve(entryCount + 1);
	
	bytes clstContent;
	
	//error checking
	if(indexVersion > 2) {
		cout << displayPath << ": Unrecognized index version" << endl;
		return Package(-1, vector<Entry>());
	}
	
	if(indexLocation > fileSize || indexLocation + indexSize > fileSize) {
		cout << displayPath << ": File index outside of bounds" << endl;
		return Package(-1, vector<Entry>());
	}
	
	uint entryCountToIndexSize = 0;
	if(indexVersion == 2) {
		entryCountToIndexSize = entryCount * 4 * 6;
	} else {
		entryCountToIndexSize = entryCount * 4 * 5;
	}
	
	if(entryCountToIndexSize > indexSize) {
		cout << displayPath << ": Entry count larger than index" << endl;
		return Package(-1, vector<Entry>());
	}
	
	//index
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
			cout << displayPath << ": Entry location outside of bounds" << endl;
			return Package(-1, vector<Entry>()); 
		}
		
		if(type == 0xE86B1EEF) {
			clstContent = read(file, location, size);
			
		} else {
			Entry entry = Entry(type, group, instance, resource, location, size);
			entries.push_back(entry);
		}
	}

	//directory of compressed files
	if(clstContent.size() > 0) {
		set<CompressedEntry> compressedEntries;

		pos = 0;
		while(pos < clstContent.size()) {
			uint type = getInt32le(clstContent, pos);
			uint group = getInt32le(clstContent, pos);
			uint instance = getInt32le(clstContent, pos);
			uint resource = 0;

			if(indexVersion == 2) {
				resource = getInt32le(clstContent, pos);
			}
			
			uint uncompressedSize = getInt32le(clstContent, pos);
			compressedEntries.insert(CompressedEntry{type, group, instance, resource, uncompressedSize});
		}
		
		//check if entries are compressed
		for(auto& entry: entries) {
			auto iter = compressedEntries.find(CompressedEntry{entry.type, entry.group, entry.instance, entry.resource});
			if(iter != compressedEntries.end()) {
				
				CompressedEntry compressedEntry = *iter;
				if(entry.size != compressedEntry.uncompressedSize) {
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

//put package in file
void putPackage(ofstream& newFile, ifstream& oldFile, Package& package, bool parallel, bool decompress, bool recompress, uint level) {
	//write header
	uint bufferSize = 96;
	bytes buffer = bytes(bufferSize);
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

	write(newFile, buffer);

	//compress and write entries, and save the location and size for the index
	
	if(parallel) {
		omp_lock_t lock;
		omp_init_lock(&lock);
		
		#pragma omp parallel for
		for(int i = 0; i < package.entries.size(); i++) {
			omp_set_lock(&lock);
			bytes content = read(oldFile, package.entries[i].location, package.entries[i].size);
			omp_unset_lock(&lock);
			
			bool wasCompressed = package.entries[i].compressed;
			
			bytes newContent;
			if(decompress) {
				content = package.entries[i].decompressEntry(content);
			
			} else if(recompress) {
				bytes decompressedContent = package.entries[i].decompressEntry(content);
				newContent = package.entries[i].compressEntry(decompressedContent, level);
				content = newContent;
				
				//only use the new compressed entry if it's smaller than the old compressed entry
				if(newContent.size() < content.size()) {
					content = newContent;
					
				//otherwise use the old entry
				} else {
					package.entries[i].compressed = wasCompressed;
				}
				
			} else {
				newContent = package.entries[i].compressEntry(content, level);
				content = newContent;
			}
			
			package.entries[i].size = content.size();
			
			//we only care about the uncompressed size if the file is compressed
			if(package.entries[i].compressed) {
				uint tempPos = 6;
				package.entries[i].uncompressedSize = getInt24bg(content, tempPos);
			}
			
			omp_set_lock(&lock);
			
			package.entries[i].location = newFile.tellp();
			write(newFile, content);
			
			omp_unset_lock(&lock);
		}
		
		omp_destroy_lock(&lock);
		
	} else {
		for(int i = 0; i < package.entries.size(); i++) {
			bytes content = read(oldFile, package.entries[i].location, package.entries[i].size);
			bool wasCompressed = package.entries[i].compressed;
			
			bytes newContent;
			if(decompress) {
				newContent = package.entries[i].decompressEntry(content);
				content = newContent;
			
			} else if(recompress) {
				bytes decompressedContent = package.entries[i].decompressEntry(content);
				newContent = package.entries[i].compressEntry(decompressedContent, level);
				
				if(newContent.size() < content.size()) {
					content = newContent;
					
				//otherwise use the old entry
				} else {
					package.entries[i].compressed = wasCompressed;
				}
				
			} else {
				newContent = package.entries[i].compressEntry(content, level);
				content = newContent;
			}
			
			package.entries[i].size = content.size();
			
			if(package.entries[i].compressed) {
				uint tempPos = 6;
				package.entries[i].uncompressedSize = getInt24bg(content, tempPos);
			}
						
			package.entries[i].location = newFile.tellp();
			write(newFile, content);
		}
	}
	
	//make and write the directory of compressed files
	bytes clstContent;
	pos = 0;
	
	if(package.indexVersion == 2) {
		clstContent = bytes(package.entries.size() * 4 * 5);
	} else {
		clstContent = bytes(package.entries.size() * 4 * 4);
	}
	
	Entry clst = Entry(0xE86B1EEF, 0xE86B1EEF, 0x286B1F03, 0, newFile.tellp(), 0);

	for(auto& entry: package.entries) {
		if(entry.compressed) {
			putInt32le(clstContent, pos, entry.type);
			putInt32le(clstContent, pos, entry.group);
			putInt32le(clstContent, pos, entry.instance);

			if(package.indexVersion == 2) {
				putInt32le(clstContent, pos, entry.resource);
			}
			
			putInt32le(clstContent, pos, entry.uncompressedSize);
		}
	}
	
	clst.size = pos;
	
	if(clst.size > 0) { 
		clstContent = bytes(clstContent.begin(), clstContent.begin() + pos);
		write(newFile, clstContent);
		package.entries.push_back(clst);
	}

	//write the index
	uint indexStart = newFile.tellp();

	if(package.indexVersion == 2) {
		bufferSize = package.entries.size() * 4 * 6;
	} else {
		bufferSize = package.entries.size() * 4 * 5;
	}
	
	buffer = bytes(bufferSize);
	pos = 0;
	
	for(auto& entry: package.entries) {
		putInt32le(buffer, pos, entry.type);
		putInt32le(buffer, pos, entry.group);
		putInt32le(buffer, pos, entry.instance);

		if(package.indexVersion == 2) {
			putInt32le(buffer, pos, entry.resource);
		}

		putInt32le(buffer, pos, entry.location);
		putInt32le(buffer, pos, entry.size);
	}
	
	write(newFile, buffer);
	uint indexEnd = newFile.tellp();

	//update the header with index info
	newFile.seekp(36);
	
	buffer = bytes(12);
	pos = 0;
	
	putInt32le(buffer, pos, package.entries.size()); //index entry count
	putInt32le(buffer, pos, indexStart); //index location
	putInt32le(buffer, pos, indexEnd - indexStart); //index size

	write(newFile, buffer);
}

#endif
