#ifndef FILE_H_
#define FILE_H_
#include "FS.h"

class KernelFile;
class File {
public:
	

	~File(); //zatvaranje fajla
	char write(BytesCnt, char* buffer);
	BytesCnt read(BytesCnt, char* buffer);
	char seek(BytesCnt);
	BytesCnt filePos();
	char eof();
	BytesCnt getFileSize();
	char truncate();

private:

	friend class FS;
	friend class KernelFS;
	File(); //objekat fajla se može kreirati samo otvaranjem
	KernelFile *myImpl;
};
#endif


