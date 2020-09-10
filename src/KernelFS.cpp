#include "stdafx.h"
#include "KernelFS.h"
#include "part.h"
#include <memory>
#include "KernelFile.h"
#include "File.h"
#include "RWLockHandle.h"

#include <iostream>
#include "stdio.h"

std::unordered_map<std::string, Lock*> KernelFS::open_files_map = {};


KernelFS::KernelFS() {
	my_partition = nullptr;
	bit_vector = nullptr;
	bit_vector_size = 0;
	bit_vector_cluster_count = 0;
	root_index[512] = { 0 };
	first_free = 0;
	root_index_entry = 0;
	root_index_2_entry = 0;
	file_table_entry = 0;
	mounting = false;
	formatting = false;
	mutex = CreateSemaphore(NULL, 1,1, NULL);
	mount_sem = CreateSemaphore(NULL, 1,32, NULL);
	format_sem = CreateSemaphore(NULL, 1,32, NULL);
	unmount_sem = CreateSemaphore(NULL, 0, 1, NULL);
	rw_mutex = CreateSemaphore(NULL, 1, 1, NULL);
	alloc_mutex = CreateSemaphore(NULL, 1, 1, NULL);

}

KernelFS::~KernelFS() {
	delete my_partition;
	delete[] bit_vector;
	bit_vector_size = 0;
	bit_vector_cluster_count = 0;
	root_index[512] = { 0 };
	first_free = 0;
	root_index_entry = 0;
	root_index_2_entry = 0;
	file_table_entry = 0;
	
}


char KernelFS::mount(Partition * partition)
{
	WaitForSingleObject(mutex, INFINITE);
	if (partition != nullptr) {
		mounting = true;
		ReleaseSemaphore(mutex,1,NULL);
		WaitForSingleObject(mount_sem, INFINITE);
		WaitForSingleObject(mutex, INFINITE);
	}
	
	//mount partition
	std::cout << "MOUNTING...\n";
	if (partition == nullptr) {
		std::cout << "NULL POINTER! \n";
		ReleaseSemaphore(mutex, 1, NULL);
		return 0;
	}
	my_partition = partition;
	//initialize bit vector
	bit_vector_size = my_partition->getNumOfClusters()/8 + (my_partition->getNumOfClusters() % 8 != 0 ? 1 : 0);
	bit_vector = new char[bit_vector_size];
	
	bit_vector_cluster_count = bit_vector_size / ClusterSize + (bit_vector_size % ClusterSize != 0 ? 1 : 0);
	std::cout << "BIT VECTOR CLUSTER COUNT : " << bit_vector_cluster_count<<std::endl;

	for (int i = 0; i < bit_vector_cluster_count; i++) {
		char buffer[ClusterSize] = { 0 };
		int ret = my_partition->readCluster(i, buffer);
		if (ret == 0) {
			std::cout << "CANNOT READ FROM PARTITION! \n";
			ReleaseSemaphore(mutex, 1, NULL);
			return 0;
		}
		for (int j = 0; j < ClusterSize; j++) {
			if (i*ClusterSize + j < bit_vector_size) {
				bit_vector[i*ClusterSize + j] = buffer[j];
			}
			
		}
	}
	//initialize root directory index
	int ret = my_partition->readCluster(bit_vector_cluster_count + 1, (char*)root_index);

	first_free = bit_vector_cluster_count + 2;

	root_index_entry = 0;
	root_index_2_entry = 0;
	file_table_entry = 0;
	
	if (ret == 0) {
		std::cout << "CANNOT READ FROM PARTITION! \n";
		ReleaseSemaphore(mutex, 1, NULL);
		return 0;
	}
	mounting = false;
	ReleaseSemaphore(mutex, 1, NULL);
	return 1;
}

char KernelFS::unmount()
{
	WaitForSingleObject(mutex, INFINITE);
	if (my_partition == nullptr) {
		ReleaseSemaphore(mutex, 1, NULL);
		return 1;
	}
	if (open_files_map.size()) {
		unmounting = true;
		ReleaseSemaphore(mutex, 1, NULL);

		WaitForSingleObject(unmount_sem, INFINITE);
		WaitForSingleObject(mutex, INFINITE);

	}
	
	my_partition = nullptr;
	for (int i = 0; i < bit_vector_size; i++) {
		bit_vector[i] = 0;
	}	
	for (int i = 0; i < 512; i++) {
		root_index[i] = 0;		
	}
	unmounting = false;
	ReleaseSemaphore(unmount_sem, 1, 0);
	if (mounting == true) {
		ReleaseSemaphore(mount_sem, 1, NULL);
	}
	ReleaseSemaphore(mutex, 1, NULL);
	
	return 1;
}

char KernelFS::format()
{
	//
	//if(open files)wait
	//signal sem for opening files to not allow opening more files
	WaitForSingleObject(mutex, INFINITE);

	if (open_files_map.size()) {
		formatting = true;

		ReleaseSemaphore(mutex, 1, NULL);
		WaitForSingleObject(format_sem, INFINITE);
		WaitForSingleObject(mutex, INFINITE);
	}
	std::cout << "FORMATING \n";
	char buffer[ClusterSize] = { 0 };

	int pos_1 = bit_vector_cluster_count / 8;
	int pos_2 = bit_vector_cluster_count % 8;
	
	int pos_2_value = 0;
	for (int i = 0; i < pos_2; i++) {
		
		pos_2_value = pos_2_value >> 1;
		pos_2_value += 0x80;
		
	}

	for (int i = 0; i < bit_vector_size; i++) {
		if (i < 512) {
			root_index[i] = 0;
		}
		if (i < pos_1) {
			bit_vector[i] = 0xFF;
		}
		else if (i == pos_1) {
			bit_vector[i] = pos_2_value;
		}
		else {
			bit_vector[i] = 0;
		}	
	}
	int ret;
	for (int i = 0; i < bit_vector_cluster_count; i++) {

		ret = my_partition->writeCluster(i, (char*)buffer);
		if (ret == 0) {
			ReleaseSemaphore(mutex, 1, NULL);
			return 0;
		}
	}
	ReleaseSemaphore(mutex, 1, NULL);
	ReleaseSemaphore(format_sem, 1, 0);
	return 1;
}

long KernelFS::readRootDir()
{
	if (my_partition == nullptr) {
		return -1;
	}else {
		FCB zero_fcb;
		int ret = 1;
		int files_on_disk = 0;
		for (int i = 0; i < 512; i++) {
			if (root_index[i] != 0) {
				//going through first-level index
				ClusterNo to_read = root_index[i];
				unsigned long index2[512] = { 0 };
				
				ret = my_partition->readCluster(to_read, (char*)index2);
				if (ret == 0) {
					std::cout << "NULLPTRS";
					return 0;
				}
				for (int j = 0; j < 512; j++) {
					
					if (index2[j] != 0) {
						//going through second level index
						FCB file_table[ClusterSize / 32] = { zero_fcb };
						
						ret = my_partition->readCluster(index2[j], (char*)file_table);
						if (ret == 0) {
							return 0;
						}
						for (int k = 0; k < ClusterSize / 32; k++) {

							if (std::memcmp(&file_table[k], &zero_fcb, sizeof(FCB))) {
								files_on_disk++;
							}
						}
					}
				}
			}
		}

		return files_on_disk;
	}
	
	
}

char KernelFS::doesExist(char * fname)
{
	WaitForSingleObject(mutex, INFINITE);
	int * ret = fileLocation(fname);
	ReleaseSemaphore(mutex, 1, NULL);
	if (ret == nullptr) {
		return 0;
	} else {
		return 1;
	}
}

File * KernelFS::open(char * fname, char mode)
{
	//check if file is in open files map
	std::string key(fname);
	WaitForSingleObject(mutex, INFINITE);
	
	auto search = KernelFS::open_files_map.find(key);
	if (search != open_files_map.end()) {
		
		if (mode == 'r' && search->second->cnt) {
			search->second->cnt++;
			ReleaseSemaphore(mutex, 1, NULL);
			search->second->lock->acquireRWLockShared();
			WaitForSingleObject(mutex, INFINITE);

		
		}
		else if ((mode == 'w' || mode == 'a') && search->second->cnt){
			search->second->cnt++;
			ReleaseSemaphore(mutex, 1, NULL);
			search->second->lock->acquireRWLockExclusive();
			WaitForSingleObject(mutex, INFINITE);
		
		}
		else {
			ReleaseSemaphore(mutex, 1, NULL);
			return nullptr;
		}
		
	}

	
	int *ret = fileLocation(fname);
	
	switch (mode) {
		case 'r': {
			if (ret != nullptr) {
				File* file = openR(fname,ret);
								
				return file;
			}
			else {
				ReleaseSemaphore(mutex, 1, NULL);
				std::cout << "FILE DOESN'T EXIST";
				return nullptr;
			}
		}
		case 'w': {
			if (ret != nullptr) {
				deleteFile(ret);
			}
			
			File *file = openW(fname);
			
			
			return file;
		}
		case 'a': {
			if (ret != nullptr) {
				File *file = openA(fname,ret);
				
				return file;
				
			}
			else {
				//greska
				ReleaseSemaphore(mutex, 1, NULL);
				return nullptr;
			}
		}
		default: {
			//bad mode
			ReleaseSemaphore(mutex, 1, NULL);
			return nullptr;
		}
	}

}


char KernelFS::deleteFile(char * fname)
{
	
	int *coord = fileLocation(fname);
	
	if (coord == nullptr) {
		return 0;
	}
	int ret = deleteFile(coord);
	
	return ret;
}

int KernelFS::readClusterFromPart(ClusterNo to_read, char* buffer)
{

	int ret = my_partition->readCluster(to_read, (char*)buffer);
	if (ret == 0) {
		
		return 0;
	}

	return 1;
	
}

int KernelFS::writeClusterToPart(ClusterNo to_write, char* cluster)
{
	
	int ret = my_partition->writeCluster(to_write, (char*)cluster);
	if (ret == 0) {
		
		return 0;
	}
	return 1;
}

int * KernelFS::fileLocation(char * fname)
{
	char **splitted = split(fname);
	char name[8] = { ' ' };
	char ext[3] = { ' ' };
	for (int i = 0; i < 8; i++) {
		name[i] = splitted[0][i];
	}
	for (int i = 0; i < 3; i++) {
		ext[i] = splitted[1][i];
	}
	
	
	int to_read = 0;
	int ret = 0;
	int *coord = new int[3];
	for (int j = 0; j < 3; j++) {
		coord[j] = 0;
	}
	FCB zero_fcb;
	unsigned int index_2[512] = { 0 };
	FCB file_table[ClusterSize / 32] = { zero_fcb };

	for (int i = 0; i < 512; i++) {
		
		to_read = root_index[i];
		if (to_read == 0) continue;
		
		//readClusterFromPart
		
		ret = readClusterFromPart(to_read, (char*)index_2);
		

		if (ret == 0) {
			continue;
		}
		for (int j = 0; j < 512; j++) {
				
			to_read = index_2[j];
			if (to_read == 0) continue;
			
			ret = readClusterFromPart(to_read, (char*)file_table);
			
			if (ret == 0) {
				continue;
			}
			for (int k = 0; k < ClusterSize / 32; k++) {
				if (!std::strncmp(name, file_table[k].name, 8) && !std::strncmp(ext, file_table[k].ext, 3)) {
					coord[0] = i;
					coord[1] = j;
					coord[2] = k;
					
					return coord;
				}
			}
				
		}
		
	}

	return nullptr;
}

int KernelFS::deleteFile(int *coord)
{
	
	int root_dir_coord = coord[0];
	int index_2_coord = coord[1];
	int file_table_coord = coord[2];

	//read level 2 index
	unsigned long buffer[512] = { 0 };
	WaitForSingleObject(mutex, INFINITE);
	int index_2_num = root_index[root_dir_coord];

	int ret = my_partition->readCluster(index_2_num, (char*)buffer);
	
	if (ret == 0) {
		ReleaseSemaphore(mutex, 1, NULL);
		return 0;
	}
	FCB zero_fcb;
	FCB file_table[ClusterSize / 32] = { zero_fcb };

	ret = my_partition->readCluster(buffer[index_2_coord], (char*)file_table);
	
	if (ret == 0) {
		ReleaseSemaphore(mutex, 1, NULL);
		return 0;
	}
	std::memcpy(&file_table[file_table_coord], &zero_fcb, sizeof(FCB));

	ret = my_partition->writeCluster(buffer[index_2_coord], (char*)file_table);
	if (ret == 0) {
		ReleaseSemaphore(mutex, 1, NULL);
		return 0;
	}
	ReleaseSemaphore(mutex, 1, NULL);
	return 1;
}
//ovde treba da se koristi readClusterFromPart
//moze3 doci do odredjenog ubrzanaj u odredjenim slucajevima
File * KernelFS::openR(char *fname, int *coord)
{
	//read index_2
	long root_index_entry = coord[0];
	long root_index_2_entry = coord[1];
	long file_table_entry = coord[2];
	
	unsigned long index_2[512] = { 0 };
	int ret = my_partition->readCluster(root_index[root_index_entry], (char*)index_2);
	
	if (ret == 0) {
		ReleaseSemaphore(mutex, 1, NULL);
		return nullptr;
	}
	//read file table
	FCB zero_fcb;
	FCB file_table[ClusterSize / 32] = { zero_fcb };
	ret = my_partition->readCluster(index_2[root_index_2_entry], (char*)file_table);

	if (ret == 0) {
		ReleaseSemaphore(mutex, 1, NULL);
		return nullptr;
	}
	FCB *new_file = new FCB;
	std::memcpy(new_file, &file_table[file_table_entry], sizeof(FCB));
	new_file->empty[0] = 'r';
	//we dont return anything on disc because we only read
	std::string key(fname);
	Lock zero_lock;
	if (KernelFS::open_files_map.find(key)==KernelFS::open_files_map.end()) {
		Lock *my_lock = new Lock;
		RWLockHandle *lock = new RWLockHandle();
		my_lock->cnt = 1;
		my_lock->lock = lock;
		my_lock->lock->acquireRWLockShared();
		KernelFS::open_files_map[key] = my_lock;
	}
	else {
		KernelFS::open_files_map[key]->lock->acquireRWLockShared();
	}
	
	std::string file_name(fname);

	KernelFile *file_ker = new KernelFile(new_file, this, file_name);
	File *file_ret = new File();
	file_ret->myImpl = file_ker;
	
	ReleaseSemaphore(mutex, 1, NULL);
	return file_ret;

}

File * KernelFS::openW(char *fname)
{
	
	if (root_index_entry == 513) {
		ReleaseSemaphore(mutex, 1, NULL);
		return nullptr;
	}
	//read index_2
	unsigned long index_2[512] = { 0 };
	int ret;
	ClusterNo index_2_cluster = root_index[root_index_entry];
	//checking cluster to see if it is allocated,
	//if it is not, allocate it 
	if (checkCluster(index_2_cluster)) {
	
		ret = my_partition->readCluster(root_index[root_index_entry], (char*)index_2);
		if (ret == 0) {
			ReleaseSemaphore(mutex, 1, NULL);
			return nullptr;
		}
	}
	else {
		index_2_cluster = allocateCluster();
		root_index[root_index_entry] = index_2_cluster;
		ret = my_partition->writeCluster(bit_vector_cluster_count + 1, (char*)root_index);
		if (ret == 0) {
			ReleaseSemaphore(mutex, 1, NULL);
			return nullptr;
		}
	
	}
	
	//read file table
	FCB zero_fcb;
	FCB file_table[ClusterSize / 32] = { zero_fcb };
	ClusterNo file_table_cluster = index_2[root_index_2_entry];

	
	if (checkCluster(file_table_cluster)) {
		ret = my_partition->readCluster(file_table_cluster, (char*)file_table);
		if (ret == 0) {
			ReleaseSemaphore(mutex, 1, NULL);
			return nullptr;
		}
	}
	else {
		file_table_cluster = allocateCluster();
		index_2[root_index_2_entry] = file_table_cluster;
		ret = my_partition->writeCluster(index_2_cluster, (char*)index_2);
		if (ret == 0) {
			ReleaseSemaphore(mutex, 1, NULL);
			return nullptr;
		}

	}

	//look for free space
	int i;
	for (i = 0; i < ClusterSize / 32; i++) {
		if (std::memcmp(&file_table[i], &zero_fcb, sizeof(FCB))) continue;
		break;
	}
	//no free space in file table
	while (i == ClusterSize / 32) {
		root_index_2_entry++;
		if (root_index_2_entry == 512) {
			root_index_2_entry = 0;
			root_index_entry++;
			ret = my_partition->writeCluster(root_index_entry - 1, (char*)index_2);
			if (ret == 0) {
				ReleaseSemaphore(mutex, 1, NULL);
				return nullptr;
			}
			if (root_index_entry == 513) {
				ReleaseSemaphore(mutex, 1, NULL);
				return nullptr;
			}
			ret = my_partition->readCluster(root_index_entry, (char*)index_2);

		}
		ret = my_partition->writeCluster(index_2[root_index_2_entry - 1], (char*)file_table);
		if (ret == 0) {
			ReleaseSemaphore(mutex, 1, NULL);
			return nullptr;
		}
		ret = my_partition->readCluster(index_2[root_index_2_entry], (char*)file_table);
		if (ret == 0) {
			ReleaseSemaphore(mutex, 1, NULL);
			return nullptr;
		}
		//check in new filetable if there is empty space, if there is not then repeat cycle untill
		//you find free space or return nullptr in case of no free space
		for (i = 0; i < ClusterSize / 32; i++) {
			if (std::memcmp(&file_table[i], &zero_fcb, sizeof(FCB))) continue;
			break;
		}
	}

	//save fcb for opened file in table and write table to disk
	char **splitted = split(fname);
	FCB *new_file = new FCB;
	
	std::memcpy(new_file->name, splitted[0], 8);
	std::memcpy(new_file->ext, splitted[1],3);
	
	ClusterNo data_cluster = allocateCluster();
	if (data_cluster == -1) {
		ReleaseSemaphore(mutex, 1, NULL);
		return nullptr;
	}
	new_file->index0 = data_cluster;
	
	//std::memcpy ovde mora
	file_table[i] = *new_file;
	new_file->empty[0] = 'w';

	ret = my_partition->writeCluster(index_2[root_index_2_entry], (char*)file_table);
	if (ret == 0) {
		ReleaseSemaphore(mutex, 1, NULL);
		return nullptr;
	}	
	std::string key(fname);

	Lock zero_lock;
	
	if (KernelFS::open_files_map.find(key) == KernelFS::open_files_map.end()) {
		Lock *my_lock = new Lock;
		my_lock->lock = new RWLockHandle();
		my_lock->lock->acquireRWLockExclusive();
		my_lock->cnt = 1;
		KernelFS::open_files_map[key] = my_lock;
		
		
	}
	else {
		KernelFS::open_files_map[key]->lock->acquireRWLockExclusive();
		KernelFS::open_files_map[key]->cnt++;
		
	}
	
	std::string file_name(fname);

	KernelFile *file_ker = new KernelFile(new_file,this, file_name);
	File *file_ret = new File();
	file_ret->myImpl = file_ker;
	
	ReleaseSemaphore(mutex, 1, NULL);
	return file_ret;
}

File * KernelFS::openA(char *fname, int *coord)
{
	//read index_2
	long root_index_entry = coord[0];
	long root_index_2_entry = coord[1];
	long file_table_entry = coord[2];
	
	unsigned long index_2[512] = { 0 };
	int ret = my_partition->readCluster(root_index[root_index_entry], (char*)index_2);
	if (ret == 0) {
		ReleaseSemaphore(mutex, 1, NULL);
		return nullptr;
	}
	//read file table
	FCB zero_fcb;
	FCB file_table[ClusterSize / 32] = { zero_fcb };
	ret = my_partition->readCluster(index_2[root_index_2_entry], (char*)file_table);

	if (ret == 0) {
		ReleaseSemaphore(mutex, 1, NULL);;
		return nullptr;
	}

	FCB *new_file = new FCB;
	std::memcpy(new_file, &file_table[file_table_entry], sizeof(FCB));

	new_file->empty[0] = 'a';
	//we dont return anything on disc because we only read 
	std::string key(fname);

	Lock zero_lock;
	if (KernelFS::open_files_map.find(key) == KernelFS::open_files_map.end()) {
		Lock *my_lock = new Lock;
		RWLockHandle *lock = new RWLockHandle();
		my_lock->lock = lock;
		my_lock->cnt = 1;
		
		
		KernelFS::open_files_map[key] = my_lock;
		my_lock->lock->acquireRWLockExclusive();
	} 
	std::string file_name(fname);

	KernelFile *file_ker = new KernelFile(new_file, this, file_name);
	file_ker->cursor = new_file->fileSize;
	file_ker->allocated_size = (new_file->fileSize / ClusterSize + (new_file->fileSize % ClusterSize != 0 ? 1 : 0))*ClusterSize;
	file_ker->index_2_entry = (file_ker->cursor / ClusterSize) % 512;
	file_ker->index_1_entry = (file_ker->cursor / ClusterSize ) / 512;
	File *file_ret = new File();
	file_ret->myImpl = file_ker;
	
	ReleaseSemaphore(mutex, 1, NULL);;
	return file_ret;

}
ClusterNo KernelFS::allocateCluster()
{
	//no free clusters
	WaitForSingleObject(alloc_mutex, INFINITE);
	if (first_free == -1) {
		ReleaseSemaphore(alloc_mutex, 1, NULL);
		return -1;
	}
	//allocate cluster
	unsigned long buffer[512] = { 0 };
	ClusterNo ret = my_partition->writeCluster(first_free, (char*)buffer);
	if (ret == 0) {
		ReleaseSemaphore(alloc_mutex, 1, NULL);
		return -1;
	}
	int pos_1 = first_free / 8;
	int pos_2 = first_free % 8;

	bit_vector[pos_1] = bit_vector[pos_1] | (256>>pos_2);
	ret = first_free++;
	

	//update first_free 
	while (checkCluster(first_free)) {
		first_free++;
		if (first_free == bit_vector_size) {
			first_free = bit_vector_cluster_count + 2;
		}
		if (first_free == ret) {
			first_free = -1;
		}
	}
	ReleaseSemaphore(alloc_mutex, 1, NULL);
	return ret;
}

void KernelFS::deallocateCluster(ClusterNo to_deallocate)
{
	WaitForSingleObject(alloc_mutex, INFINITE);
	int pos_1 = to_deallocate / 8;
	int pos_2 = to_deallocate % 8;

	bit_vector[pos_1] = bit_vector[pos_1] & (~ (0x100>>pos_2));
	if (to_deallocate < first_free) {
		first_free = to_deallocate;
	}
	ReleaseSemaphore(alloc_mutex, 1, NULL);

}

int KernelFS::checkCluster(ClusterNo entry)
{
	//called from safe env, no need to sync
	if (entry == 0) return 0;

	int pos_1 = entry / 8;
	int pos_2 = entry % 8;
	int to_shift = bit_vector[pos_1];
	int ret = (to_shift >> (8 - pos_2)) & 1;

	return ret;
	
}



char ** KernelFS::split(char *fname)
{
	int i = 0;
	int length = std::strlen(fname);
	//finding dot
	while (1) {
		i++;
		if (fname[i] == '.') {
			break;
		}
		//we dont have a dot in fname -- invalid name
		if (i >= length) return nullptr;
		if (i == 0)return nullptr;

	}
	char *name = new char[8];
	char *ext = new char[3];
	std::memcpy(name, fname+1, i);
	for (int j = i; j < 8; j++) {
		name[j] = ' ';
	}
	std::memcpy(ext, fname + i + 1, 3);

	char ** ret = new char*[2];
	ret[0] = name;
	ret[1] = ext;
	
	return ret;
}

int KernelFS::update_fcb(char * file_name, FCB* fcb)
{
	int* ret = fileLocation(file_name);
	unsigned long index2[512] = { 0 };
	int c = readClusterFromPart(root_index[ret[0]], (char*)index2);
	if (c == 0) {
		return 0;
	}
	FCB zero_fcb;
	FCB fcb_cluster[ClusterSize / 32] = { zero_fcb };
	c = my_partition->readCluster(index2[ret[1]], (char*)fcb_cluster);
	if (c == 0) {
		return 0;
	}
	
	fcb_cluster[ret[2]].fileSize = fcb->fileSize;
	fcb_cluster[ret[2]].empty[0] = '\0';
	
	c = my_partition->writeCluster(index2[ret[1]], (char*)fcb_cluster);
	if (c == 0) {
		return 0;
	}
	return 1;
}


