#include "dbpf.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

int main(int argc, char *argv[]) {
	if(argc == 1) {
		cout << "No argument provided" << endl;
		return 0;
	}
	
	//parse args
	bool recompress = false;
	bool decompress = false;
	bool inParallel = false;
	bool quiet = false;
	int level = 5;
	
	string arg;
	for(int i = 0; i < argc; i++) {
		arg = argv[i];
		
		if(arg == "-h" || arg == "help") {
			cout << "  -l  level" << endl;
			cout << "  -p  parallel" << endl;
			cout << "  -r  recompress" << endl;
			cout << "  -d  decompress" << endl;
			cout << "  -q  quiet" << endl;
			cout << endl;
			return 0;
		}
		
		else if(arg.find("-l") == 0) {
			if(arg.size() < 3) {
				cout << "Compression level not specified" << endl;
				return 0;
			}

			level = arg[2] - 48;
			
			if(level != 1 && level != 3 && level != 5 && level != 7 && level != 9) {
				cout << "Level " << to_string(i) << " is not supported" << endl;
				return 0;
			}
			
		} else if(arg == "-r") {
			recompress = true;
			
		} else if(arg == "-d") {
			decompress = true;
			
		} else if(arg == "-p") {
			inParallel = true;
		
		} else if(arg =="-q") {
			quiet = true;
		}
	}
	
	if(decompress && recompress) {
		cout << "Cannot use arguments -d and -r at the same time" << endl;
		return 0;
	}
	
	string pathName = argv[argc - 1];

	if(pathName == "dbpf-recompress" || pathName.find("-") == 0) {
		cout << "No file path provided" << endl;
		return 0;
	}
	
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
		float current_size = dir_entry.file_size() / 1024;
		
		string displayPath; //for cout
		if(filesystem::is_regular_file(pathName)) {
			displayPath = fileName;
		} else {
			displayPath = filesystem::relative(fileName, pathName).string();
		}
		
		ifstream file = ifstream(fileName, ios::binary);
		
		if(!file.is_open()) {
			cout << displayPath << ": Failed to open file" << endl;
			continue;
		}
		
		//get package
		file.seekg(0, ios::beg);

		Package package = getPackage(file, displayPath);
		
		//error unpacking package
		if(package.indexVersion == -1) {
			file.close();
			continue;
		}
		
		//compress entries, pack package, and write to temp file
		ofstream tempFile = ofstream(fileName + ".new", ios::binary);
		if(tempFile.is_open()) {
			putPackage(tempFile, file, package, inParallel, decompress, recompress, level);
			tempFile.close();
			
		} else {
			cout << displayPath << ": Failed to create temp file" << endl;
			file.close();
			continue;
		}
		
		//validate new file
		Package oldPackage = getPackage(file, displayPath);
		ifstream newFile = ifstream(fileName + ".new", ios::binary);
		
		bool validationFailed = false;
		
		if(!newFile.is_open()) {
			cout << displayPath << ": Could not open new package file" << endl;
			validationFailed = true;
		}
		
		Package newPackage = getPackage(newFile, displayPath + ".new");
		
		if(!validationFailed && newPackage.indexVersion == -1) {
			cout << displayPath << ": Failed to load new package" << endl;
			validationFailed = true;
		}
		
		if(!validationFailed && (oldPackage.entries.size() != newPackage.entries.size())) {
			cout << displayPath << ": Number of entries between old package and new package not matching" << endl;
			validationFailed = true;
		}
		
		if(!validationFailed) {
			auto equal = EqualFunction();
			
			for(int i = 0; i < oldPackage.entries.size(); i++) {
				if(!(equal(oldPackage.entries[i], newPackage.entries[i]))) {
					cout << displayPath << ": Types, groups, instances, or resources of entries not matching" << endl;
					validationFailed = true;
					break;
				}
				
				bytes oldContent = read(file, oldPackage.entries[i].location, oldPackage.entries[i].size);
				bytes newContent = read(newFile, newPackage.entries[i].location, newPackage.entries[i].size);
				
				if(decompressEntry(oldPackage.entries[i], oldContent) != decompressEntry(newPackage.entries[i], newContent)) {
					cout << displayPath << ": Mismatch between old entry and new entry" << endl;
					validationFailed = true;
					break;
				}
			}
		}
		
		file.close();
		newFile.close();
		
		if(validationFailed) {
			try {
				filesystem::remove(fileName + ".new");
				
			} catch(filesystem::filesystem_error) {}
			
			continue;
		}
		
		//overwrite old file
		if(decompress || filesystem::file_size(fileName + ".new") < filesystem::file_size(fileName)) {
			try {
				filesystem::rename(fileName + ".new", fileName);
			}
			
			catch(filesystem::filesystem_error) {
				cout << displayPath << ": Failed to overwrite file" << endl;
				
				try {
					filesystem::remove(fileName + ".new");
					
				} catch(filesystem::filesystem_error) {}
				
				continue;
			}
			
		} else {
			cout << displayPath << ": New file is larger or equal in size to the old file" << endl;
			
			try {
				filesystem::remove(fileName + ".new");
				
			} catch(filesystem::filesystem_error) {}
			
			continue;
		}
		
		//output file size to console
		float new_size = filesystem::file_size(fileName) / 1024;
		
		if(!quiet) {
			cout << displayPath << " " << fixed << setprecision(2);
			
			if(current_size >= 1e3) {
				cout << current_size / 1024 << " MB";
			} else {
				cout << current_size << " KB";
			}
			
			cout << " -> ";
			
			if(new_size >= 1e3) {
				cout << new_size / 1024 << " MB";
			} else {
				cout << new_size << " KB";
			}
			
			cout << endl;
		}
	}
	
	if(!quiet) {
		cout << endl;
	}
	
	return 0;
}