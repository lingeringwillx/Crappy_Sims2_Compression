#ifndef DBPF_H
#define DBPF_H

#include "compression3.h"
#include "omp.h"

#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

using namespace std;

typedef unsigned int uint;
typedef vector<unsigned char> bytes;

namespace dbpf {
	
	bytes readFile(fstream& file, uint pos, uint size) {
		bytes buf = bytes(size);
		file.seekg(pos, ios::beg);
		file.read(reinterpret_cast<char*>(buf.data()), size);
		return buf;
	}

	void writeFile(fstream& file, bytes& buf) {
		file.write(reinterpret_cast<char*>(buf.data()), buf.size());
	}

	//convert 4 bytes from buf at pos to integer and increment pos (little endian)
	uint getInt32le(bytes& buf, uint& pos) {
		return ((uint) buf[pos++]) + ((uint) buf[pos++] << 8) + ((uint) buf[pos++] << 16) + ((uint) buf[pos++] << 24);
	}

	//put integer in buf at pos and increment pos (little endian)
	void putInt32le(bytes& buf, uint& pos, uint n) {
		buf[pos++] = n;
		buf[pos++] = n >> 8;
		buf[pos++] = n >> 16;
		buf[pos++] = n >> 24;
	}

	//convert 3 bytes from buf at pos to integer and increment pos (big endian)
	uint getInt24bg(bytes& buf, uint& pos) {
		return ((uint) buf[pos++] << 16) + ((uint) buf[pos++] << 8) + ((uint) buf[pos++]);
	}

	//put integer in buf at pos and increment pos (big endian)
	void putInt24bg(bytes& buf, uint& pos, uint n) {
		buf[pos++] = n >> 16;
		buf[pos++] = n >> 8;
		buf[pos++] = n;
	}

	//representing one entry (file) inside the package
	struct Entry {
		uint type;
		uint group;
		uint instance;
		uint resource;
		uint location;
		uint size;
		uint uncompressedSize = 0;
		bool compressed = false;
		bool repeated = false; //appears twice in same package
	};

	//for holding info from the DIR/CLST
	struct CompressedEntry {
		uint type;
		uint group;
		uint instance;
		uint resource;
		uint uncompressedSize;
	};

	//representing one package file
	struct Package {
		int indexVersion;
		vector<Entry> entries;
	};

	//for use by sets and maps
	struct hashFunction {
		template<class EntryType>
		size_t operator()(const EntryType& entry) const {
			return entry.type ^ entry.group ^ entry.instance ^ entry.resource;
		}
	};

	struct equalFunction {
		template<class EntryType>
		bool operator()(const EntryType& entry, const EntryType& entry2) const {
			return entry.type == entry2.type && entry.group == entry2.group && entry.instance == entry2.instance && entry.resource == entry2.resource;
		}
	};

	bytes compressEntry(Entry& entry, bytes& content, int level) {
		if(!entry.compressed && !entry.repeated) {
			bytes newContent = qfs::compress(content);
			
			if(newContent.size() > 0) {
				entry.compressed = true;
				return newContent;
			}
		}
		
		return content;
	}

	bytes decompressEntry(Entry& entry, bytes& content) {
		if(entry.compressed) {
			bytes newContent = qfs::decompress(content);
			
			if(newContent.size() > 0) {
				entry.compressed = false;
				return newContent;
				
			} else {
				cout << "Failed to decompress entry" << endl;
			}
		}
		
		return content;
	}

	bytes recompressEntry(Entry& entry, bytes& content, int level) {
		bool wasCompressed = entry.compressed;
		bytes decompressedContent = decompressEntry(entry, content);
		bytes compressedContent = compressEntry(entry, decompressedContent, level);
		
		if(compressedContent.size() < content.size()) {
			return compressedContent;
		} else {
			//decompression/compression failed, or new compressed entry is larger or equal to old compressed entry
			entry.compressed = wasCompressed;
			return content;
		}
	}

	//get package infromation from file
	Package getPackage(fstream& file, string displayPath) {
		file.seekg(0, ios::end);
		uint fileSize = file.tellg();
		file.seekg(0, ios::beg);
			
		if(fileSize < 64) {
			cout << displayPath << ": Header not found" << endl;
			return Package{-1, vector<Entry>()};
		}
		
		//header
		bytes buffer = readFile(file, 0, 64);

		uint pos = 36;
		uint entryCount = getInt32le(buffer, pos);
		uint indexLocation = getInt32le(buffer, pos);
		uint indexSize = getInt32le(buffer, pos);

		pos += 12;
		int indexVersion = getInt32le(buffer, pos);
		
		Package package = Package{indexVersion, vector<Entry>()};
		package.entries.reserve(entryCount + 1);
		
		bytes clstContent;
		
		//error checking
		if(indexVersion > 2) {
			cout << displayPath << ": Unrecognized index version" << endl;
			return Package{-1, vector<Entry>()};
		}
		
		if(indexLocation > fileSize || indexLocation + indexSize > fileSize) {
			cout << displayPath << ": File index outside of bounds" << endl;
			return Package{-1, vector<Entry>()};
		}
		
		uint entryCountToIndexSize = 0;
		if(indexVersion == 2) {
			entryCountToIndexSize = entryCount * 4 * 6;
		} else {
			entryCountToIndexSize = entryCount * 4 * 5;
		}
		
		if(entryCountToIndexSize > indexSize) {
			cout << displayPath << ": Entry count larger than index" << endl;
			return Package{-1, vector<Entry>()};
		}
		
		//index
		buffer = readFile(file, indexLocation, indexSize);
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
				return Package{-1, vector<Entry>()}; 
			}
			
			if(type == 0xE86B1EEF) {
				clstContent = readFile(file, location, size);
				
			} else {
				Entry entry = Entry{type, group, instance, resource, location, size};
				package.entries.push_back(entry);
			}
		}

		//directory of compressed files
		if(clstContent.size() > 0) {
			unordered_set<CompressedEntry, hashFunction, equalFunction> compressedEntries;
			if(indexVersion == 2) {
				compressedEntries.reserve(clstContent.size() / (4 * 5));
			} else {
				compressedEntries.reserve(clstContent.size() / (4 * 4));
			}
			
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
			for(auto& entry: package.entries) {
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
		unordered_map<Entry, uint, hashFunction, equalFunction> entriesMap;
		entriesMap.reserve(package.entries.size());
		
		for(uint i = 0; i < package.entries.size(); i++) {
			auto iter = entriesMap.find(package.entries[i]);
			if(iter != entriesMap.end()) {
				uint j = iter->second;

				package.entries[i].repeated = true;
				package.entries[j].repeated = true;

			} else {
				entriesMap[package.entries[i]] = i;
			}
		}

		return package;
	}

	//put package in file
	void putPackage(fstream& newFile, fstream& oldFile, Package& package, bool inParallel, bool decompress, bool recompress, uint level) {
		//write header
		bytes buffer = bytes(96);
		uint pos = 0;
		
		putInt32le(buffer, pos, 0x46504244);
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

		writeFile(newFile, buffer);

		//compress and write entries, and save the location and size for the index
		omp_lock_t lock;
		omp_init_lock(&lock);
		
		#pragma omp parallel for if (inParallel)
		for(int i = 0; i < package.entries.size(); i++) {
			omp_set_lock(&lock);
			bytes content = readFile(oldFile, package.entries[i].location, package.entries[i].size);
			omp_unset_lock(&lock);
			
			bytes newContent;
			
			if(decompress) {
				newContent = decompressEntry(package.entries[i], content);
			} else if(recompress) {
				newContent = recompressEntry(package.entries[i], content, level);
			} else {
				newContent = compressEntry(package.entries[i], content, level);
			}
			
			package.entries[i].size = newContent.size();
			
			//we only care about the uncompressed size if the file is compressed
			if(package.entries[i].compressed) {
				uint tempPos = 6;
				package.entries[i].uncompressedSize = getInt24bg(newContent, tempPos);
			}
			
			omp_set_lock(&lock);
			
			package.entries[i].location = newFile.tellp();
			writeFile(newFile, newContent);
			
			omp_unset_lock(&lock);
		}
		
		omp_destroy_lock(&lock);
		
		//make and write the directory of compressed files
		bytes clstContent;
		pos = 0;
		
		if(package.indexVersion == 2) {
			clstContent = bytes(package.entries.size() * 4 * 5);
		} else {
			clstContent = bytes(package.entries.size() * 4 * 4);
		}
		
		Entry clst = Entry{0xE86B1EEF, 0xE86B1EEF, 0x286B1F03, 0, (uint) newFile.tellp(), 0};

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
			writeFile(newFile, clstContent);
			package.entries.push_back(clst);
		}

		//write the index
		uint indexStart = newFile.tellp();
		
		if(package.indexVersion == 2) {
			buffer = bytes(package.entries.size() * 4 * 6);
		} else {
			buffer = bytes(package.entries.size() * 4 * 5);
		}
		
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
		
		writeFile(newFile, buffer);
		uint indexEnd = newFile.tellp();

		//update the header with index info
		newFile.seekp(36);
		
		buffer = bytes(12);
		pos = 0;
		
		putInt32le(buffer, pos, package.entries.size()); //index entry count
		putInt32le(buffer, pos, indexStart); //index location
		putInt32le(buffer, pos, indexEnd - indexStart); //index size

		writeFile(newFile, buffer);
	}
	
}

#endif