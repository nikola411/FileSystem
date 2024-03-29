#ifndef KERNELFS_H_
#define KERNELFS_H_
#include <windows.h>
#include "FS.h"
#include <unordered_map>

typedef unsigned long ClusterNo;
const unsigned long ClusterSize = 2048;

class KernelFS;
class Partition;
class File;
class KernelFile;
class RWLockHandle;


struct FCB {

	char name[8] = { 0 };
	char ext[3] = { 0 };
	char notUsed = 0;
	ClusterNo index0 = 0;
	unsigned int fileSize = 0;
	char empty[12] = { 0 };
};

struct Lock {
	RWLockHandle *lock;
	int cnt = 0;//how many reading
};


class KernelFS
{
public:
	friend class FS;
	~KernelFS();
	KernelFS();
	char mount(Partition* partition); //montira particiju
											 // vraca 0 u slucaju neuspeha ili 1 u slucaju uspeha
	char unmount(); //demontira particiju
						   // vraca 0 u slucaju neuspeha ili 1 u slucaju uspeha
	char format(); //formatira particiju;
						  // vraca 0 u slucaju neuspeha ili 1 u slucaju uspeha
	long readRootDir();
	// vraca -1 u slucaju neuspeha ili broj fajlova u slucaju uspeha
	char doesExist(char* fname); //argument je naziv fajla sa
										//apsolutnom putanjom

	File* open(char* fname, char mode);

	char deleteFile(char* fname);

private:
	friend class KernelFile;

	int readClusterFromPart(ClusterNo, char*);
	int writeClusterToPart(ClusterNo,char*);
	
	Partition *my_partition;
	char *bit_vector;
	int bit_vector_size;
	int bit_vector_cluster_count;

	unsigned long root_index[512];

	ClusterNo first_free;
	ClusterNo root_index_entry;
	ClusterNo root_index_2_entry;

	ClusterNo file_table_entry;

	static std::unordered_map<std::string, Lock*> open_files_map;
	
	int * fileLocation(char* fname);
	//delete file takes file coordinates as argument
	int deleteFile(int*);
	static char ** split(char*);
	int update_fcb(char* file_name, FCB* fcb);

	File * openR(char*, int*);
	File * openW(char*);
	File * openA(char*,int*);

	ClusterNo allocateCluster();
	void deallocateCluster(ClusterNo);
	//splits file name into two strings : 1. name and 2. extension
	int checkCluster(ClusterNo);
	//flags
	bool mounting;
	bool formatting;
	bool unmounting; 

	HANDLE mutex; // wait for critical sections
	HANDLE mount_sem ;//we wait on this mutex we there are open files
	HANDLE format_sem;//waitin when open files cnt  > 0
	HANDLE unmount_sem;
	HANDLE rw_mutex;
	HANDLE alloc_mutex;
	
};


#endif

