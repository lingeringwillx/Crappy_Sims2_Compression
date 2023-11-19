#ifndef DBPF_H
#define DBPF_H

#include "qfs.h"
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

	//convert 4 bytes from buf at pos to an integer and increment pos (little endian)
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

	//get the uncompressed size from the compression header (3 bytes big endian integer)
	uint getUncompressedSize(bytes& buf) {
		return ((uint) buf[6] << 16) + ((uint) buf[7] << 8) + ((uint) buf[8]);
	}
	
	//compression mode
	enum Mode { COMPRESS, DECOMPRESS, RECOMPRESS, SKIP };
	
	//representing the header of a package file
	struct Header {
		uint majorVersion;
		uint minorVersion;
		uint majorUserVersion;
		uint minorUserVersion;
		uint flags;
		uint createdDate;
		uint modifiedDate;
		uint indexMajorVersion;
		uint indexEntryCount;
		uint indexLocation;
		uint indexSize;
		uint holeIndexEntryCount;
		uint holdIndexLocation;
		uint holeIndexSize;
		uint indexMinorVersion;
		bytes remainder;
	};
	
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

	//representing one package file
	struct Package {
		bool unpacked = true;
		Header header;
		vector<Entry> entries;
		unordered_set<CompressedEntry, hashFunction, equalFunction> compressedEntries; //directory of compressed files
	};
	
	bytes compressEntry(Entry& entry, bytes& content) {
		if(!entry.compressed && !entry.repeated) {
			bytes newContent = bytes(content.size() - 1); //must be smaller than the original, otherwise there is no benefit
			int length = qfs_compress(content.data(), content.size(), newContent.data());
			
			if(length > 0) {
				newContent.resize(length);
				entry.compressed = true;
				return newContent;
			}
		}
		
		return content;
	}

	bytes decompressEntry(Entry& entry, bytes& content) {
		if(entry.compressed) {
			bytes newContent = bytes(getUncompressedSize(content));
			bool success = qfs_decompress(content.data(), content.size(), newContent.data(), newContent.size(), false);
			
			if(success) {
				entry.compressed = false;
				return newContent;
			} else {
				wcout << L"Failed to decompress entry" << endl;
			}
		}
		
		return content;
	}
	
	bytes recompressEntry(Entry& entry, bytes& content) {
		bool wasCompressed = entry.compressed;
		
		bytes newContent = decompressEntry(entry, content);
		newContent = compressEntry(entry, newContent);
		
		//only return the new entry if there is a reduction in size
		if(newContent.size() < content.size()) {
			return newContent;
		} else {
			entry.compressed = wasCompressed;
			return content;
		}
	}
	
	//get package infromation from file
	Package getPackage(fstream& file, wstring displayPath) {
		file.seekg(0, ios::end);
		uint fileSize = file.tellg();
		file.seekg(0, ios::beg);
			
		if(fileSize < 64) {
			wcout << displayPath << L": Header not found" << endl;
			return Package{false};
		}
		
		Package package = Package();
		
		//header
		bytes buffer = readFile(file, 0, 96);
		uint pos = 4;
		
		if(buffer[0] != 'D' || buffer[1] != 'B' || buffer[2] != 'P' || buffer[3] != 'F') {
			wcout << displayPath << L": Magic header not found" << endl;
			return Package{false};
		}
		
		package.header.majorVersion = getInt32le(buffer, pos);
		package.header.minorVersion = getInt32le(buffer, pos);
		package.header.majorUserVersion = getInt32le(buffer, pos);
		package.header.minorUserVersion = getInt32le(buffer, pos);
		package.header.flags = getInt32le(buffer, pos);
		package.header.createdDate = getInt32le(buffer, pos);
		package.header.modifiedDate = getInt32le(buffer, pos);
		package.header.indexMajorVersion = getInt32le(buffer, pos);
		package.header.indexEntryCount = getInt32le(buffer, pos);
		package.header.indexLocation = getInt32le(buffer, pos);
		package.header.indexSize = getInt32le(buffer, pos);
		pos += 12; //skip hole index info
		package.header.indexMinorVersion = getInt32le(buffer, pos);
		package.header.remainder = bytes(buffer.begin() + 64, buffer.end());
		
		if(package.header.majorVersion != 1 || (package.header.minorVersion != 0 && package.header.minorVersion != 1 && package.header.minorVersion != 2) || package.header.indexMajorVersion != 7) {
			wcout << displayPath << L": Not a Sims 2 package file" << endl;
			return Package{false};
		}
		
		package.entries.reserve(package.header.indexEntryCount + 1);
		
		bytes clstContent;
		
		if(package.header.indexMinorVersion > 2) {
			wcout << displayPath << L": Unrecognized index version" << endl;
			return Package{false};
		}
		
		if(package.header.indexLocation > fileSize || package.header.indexLocation + package.header.indexSize > fileSize) {
			wcout << displayPath << L": File index outside of bounds" << endl;
			return Package{false};
		}
		
		uint indexEntryCountToIndexSize = 0;
		if(package.header.indexMinorVersion == 2) {
			indexEntryCountToIndexSize = package.header.indexEntryCount * 4 * 6;
		} else {
			indexEntryCountToIndexSize = package.header.indexEntryCount * 4 * 5;
		}
		
		if(indexEntryCountToIndexSize > package.header.indexSize) {
			wcout << displayPath << L": Entry count larger than index" << endl;
			return Package{false};
		}
		
		//index
		buffer = readFile(file, package.header.indexLocation, package.header.indexSize);
		pos = 0;

		for(uint i = 0; i < package.header.indexEntryCount; i++) {
			uint type = getInt32le(buffer, pos);
			uint group = getInt32le(buffer, pos);
			uint instance = getInt32le(buffer, pos);
			uint resource = 0;

			if(package.header.indexMinorVersion == 2) {
				resource = getInt32le(buffer, pos);
			}

			uint location = getInt32le(buffer, pos);
			uint size = getInt32le(buffer, pos);
			
			if(location > fileSize || location + size > fileSize) {
				wcout << displayPath << L": Entry location outside of bounds" << endl;
				return Package{false}; 
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
			if(package.header.indexMinorVersion == 2) {
				package.compressedEntries.reserve(clstContent.size() / (4 * 5));
			} else {
				package.compressedEntries.reserve(clstContent.size() / (4 * 4));
			}
			
			pos = 0;
			while(pos < clstContent.size()) {
				uint type = getInt32le(clstContent, pos);
				uint group = getInt32le(clstContent, pos);
				uint instance = getInt32le(clstContent, pos);
				uint resource = 0;

				if(package.header.indexMinorVersion == 2) {
					resource = getInt32le(clstContent, pos);
				}
				
				uint uncompressedSize = getInt32le(clstContent, pos);
				package.compressedEntries.insert(CompressedEntry{type, group, instance, resource, uncompressedSize});
			}
			
			//check if entries are compressed
			for(auto& entry: package.entries) {
				auto iter = package.compressedEntries.find(CompressedEntry{entry.type, entry.group, entry.instance, entry.resource});
				if(entry.size > 9 && iter != package.compressedEntries.end()) {
					bytes header = readFile(file, entry.location, 9);
					
					if(header[4] == 0x10 && header[5] == 0xFB) {
						entry.compressed = true;
						entry.uncompressedSize = getUncompressedSize(header);
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
	void putPackage(fstream& newFile, fstream& oldFile, Package& package, Mode mode) {
		//write header
		bytes buffer = bytes(96);
		uint pos = 0;
		
		putInt32le(buffer, pos, 0x46504244);
		putInt32le(buffer, pos, package.header.majorVersion);
		putInt32le(buffer, pos, package.header.minorVersion);
		putInt32le(buffer, pos, package.header.majorUserVersion);
		putInt32le(buffer, pos, package.header.minorUserVersion);
		putInt32le(buffer, pos, package.header.flags);
		putInt32le(buffer, pos, package.header.createdDate);
		putInt32le(buffer, pos, package.header.modifiedDate);
		putInt32le(buffer, pos, package.header.indexMajorVersion);
		pos += 12; //skip index info, update later
		putInt32le(buffer, pos, 0);
		putInt32le(buffer, pos, 0);
		putInt32le(buffer, pos, 0);
		putInt32le(buffer, pos, package.header.indexMinorVersion);
		
		for(uint i = 0; i < package.header.remainder.size(); i++) {
			buffer[pos++] = package.header.remainder[i];
		}

		writeFile(newFile, buffer);

		//compress and write entries, and save the location and size for the index
		omp_lock_t lock;
		omp_init_lock(&lock);
		
		#pragma omp parallel for
		for(int i = 0; i < package.entries.size(); i++) {
			auto& entry = package.entries[i];
			
			omp_set_lock(&lock);
			bytes content = readFile(oldFile, entry.location, entry.size);
			omp_unset_lock(&lock);
			
			if(mode == DECOMPRESS) {
				content = decompressEntry(entry, content);
			} else if(mode == RECOMPRESS) {
				content = recompressEntry(entry, content);
			} else {
				content = compressEntry(entry, content);
			}
			
			entry.size = content.size();
			
			//we only care about the uncompressed size if the file is compressed
			if(entry.compressed) {
				entry.uncompressedSize = getUncompressedSize(content);
			}
			
			omp_set_lock(&lock);
			
			entry.location = newFile.tellp();
			writeFile(newFile, content);
			
			omp_unset_lock(&lock);
		}
		
		omp_destroy_lock(&lock);
		
		//make and write the directory of compressed files
		bytes clstContent;
		pos = 0;
		
		if(package.header.indexMinorVersion == 2) {
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

				if(package.header.indexMinorVersion == 2) {
					putInt32le(clstContent, pos, entry.resource);
				}
				
				putInt32le(clstContent, pos, entry.uncompressedSize);
			}
		}
		
		clst.size = pos;
		
		if(clst.size > 0) { 
			clstContent.resize(clst.size);
			writeFile(newFile, clstContent);
			package.entries.push_back(clst);
		}

		//write the index
		uint indexStart = newFile.tellp();
		
		if(package.header.indexMinorVersion == 2) {
			buffer = bytes(package.entries.size() * 4 * 6);
		} else {
			buffer = bytes(package.entries.size() * 4 * 5);
		}
		
		pos = 0;
		
		for(auto& entry: package.entries) {
			putInt32le(buffer, pos, entry.type);
			putInt32le(buffer, pos, entry.group);
			putInt32le(buffer, pos, entry.instance);

			if(package.header.indexMinorVersion == 2) {
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