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

void log_error(string error) {
	cout << error << endl;
	ofstream logFile = ofstream("errors.txt", ios::app);
	
	if(logFile.is_open()) {
		logFile << error << '\n';
		logFile.close();
	}
}

bytes read(ifstream& file, uint pos, uint size) {
	bytes buf = bytes(size);
	file.seekg(pos, ios::beg);
	file.read(reinterpret_cast<char *>(buf.data()), size);
	return buf;
}

void write(ofstream& file, bytes& buf) {
	file.write(reinterpret_cast<char *>(buf.data()), buf.size());
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
		uint type;
		uint group;
		uint instance;
		uint resource;
		uint location;
		uint size;
		uint uncompressedSize = 0;
		bool listedInDir = false; //found in the directory of compressed files
		bool hasCompressionHeader = false; //has bytes 0x10FB in the header
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
		if((!listedInDir || !hasCompressionHeader) && !repeated) {
			bytes newContent = bytes((content.size() - 1)); //must be smaller than the original, otherwise there is no benefit
			int length = try_compress(&content[0], content.size(), &newContent[0], level);
			
			if(length > 0) {
				listedInDir = true;
				hasCompressionHeader = true;
				return bytes(newContent.begin(), newContent.begin() + length);
			}
		}
		
		return content;
	}

	bytes decompressEntry(bytes& content) {
		if(listedInDir && hasCompressionHeader) {
			uint tempPos = 6;
			bytes newContent = bytes((getInt24bg(content, tempPos))); //uncompressed
			bool success = decompress(&content[0], content.size(), &newContent[0], newContent.size(), false);
			
			if(success) {
				listedInDir = false;
				hasCompressionHeader = false;
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
	uint fileSize = getSize(file);
	
	if(fileSize < 64) {
		log_error(displayPath + ": Header not found");
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
	
	//error checking
	if(indexVersion > 2) {
		log_error(displayPath + ": Unrecognized index version");
		return Package(-1, vector<Entry>());
	}
	
	if(indexLocation > fileSize || indexLocation + indexSize > fileSize) {
		log_error(displayPath + ": File index outside of bounds");
		return Package(-1, vector<Entry>());
	}
	
	uint entryCountToIndexSize = 0;
	if(indexVersion == 2) {
		entryCountToIndexSize = entryCount * 4 * 6;
	} else {
		entryCountToIndexSize = entryCount * 4 * 5;
	}
	
	if(entryCountToIndexSize > indexSize) {
		log_error(displayPath + ": Entry count larger than index");
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
			log_error(displayPath + ": Entry location outside of bounds");
			return Package(-1, vector<Entry>()); 
		}
		
		if(type == 0xE86B1EEF) {
			clstContent = read(file, location, size);
			hasClst = true;
		} else {
			Entry entry = Entry(type, group, instance, resource, location, size);
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
				entry.listedInDir = true;
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
			
			package.entries[i].hasCompressionHeader = content[4] == 0x10 && content[5] == 0xFB;
			
			if(recompress || decompress) {
				content = package.entries[i].decompressEntry(content);
			}
			
			if(!decompress || (decompress && recompress)) {
				content = package.entries[i].compressEntry(content, level);
			}
			
			package.entries[i].size = content.size();
			
			//we only care about the uncompressed size if the file is compressed
			if(package.entries[i].listedInDir && package.entries[i].hasCompressionHeader) {
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
			buffer = read(oldFile, package.entries[i].location, package.entries[i].size);
			package.entries[i].hasCompressionHeader = buffer[4] == 0x10 && buffer[5] == 0xFB;
			
			if(recompress || decompress) {
				buffer = package.entries[i].decompressEntry(buffer);
			}
			
			if(!decompress || (decompress && recompress)) {
				buffer = package.entries[i].compressEntry(buffer, level);
			}
			
			package.entries[i].size = buffer.size();
			
			if(package.entries[i].listedInDir && package.entries[i].hasCompressionHeader) {
				uint tempPos = 6;
				package.entries[i].uncompressedSize = getInt24bg(buffer, tempPos);
			}
			
			package.entries[i].location = newFile.tellp();
			write(newFile, buffer);
		}
	}
	
	//make and write the directory of compressed files
	bytes clstContent;
	pos = 0;

	clstContent = bytes();
	if(package.indexVersion == 2) {
		clstContent = bytes(package.entries.size() * 4 * 5);
	} else {
		clstContent = bytes(package.entries.size() * 4 * 4);
	}
	
	Entry clst = Entry(0xE86B1EEF, 0xE86B1EEF, 0x286B1F03, 0, newFile.tellp(), 0);

	for(auto& entry: package.entries) {
		if(entry.listedInDir && entry.hasCompressionHeader) {
			putInt32le(clstContent, pos, entry.type);
			putInt32le(clstContent, pos, entry.group);
			putInt32le(clstContent, pos, entry.instance);

			if(package.indexVersion == 2) {
				putInt32le(clstContent, pos, entry.resource);
			}
			
			putInt32le(clstContent, pos, entry.uncompressedSize); //uncompressed size
		}
	}
	
	clst.location = newFile.tellp();
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
