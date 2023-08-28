#include "dbpf.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
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
				cout << "Level " << level << " is not supported" << endl;
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
		cout << "No file path provided" << endl;
		return 0;
	}
	
	vector<filesystem::directory_entry> files = vector<filesystem::directory_entry>();
	
	try {
		if(filesystem::is_regular_file(pathName)) {
			int extensionLoc = pathName.find(".package");
			if(extensionLoc == -1 || extensionLoc != pathName.size() - 8) {
				cout << "Not a package file" << endl;
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
			cout << "File not found" << endl;
			return 0;
		}
		
	} catch(filesystem::filesystem_error) {
		cout << "File system error" << endl;
		return 0;
	}

	Timer timer = Timer();
	
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
			cout << displayPath << ": Failed to open file" << endl;;
			continue;
		}
		
		//get package
		file.seekg(0, ios::beg);

		Package package = getPackage(file, displayPath);
		
		file.close();
		
		//error unpacking package
		if(package.indexVersion == -1) {
			continue;
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
				cout << displayPath << ": Failed to write to file" << endl;
				tempFile.close();
				
				try {
					filesystem::remove(fileName + ".new");
					
				} catch(filesystem::filesystem_error) {}
				
				continue;
			}

			tempFile.close();

		} else {
			cout << displayPath << ": Failed to create temp file" << endl;
			continue;
		}
		
		//overwrite old file
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
		
		timer.log("Write file");
		
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
		system("pause");
		cout << endl;
	}
	
	return 0;
}