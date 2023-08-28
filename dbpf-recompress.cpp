#include "dbpf.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

//for debugging
class Timer {
	public:
		chrono::time_point<std::chrono::steady_clock> t;
		
	Timer() {
		t = chrono::high_resolution_clock::now();
	}
	
	void log(string info)	{
		//uncomment to display timers
		int timeSpent = chrono::duration_cast<chrono::microseconds>(chrono::high_resolution_clock::now() - t).count();
		//cout << info << ": " << timeSpent << endl;
		t = chrono::high_resolution_clock::now();
	}
};

int main(int argc, char *argv[]) {
	if(argc == 1) {
		cout << "No argument provided" << endl;
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
			cout << "    -l    level" << endl;
			cout << "    -p    parallel" << endl;
			cout << "    -r    recompress" << endl;
			cout << "    -d    decompress" << endl;
			cout << endl;
			return 0;
		}
		
		else if(arg.find("-l") == 0) {
			if(arg.size() < 3) {
				cout << "Compression level not specified" << endl;
				return 0;
			}

			level = (int) arg[2] - 48;
			
			if(level != 1 && level != 3 && level != 5 && level != 7 && level != 9) {
				cout << "Level " << level << " is not supported" << endl;
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

	if(fileName == "dbpf-recompress" || fileName.find("-") == 0) {
		cout << "No file path provided" << endl;
		return 0;
	}

	try {
		if(!filesystem::is_regular_file(fileName)) {
			cout << "File not found" << endl;
			return 0;
		}

	} catch(filesystem::filesystem_error) {
		cout << "File system error" << endl;
		return 0;
	}

	int extensionLoc = fileName.find(".package");
	if(extensionLoc == -1 || extensionLoc != fileName.size() - 8) {
		cout << "Not a package file" << endl;
		return 0;
	}

	Timer timer = Timer();
	
	//open file
	ifstream file = ifstream(fileName, ios::binary);
	
	if(!file.is_open()) {
		cout << "Failed to open file" << endl;;
		return 0;
	}
	
	//get package
	file.seekg(0, ios::beg);

	Package package = getPackage(file);
	
	file.close();
	
	//error unpacking package
	if(package.indexVersion == -1) {
		return 0;
	}

	timer.log("Unpack package");
	
	if(parallel) {
		//parallel compression of package
		#pragma omp parallel for
		for(int i = 0; i < package.entries.size(); i++) {
			if(recompress || decompress) {
				package.entries[i].decompressEntry();
			}
			
			if(!decompress || (decompress && recompress)) {
				package.entries[i].compressEntry(level);
			}
		}
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
	bytes content = putPackage(package);
	
	timer.log("Pack package");
	
	//try to write to temp file
	ofstream tempFile = ofstream(fileName + ".new", ios::binary);
	if(tempFile.is_open()) {
		if(!tempFile.write(reinterpret_cast<char *>(content.data()), content.size())) {
			cout << "Failed to write to file" << endl;
			tempFile.close();
			return 0;
		}

		tempFile.close();

	} else {
		cout << "Failed to open file" << endl;
		return 0;
	}
	
	//overwrite old file
	try {
		filesystem::rename(fileName + ".new", fileName);
	}
	
	catch(filesystem::filesystem_error) {
		cout << "Failed to replace file" << endl;
		return 0;
	}
	
	timer.log("Write file");
}