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
	const uint DBPF_MAGIC = 0x46504244; //"DBPF"
	const uint SIGNATURE = 0x35677262 ; //"brg5"
	
	uint getFileSize(fstream& file) {
		uint pos = file.tellg();
		file.seekg(0, ios::end);
		uint size = file.tellg();
		file.seekg(pos, ios::beg);
		return size;
	}
	
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
	uint getInt(bytes& buf, uint& pos) {
		return ((uint) buf[pos++]) + ((uint) buf[pos++] << 8) + ((uint) buf[pos++] << 16) + ((uint) buf[pos++] << 24);
	}

	//put integer in buf at pos and increment pos (little endian)
	void putInt(bytes& buf, uint& pos, uint n) {
		buf[pos++] = n;
		buf[pos++] = n >> 8;
		buf[pos++] = n >> 16;
		buf[pos++] = n >> 24;
	}

	//get the uncompressed size from the compression header (3 bytes big endian integer)
	uint getUncompressedSize(bytes& buf) {
		return ((uint) buf[6] << 16) + ((uint) buf[7] << 8) + ((uint) buf[8]);
	}
	
	/* compression mode
	COMPRESS: compress uncompress entries only
	DECOMPRESS: decompress the package
	RECOMPRESS: decompress the package's entries then compress them again, can result in better compression if the older compression is weak
	SKIP: don't do anything with the package
	VALIDATE: don't check for the compressor signature in getPackage
	
	SKIP and VALIDATE can't be used as a default modes as they are not supported by every part of the tool*/
	enum Mode { COMPRESS, DECOMPRESS, RECOMPRESS, SKIP, VALIDATE };
	
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
		uint holeIndexLocation;
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
	
	//representing a hole in the package file
	struct Hole {
		uint location;
		uint size;
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
		bool signature_in_package = false;
		Header header;
		vector<Entry> entries;
		vector<Hole> holes;
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
	Package getPackage(fstream& file, wstring displayPath, Mode mode) {
		file.seekg(0);
		uint fileSize = getFileSize(file);
		
		if(fileSize < 64) {
			wcout << displayPath << L": Header not found" << endl;
			return Package{false};
		}
		
		Package package = Package();
		
		//header
		bytes buffer = readFile(file, 0, 96);
		uint pos = 0;
		
		//package file magic header "DBPF" should be the first 4 bytes of any dbpf package file
		uint magic = getInt(buffer, pos);
		if(magic != DBPF_MAGIC) {
			wcout << displayPath << L": DBPF_MAGIC header not found" << endl;
			return Package{false};
		}
		
		package.header.majorVersion = getInt(buffer, pos);
		package.header.minorVersion = getInt(buffer, pos);
		package.header.majorUserVersion = getInt(buffer, pos);
		package.header.minorUserVersion = getInt(buffer, pos);
		package.header.flags = getInt(buffer, pos);
		package.header.createdDate = getInt(buffer, pos);
		package.header.modifiedDate = getInt(buffer, pos);
		package.header.indexMajorVersion = getInt(buffer, pos);
		package.header.indexEntryCount = getInt(buffer, pos);
		package.header.indexLocation = getInt(buffer, pos);
		package.header.indexSize = getInt(buffer, pos);
		package.header.holeIndexEntryCount = getInt(buffer, pos);
		package.header.holeIndexLocation = getInt(buffer, pos);
		package.header.holeIndexSize = getInt(buffer, pos);
		package.header.indexMinorVersion = getInt(buffer, pos);
		package.header.remainder = bytes(buffer.begin() + 64, buffer.end());
		
		/*valid Sims 2 package file header information is:
			major version = 1
			minor version = [0, 1, 2]
			index major version = 7
			index minor version = [0, 1, 2]
			
		if different values are encountered, then the package is likely a package file for another game*/
		
		if(package.header.majorVersion != 1 || (package.header.minorVersion != 0 && package.header.minorVersion != 1 && package.header.minorVersion != 2) || package.header.indexMajorVersion != 7) {
			wcout << displayPath << L": Not a Sims 2 package file" << endl;
			return Package{false};
		}
		
		if(package.header.indexMinorVersion > 2) {
			wcout << displayPath << L": Unrecognized index version" << endl;
			return Package{false};
		}
		
		//boundary checks
		if(package.header.indexLocation > fileSize || package.header.indexLocation + package.header.indexSize > fileSize) {
			wcout << displayPath << L": Entry index outside of bounds" << endl;
			return Package{false};
		}
		
		//check if the index entry count and the index size match up
		//NOTE: this is likely unnecessary but I'll still leave it there
		uint indexEntryCountToIndexSize = 0;
		if(package.header.indexMinorVersion == 2) {
			indexEntryCountToIndexSize = package.header.indexEntryCount * 4 * 6;
		} else {
			indexEntryCountToIndexSize = package.header.indexEntryCount * 4 * 5;
		}
		
		if(indexEntryCountToIndexSize > package.header.indexSize) {
			wcout << displayPath << L": Entry count larger than index size" << endl;
			return Package{false};
		}
		
		//boundary checks
		if(package.header.holeIndexLocation > fileSize || package.header.holeIndexLocation + package.header.holeIndexSize > fileSize) {
			wcout << displayPath << L": Hole index outside of bounds" << endl;
			return Package{false};
		}
		
		//check if the hole index entry count and the hole index size match up
		if(package.header.holeIndexEntryCount * 8 != package.header.holeIndexSize) {
			wcout << displayPath << L": Hole count larger than hole index size" << endl;
			return Package{false};
		}
		
		//holes
		buffer = readFile(file, package.header.holeIndexLocation, package.header.holeIndexSize);
		pos = 0;
		
		package.holes.reserve(package.header.holeIndexEntryCount);
		
		for(uint i = 0; i < package.header.holeIndexEntryCount; i++) {
			uint location = getInt(buffer, pos);
			uint size = getInt(buffer, pos);
			package.holes.push_back(Hole{location, size});
		}
		
		//check for compressor signature
		
		/*this is an optimization to skip package files that have already been compressed by this tool in the past
		a hole is added by the compressor containing information about the compression used by the compressor, plus the complete file size
			
		holes are junk data, a placeholder for when the game deletes a specific entry, and are ignored by the game and by most unpacking tools, however here we are exploiting them to store some information
		
		signature format is:
			DWORD signature = 0 if the file is decompressed, "brg5" if the file is compressed
			DWORD file size
			
		"brg5" refers to the compression algorithm used by this compressor, which is an implementation of EA's Refpack/QFS compression algorithm written by Ben Rudiak-Gould adjusted to use zlib's level 5 compression parameters
			
		if the signature is found and the file size has not changed then we can skip the file
		*/
		
		if(mode != VALIDATE && package.header.holeIndexEntryCount == 1 && package.holes[0].size == 8) {
			dbpf::Hole hole = package.holes[0];
			
			//boundary checks
			if(hole.location > fileSize || hole.location + hole.size > fileSize) {
				wcout << displayPath << L": Hole location outside of bounds" << endl;
				return Package{false}; 
			}
			
			buffer = dbpf::readFile(file, hole.location, 8);
			pos = 0;
			
			uint sig = dbpf::getInt(buffer, pos);
			uint fileSizeInHole = dbpf::getInt(buffer, pos);
			
			if(((mode != DECOMPRESS && sig == SIGNATURE) || (mode == DECOMPRESS && sig == 0)) && fileSizeInHole == fileSize) {
				//package has already been compressed/decompressed by this compressor in the past, skip
				return Package{false, true};
			}
		}
		
		//index
		buffer = readFile(file, package.header.indexLocation, package.header.indexSize);
		pos = 0;
		
		package.entries.reserve(package.header.indexEntryCount + 1);
		bytes clstContent;
		
		for(uint i = 0; i < package.header.indexEntryCount; i++) {
			uint type = getInt(buffer, pos);
			uint group = getInt(buffer, pos);
			uint instance = getInt(buffer, pos);
			uint resource = 0;

			if(package.header.indexMinorVersion == 2) {
				resource = getInt(buffer, pos);
			}

			uint location = getInt(buffer, pos);
			uint size = getInt(buffer, pos);
			
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
				uint type = getInt(clstContent, pos);
				uint group = getInt(clstContent, pos);
				uint instance = getInt(clstContent, pos);
				uint resource = 0;

				if(package.header.indexMinorVersion == 2) {
					resource = getInt(clstContent, pos);
				}
				
				uint uncompressedSize = getInt(clstContent, pos);
				package.compressedEntries.insert(CompressedEntry{type, group, instance, resource, uncompressedSize});
			}
			
			//check if entries are compressed
			for(auto& entry: package.entries) {
				auto iter = package.compressedEntries.find(CompressedEntry{entry.type, entry.group, entry.instance, entry.resource});
				if(entry.size > 9 && iter != package.compressedEntries.end()) {
					//this is slow, but unavoidable??
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
		
		putInt(buffer, pos, DBPF_MAGIC);
		putInt(buffer, pos, package.header.majorVersion);
		putInt(buffer, pos, package.header.minorVersion);
		putInt(buffer, pos, package.header.majorUserVersion);
		putInt(buffer, pos, package.header.minorUserVersion);
		putInt(buffer, pos, package.header.flags);
		putInt(buffer, pos, package.header.createdDate);
		putInt(buffer, pos, package.header.modifiedDate);
		putInt(buffer, pos, package.header.indexMajorVersion);
		pos += 24; //skip index and hole info, update later
		putInt(buffer, pos, package.header.indexMinorVersion);
		
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
				putInt(clstContent, pos, entry.type);
				putInt(clstContent, pos, entry.group);
				putInt(clstContent, pos, entry.instance);

				if(package.header.indexMinorVersion == 2) {
					putInt(clstContent, pos, entry.resource);
				}
				
				putInt(clstContent, pos, entry.uncompressedSize);
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
			putInt(buffer, pos, entry.type);
			putInt(buffer, pos, entry.group);
			putInt(buffer, pos, entry.instance);

			if(package.header.indexMinorVersion == 2) {
				putInt(buffer, pos, entry.resource);
			}

			putInt(buffer, pos, entry.location);
			putInt(buffer, pos, entry.size);
		}
		
		writeFile(newFile, buffer);
		uint indexEnd = newFile.tellp();
		
		//write compressor signature as a hole and write the hole index
		uint holeLocation = newFile.tellp();
		uint holeIndexLocation = holeLocation + 8;
		uint fileSize = holeLocation + 16;
		
		buffer = bytes(16);
		pos = 0;
		
		//hole
		if(mode == DECOMPRESS) {
			putInt(buffer, pos, 0);
		} else {
			putInt(buffer, pos, SIGNATURE);
		}
		
		putInt(buffer, pos, fileSize);
		
		//hole index
		putInt(buffer, pos, holeLocation);
		putInt(buffer, pos, 8); //hole size
		
		writeFile(newFile, buffer);

		//update the header with index info
		newFile.seekp(36);
		
		buffer = bytes(24);
		pos = 0;
		
		putInt(buffer, pos, package.entries.size()); //index entry count
		putInt(buffer, pos, indexStart); //index location
		putInt(buffer, pos, indexEnd - indexStart); //index size
		putInt(buffer, pos, 1); //hole index entry count
		putInt(buffer, pos, holeIndexLocation); //hole index location
		putInt(buffer, pos, 8); //hole index size
		
		writeFile(newFile, buffer);
	}
	
}

#endif
