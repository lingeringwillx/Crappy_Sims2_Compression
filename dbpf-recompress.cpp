#include "dbpf.h"

#include <execution>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

int main(int argc, char *argv[]) {
	if(argc == 1) {
		cout << "No argument provided";
		return 0;
	}
	
	bool recompress = false;
	bool decompress = false;
	bool parallel = false;
	int level = 5;
	
	string arg;
	for(int i = 0; i < argc; i++) {
		arg = argv[i];
		
		if(arg == "-h" || arg == "help") {
			cout << "    -r    recompress" << endl;
			cout << "    -p    parallel" << endl;
			cout << "    -l    level" << endl;
			cout << "    -d    decompress" << endl << endl;
			return 0;
		}
		
		else if(arg.find("-l") == 0) {
			level = (int) arg[2] - 48;
			
			if(level != 1 && level != 3 && level != 5 && level != 7 && level != 9) {
				cout << "Level " << level << " is not supported";
				return 0;
			}
			
		} else if(arg == "-r") {
			recompress = true;
			
		} else if(arg == "-d") {
			decompress = true;
			
		} else if(arg == "-p") {
			parallel = true;
		}
	}
	
	string fileName = argv[argc - 1];
	
	Timer timer = Timer();
	
	//open file
	ifstream file = ifstream(fileName, ios::binary);
	
	if(!file.is_open()) {
		cout << "Failed to open file";
		return 0;
	}
	
	//get file size
	file.seekg(0, ios::end);
	streampos fileSize = file.tellg();
	
	//read file to signedContent
	file.seekg(0, ios::beg);
	vector<char> signedContent = vector<char>(fileSize);
	file.read(&signedContent[0], fileSize);
	
	file.close();
	
	timer.log("File read");
	
	//convert to unsigned char, is there a faster way to do this?
	bytes content = bytes(signedContent.size());
	for(uint i = 0; i < signedContent.size(); i++) {
		content[i] = (unsigned char) signedContent[i];
	}
	
	timer.log("Cast to unsigned");
	
	//get package
	Package package = getPackage(content);
	
	timer.log("Unpack package");
	
	if(parallel) {
		//parallel compression of package
		for_each(execution::par, package.entries.begin(), package.entries.end(), [decompress, recompress, level](auto& entry) {
			if(recompress || decompress) {
				entry.decompressEntry();
			}
			
			if(!decompress || (decompress && recompress)) {
				entry.compressEntry(level);
			}
		});
	}
	
	else {
		for(auto& entry: package.entries) {
			if(recompress || decompress) {
				entry.decompressEntry();
			}
			
			if(!decompress || (decompress && recompress)) {
				entry.compressEntry(level);
			}
		}
	}
	
	timer.log("Compress");
	
	//convert to bytes
	content = putPackage(package);
	
	timer.log("Pack package");
	
	//convert to signed (is there a faster way to do this?)
	signedContent = vector<char>(content.size());

	for(uint i = 0; i < content.size(); i++) {
		signedContent[i] = (signed char) content[i];
	}
	
	timer.log("Cast to signed");
	
	//try to write to temp file
	ofstream tempFile = ofstream(fileName + ".new", ios::binary);
	if(tempFile.is_open()) {
		if(!tempFile.write(&signedContent[0], signedContent.size())) {
			cout << "Failed to write file";
		}
		tempFile.close();

	} else {
		cout << "Failed to open file";
	}
	
	//overwrite old file
	try {
		filesystem::rename(fileName + ".new", fileName);
	}
	
	catch(filesystem::filesystem_error) {
		cout << "Failed to replace file";
		return 0;
	}
	
	timer.log("Write file");
}