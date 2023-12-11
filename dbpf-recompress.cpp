#include "dbpf.h"

#include <fcntl.h>
#include <io.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

bool validatePackage(dbpf::Package& oldPackage, dbpf::Package& newPackage, fstream& oldFile, fstream& newFile, wstring displayPath, dbpf::Mode mode);

//trys to delete a file, fails silently
void tryDelete(wstring fileName) {
	try { filesystem::remove(fileName); }
	catch(filesystem::filesystem_error) {}
}

//using wide chars and wide strings to support UTF-16 file names
int wmain(int argc, wchar_t *argv[]) {
	_setmode(_fileno(stdout), _O_U16TEXT); //fix for wcout
	
	if(argc == 1) {
		wcout << L"No arguments provided" << endl;
		return 0;
	}
	
	//parse args
	wstring arg = argv[1];
	
	if(arg == L"help") {
		wcout << L"dbpf-recompress.exe -args package_file_or_folder" << endl;
		wcout << L"  -d  decompress" << endl;
		wcout << endl;
		return 0;
	}
	
	dbpf::Mode default_mode = dbpf::RECOMPRESS;
	int fileArgIndex = 1;
	
	if(arg == L"-d") {
		default_mode = dbpf::DECOMPRESS;
		fileArgIndex = 2;
	}
	
	if(fileArgIndex > argc - 1) {
		wcout << L"No file path provided" << endl;
		return 0;
	}
	
	wstring pathName = argv[fileArgIndex];
	
	auto files = vector<filesystem::directory_entry>();
	bool is_dir = false;
	
	if(filesystem::is_regular_file(pathName)) {
		auto file_entry = filesystem::directory_entry(pathName);
		if(file_entry.path().extension() != ".package") {
			wcout << L"Not a package file" << endl;
			return 0;
		}
		
		files.push_back(file_entry);
		
	} else if(filesystem::is_directory(pathName)) {
		is_dir = true;
		for(auto& dir_entry: filesystem::recursive_directory_iterator(pathName)) {
			if(dir_entry.is_regular_file() && dir_entry.path().extension() == ".package") {
				files.push_back(dir_entry);
			}
		}
		
	} else {
		wcout << L"File not found" << endl;
		return 0;
	}
	
	for(auto& dir_entry: files) {
		auto mode = default_mode;
		
		//open file
		wstring fileName = dir_entry.path().wstring();
		wstring tempFileName = fileName + L".new";
		
		float current_size = dir_entry.file_size() / 1024.0;
		
		wstring displayPath; //for cout
		
		if(is_dir) {
			displayPath = filesystem::relative(fileName, pathName).wstring();
		} else {
			displayPath = fileName;
		}
		
		fstream file = fstream(fileName, ios::in | ios::binary);
		
		if(!file.is_open()) {
			wcout << displayPath << L": Failed to open file" << endl;
			continue;
		}
		
		//get package
		dbpf::Package package = dbpf::getPackage(file, displayPath, mode);
		dbpf::Package oldPackage = package; //copy
		
		//optimization: if the package file has the compressor's signature then skip it
		if(mode == dbpf::RECOMPRESS && package.signature_in_package) {
			mode = dbpf::SKIP;
			file.close();
		}
		
		//error unpacking package, getPackage already prints an error so there is no need to print one here
		if(!package.unpacked) {
			file.close();
			continue;
		}
		
		//optimization: for DECOMPRESS mode skip the package file if all of it's entries are decompressed
		if(mode == dbpf::DECOMPRESS) {
			bool all_entries_decompressed = true;
			
			for(auto& entry: package.entries) {
				if(entry.compressed) {
					all_entries_decompressed = false;
					break;
				}
			}
			
			if(all_entries_decompressed) {
				mode = dbpf::SKIP;
				file.close();
			}
		}
		
		if(mode != dbpf::SKIP) {
			//compress entries, pack package, and write to temp file
			fstream tempFile = fstream(tempFileName, ios::in | ios::out | ios::binary | ios::trunc);
			
			if(tempFile.is_open()) {
				dbpf::putPackage(tempFile, file, package, mode);
				
			} else {
				wcout << displayPath << L": Failed to create temp file" << endl;
				file.close();
				continue;
			}
			
			//validate new file
			tempFile.seekg(0, ios::beg);
			dbpf::Package newPackage = dbpf::getPackage(tempFile, tempFileName, mode);
			bool is_valid = validatePackage(oldPackage, newPackage, file, tempFile, displayPath, mode);
			
			file.close();
			tempFile.close();
			
			if(!is_valid) {
				tryDelete(tempFileName);
				continue;
			}
			
			float new_size = filesystem::file_size(tempFileName) / 1024.0;
			
			//overwrite old file
			try {
				filesystem::rename(tempFileName, fileName);
			}
			
			catch(filesystem::filesystem_error) {
				wcout << displayPath << L": Failed to overwrite file" << endl;
				tryDelete(tempFileName);
				continue;
			}
		}
		
		float new_size = filesystem::file_size(fileName) / 1024.0;
		
		//output file size to console
		wcout << displayPath << L" " << fixed << setprecision(2);
		
		if(current_size >= 1000) {
			wcout << current_size / 1024.0 << L" MB";
		} else {
			wcout << current_size << L" KB";
		}
		
		wcout << " -> ";
		
		if(new_size >= 1000) {
			wcout << new_size / 1024.0 << L" MB";
		} else {
			wcout << new_size << L" KB";
		}
		
		wcout << endl;
	}
	
	wcout << endl;
	return 0;
}

//checks if the new package file is valid
bool validatePackage(dbpf::Package& oldPackage, dbpf::Package& newPackage, fstream& oldFile, fstream& newFile, wstring displayPath, dbpf::Mode mode) {
	//package unpacking failed, getPackage already prints an error
	if(!newPackage.unpacked) {
		return false;
	}
	
	//compare headers
	bytes oldHeader = dbpf::readFile(oldFile, 0, 96);
	bytes newHeader = dbpf::readFile(newFile, 0, 96);
	
	if(bytes(oldHeader.begin(), oldHeader.begin() + 36) != bytes(newHeader.begin(), newHeader.begin() + 36)
	|| bytes(oldHeader.begin() + 60, oldHeader.end()) != bytes(newHeader.begin() + 60, newHeader.end())) {
		wcout << displayPath << L": New header does not match the old header" << endl;
		return false;
	}
	
	if(mode == dbpf::RECOMPRESS) {
		//should only have one hole for the compressor signature
		if(newPackage.header.holeIndexEntryCount != 1) {
			wcout << displayPath << L": Wrong hole index count" << endl;
			return false;
		}
		
		//one hole index entry is 8 bytes long
		if(newPackage.header.holeIndexSize != 8) {
			wcout << displayPath << L": Wrong hole index size" << endl;
			return false;
		}
		
		dbpf::Hole hole = newPackage.holes[0];
		
		//compressor signature is 8 bytes long
		if(hole.size != 8) {
			wcout << L": Wrong hole size" << endl;
			return false;
		}
		
		bytes holeData = dbpf::readFile(newFile, hole.location, 8);
		uint pos = 0;
		
		uint sig = dbpf::getInt(holeData, pos);
		
		//if the file was compressed then the signature should be "BRG5"
		if(sig != dbpf::SIGNATURE) {
			wcout << displayPath << L": Compressor signature not found" << endl;
			return false;
		}
		
		uint fileSizeInHole = dbpf::getInt(holeData, pos);
		uint fileSize = dbpf::getFileSize(newFile);
		
		//file size written in the hole should match the actual file size
		if(fileSizeInHole != fileSize) {
			wcout << displayPath << L": File size in signature does not match the actual file size" << endl;
			return false;
		}
	}
	
	//should have the exact number of entries as the original package
	//NOTE: getPackage does not include the directory of compressed files entry in the entries vector for both packages
	if(oldPackage.entries.size() != newPackage.entries.size()) {
		wcout << displayPath << L": Number of entries between old package and new package not matching" << endl;
		return false;
	}
	
	//compare entries
	for(uint i = 0; i < oldPackage.entries.size(); i++) {
		auto& oldEntry = oldPackage.entries[i];
		auto& newEntry = newPackage.entries[i];
		
		//compare TGIRs
		if(oldEntry.type != newEntry.type || oldEntry.group != newEntry.group || oldEntry.instance != newEntry.instance || oldEntry.resource != newEntry.resource) {
			wcout << displayPath << L": Types, groups, instances, or resources of entries not matching" << endl;
			return false;
		}
		
		//check entry content
		bytes oldContent = dbpf::readFile(oldFile, oldEntry.location, oldEntry.size);
		bytes newContent = dbpf::readFile(newFile, newEntry.location, newEntry.size);
		
		//compression info in the directory of compressed files should match the information in the compression header
		bool compressed_in_header = newContent[4] == 0x10 && newContent[5] == 0xFB;
		bool in_clst = newPackage.compressedEntries.find(dbpf::CompressedEntry{newEntry.type, newEntry.group, newEntry.instance, newEntry.resource}) != newPackage.compressedEntries.end();
		
		if(compressed_in_header != in_clst) {
			wcout << displayPath << L": Incorrect compression information" << endl;
			return false;
		}
		
		//the compressor should only produce compressed entries that are smaller than the original decompressed entries
		if(newEntry.compressed) {
			uint tempPos = 0;
			uint uncompressedSize = dbpf::getUncompressedSize(newContent);
			uint compressedSize = dbpf::getInt(newContent, tempPos);
			
			if(compressedSize > uncompressedSize) {
				wcout << displayPath << L": Compressed size is larger than the uncompressed size for one entry" << endl;
				return false;
			}
		}
		
		//decompress the entries and compare them
		oldContent = dbpf::decompressEntry(oldEntry, oldContent);
		newContent = dbpf::decompressEntry(newEntry, newContent);
		
		if(oldContent != newContent) {
			wcout << displayPath << L": Mismatch between old entry and new entry" << endl;
			return false;
		}
	}
	
	//if all passes then return true
	return true;
}