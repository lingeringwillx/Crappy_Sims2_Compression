#include "dbpf.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

int main(int argc, char *argv[]) {
	//remove last error log
	if(filesystem::is_regular_file("errors.txt")) {
		try {
			filesystem::remove("errors.txt");
		} catch(filesystem::filesystem_error) {}
	}
	
	if(argc == 1) {
		log_error("No argument provided");
		return 0;
	}
	
	//parse args
	bool recompress = false;
	bool decompress = false;
	bool parallel = false;
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
				log_error("Compression level not specified");
				return 0;
			}

			level = arg[2] - 48;
			
			if(level != 1 && level != 3 && level != 5 && level != 7 && level != 9) {
				log_error("Level " + to_string(i) + " is not supported");
				return 0;
			}
			
		} else if(arg == "-r") {
			recompress = true;
			
		} else if(arg == "-d") {
			decompress = true;
			
		} else if(arg == "-p") {
			parallel = true;
		
		} else if(arg =="-q") {
			quiet = true;
		}
	}

	string pathName = argv[argc - 1];

	if(pathName == "dbpf-recompress" || pathName.find("-") == 0) {
		log_error("No file path provided");
		return 0;
	}
	
	vector<filesystem::directory_entry> files = vector<filesystem::directory_entry>();
	
	if(filesystem::is_regular_file(pathName)) {
		int extensionLoc = pathName.find(".package");
		if(extensionLoc == -1 || extensionLoc != pathName.size() - 8) {
			log_error("Not a package file");
			return 0;
		}
		
		files.push_back(filesystem::directory_entry(pathName));
		
	} else if(filesystem::is_directory(pathName)) {
		
		for(auto& dir_entry: filesystem::recursive_directory_iterator(pathName)) {
			if(dir_entry.is_regular_file() && dir_entry.path().extension() == ".package") {
				files.push_back(dir_entry);
			}
		}
		
	} else {
		log_error("File not found");
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
			log_error(displayPath + ": Failed to open file");
			continue;
		}
		
		//get package
		file.seekg(0, ios::beg);

		Package package = getPackage(file, displayPath);
		
		//error unpacking package
		if(package.indexVersion == -1) {
			continue;
		}
		
		//compress entries, pack package, and write to temp file
		ofstream tempFile = ofstream(fileName + ".new", ios::binary);
		if(tempFile.is_open()) {
			putPackage(tempFile, file, package, parallel, decompress, recompress, level);
			file.close();
			tempFile.close();
			
		} else {
			log_error(displayPath + ": Failed to create temp file");
			continue;
		}
		
		//overwrite old file
		try {
			filesystem::rename(fileName + ".new", fileName);
		}
		
		catch(filesystem::filesystem_error) {
			log_error(displayPath + ": Failed to overwrite file");
			
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