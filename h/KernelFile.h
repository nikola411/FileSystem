#ifndef KERNELFILE_H_
#define KERNELFILE_H_
#include "File.h"
#include <thread>
#include <windows.h>


class KernelFS;
struct FCB;
const long maxFileSize= 512 * 512 * 2048;


typedef unsigned long ClusterNo;



class KernelFile
{
public:
	
	~KernelFile(); //zatvaranje fajla
	
	char close();

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
	FCB *my_fcb;
	unsigned long cursor;//current position in file given as abosulte postition counted in bytes
	unsigned long end_of_file;//end of file given as absolute position of end counted in bytes

	unsigned long last_cluster[512];
	ClusterNo last_cluster_no;

	unsigned long index_1[512];
	ClusterNo index_1_no;

	unsigned long allocated_size;//allocated size in bytes
	unsigned long current_size;//current size in bytes, equal with end_of_file
	unsigned long index_1_entry;
	unsigned long index_2_entry;

	static KernelFS *my_fs;
	std::string file_name;

	std::thread::id parent_thread_id;
	KernelFile(FCB*, KernelFS*, std::string); //objekat fajla se može kreirati samo otvaranjem
	int allocate_clusters(long cluster_to_allocate);
	int write_in_file(BytesCnt, char*);
	char *file_name_converting();

	HANDLE mutex = CreateMutex(NULL, false, NULL);
	int write_cnt;//actually bool 
	int read_cnt;
	
};
#endif

