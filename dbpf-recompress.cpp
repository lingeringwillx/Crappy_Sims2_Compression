#include "dbpf.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

void tryDelete(string fileName) {
	try { filesystem::remove(fileName); }
	catch(filesystem::filesystem_error) {}
}

int main(int argc, char *argv[]) {
	if(argc == 1) {
		cout << "No arguments provided" << endl;
		return 0;
	}
	
	//parse args
	string arg = argv[1];
	
	if(arg == "help") {
		cout << "dbpf-recompress.exe -args package_file_or_folder" << endl;
		cout << "  -d  decompress" << endl;
		cout << endl;
		return 0;
	}
	
	dbpf::Mode mode = dbpf::COMPRESS;
	int fileArgIndex = 1;
	
	if(arg == "-d") {
		mode = dbpf::DECOMPRESS;
		fileArgIndex = 2;
	}
	
	if(fileArgIndex > argc - 1) {
		cout << "No file path provided" << endl;
		return 0;
	}
	
	string pathName = argv[fileArgIndex];
	
	auto files = vector<filesystem::directory_entry>();
	
	if(filesystem::is_regular_file(pathName)) {
		auto file_entry = filesystem::directory_entry(pathName);
		if(file_entry.path().extension() != ".package") {
			cout << "Not a package file" << endl;
			return 0;
		}
		
		files.push_back(file_entry);
		
	} else if(filesystem::is_directory(pathName)) {
		
		for(auto& dir_entry: filesystem::recursive_directory_iterator(pathName)) {
			if(dir_entry.is_regular_file() && dir_entry.path().extension() == ".package") {
				files.push_back(dir_entry);
			}
		}
		
	} else {
		cout << "File not found" << endl;
		return 0;
	}
	
	for(auto& dir_entry: files) {
		//open file
		string fileName = dir_entry.path().string();
		string tempFileName = fileName + ".new";
		
		float current_size = dir_entry.file_size() / 1024.0;
		
		string displayPath; //for cout
		if(filesystem::is_regular_file(pathName)) {
			displayPath = fileName;
		} else {
			displayPath = filesystem::relative(fileName, pathName).string();
		}
		
		fstream file = fstream(fileName, ios::in | ios::binary);
		
		if(!file.is_open()) {
			cout << displayPath << ": Failed to open file" << endl;
			continue;
		}
		
		//get package
		dbpf::Package oldPackage = dbpf::getPackage(file, displayPath);
		dbpf::Package package = dbpf::Package{oldPackage.indexVersion, oldPackage.entries};
		
		//error unpacking package
		if(package.indexVersion == -1) {
			file.close();
			continue;
		}
		
		//find proper compression mode
		if(mode != dbpf::DECOMPRESS) {
			bool all_entries_compressed = true;
			bool compression_can_improve = false;
			
			for(auto& entry: package.entries) {
				if(!entry.compressed) {
					all_entries_compressed = false;
					break;
				}
			}
			
			//try to recompress one large entry that's already compressed and find out if we're gonna get a smaller size
			for(auto entry: package.entries) {
				if(entry.compressed && entry.uncompressedSize >= 100000) {
					bytes content = dbpf::readFile(file, entry.location, entry.size);
					content = dbpf::recompressEntry(entry, content);
						
					if(content.size() < entry.size) {
						compression_can_improve = true;
					}
					
					break;
				}
			}
			
			if(compression_can_improve) {
				mode = dbpf::RECOMPRESS;
			} else {
				if(all_entries_compressed) {
					mode = dbpf::SKIP;
				} else {
					mode = dbpf::COMPRESS;
				}
			}
		}
		
		if(mode != dbpf::SKIP) {
			//compress entries, pack package, and write to temp file
			fstream tempFile = fstream(tempFileName, ios::in | ios::out | ios::binary | ios::trunc);
			
			if(tempFile.is_open()) {
				dbpf::putPackage(tempFile, file, package, mode);
				
			} else {
				cout << displayPath << ": Failed to create temp file" << endl;
				file.close();
				continue;
			}
			
			//validate new file
			tempFile.seekg(0, ios::beg);
			dbpf::Package newPackage = dbpf::getPackage(tempFile, displayPath + ".new");
			
			bool validationFailed = false;
			
			if(!validationFailed && newPackage.indexVersion == -1) {
				cout << displayPath << ": Failed to load new package" << endl;
				validationFailed = true;
			}
			
			if(!validationFailed && (oldPackage.entries.size() != newPackage.entries.size())) {
				cout << displayPath << ": Number of entries between old package and new package not matching" << endl;
				validationFailed = true;
			}
			
			if(!validationFailed) {
				for(int i = 0; i < oldPackage.entries.size(); i++) {
					auto& oldEntry = oldPackage.entries[i];
					auto& newEntry = newPackage.entries[i];
					
					if(oldEntry.type != newEntry.type || oldEntry.group != newEntry.group || oldEntry.instance != newEntry.instance || oldEntry.resource != newEntry.resource) {
						cout << displayPath << ": Types, groups, instances, or resources of entries not matching" << endl;
						validationFailed = true;
						break;
					}
					
					bytes oldContent = dbpf::readFile(file, oldEntry.location, oldEntry.size);
					bytes newContent = dbpf::readFile(tempFile, newEntry.location, newEntry.size);
					
					oldContent = dbpf::decompressEntry(oldEntry, oldContent);
					newContent = dbpf::decompressEntry(newEntry, newContent);
					
					if(oldContent != newContent) {
						cout << displayPath << ": Mismatch between old entry and new entry" << endl;
						validationFailed = true;
						break;
					}
				}
			}
			
			file.close();
			tempFile.close();
			
			if(validationFailed) {
				tryDelete(tempFileName);
				continue;
			}
			
			float new_size = filesystem::file_size(tempFileName) / 1024.0;
			
			//overwrite old file
			if(mode == dbpf::DECOMPRESS || new_size < current_size) {
				try {
					filesystem::rename(tempFileName, fileName);
				}
				
				catch(filesystem::filesystem_error) {
					cout << displayPath << ": Failed to overwrite file" << endl;
					tryDelete(tempFileName);
					continue;
				}
				
			} else {
				tryDelete(tempFileName);
			}
		}
		
		if(mode == dbpf::SKIP) {
			file.close();
		}
		
		float new_size = filesystem::file_size(fileName) / 1024.0;
		
		//output file size to console
		cout << displayPath << " " << fixed << setprecision(2);
		
		if(current_size >= 1000) {
			cout << current_size / 1024.0 << " MB";
		} else {
			cout << current_size << " KB";
		}
		
		cout << " -> ";
		
		if(new_size >= 1000) {
			cout << new_size / 1024.0 << " MB";
		} else {
			cout << new_size << " KB";
		}
		
		cout << endl;
	}
	
	cout << endl;
	return 0;
}
