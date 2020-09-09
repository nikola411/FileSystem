#include "stdafx.h"
#include "KernelFile.h"
#include <iostream>
#include "KernelFS.h"
#include "RWLockHandle.h"
#include <thread>

const int FILE_SIZE_OVERFLOW = 0;
const int CLUSTER_READING_ERR = 0;
const int ALLOCATION_ERR = 0;
const int NO_OPEN_FILE_ERR = 0;
const int CLUSTER_WRITE_ERR = 0;

KernelFS * KernelFile::my_fs = nullptr;

KernelFile::KernelFile(FCB *my_fcb, KernelFS *myFS, std::string fname) {
	if (my_fcb != nullptr) {
		this->my_fcb = my_fcb;
		cursor = 0;
		parent_thread_id = std::this_thread::get_id();
		last_cluster[512] = { 0 };
		index_1[512] = { 0 };
		
		last_cluster_no = 0;
		index_1_entry = 0;
		index_2_entry = 0;
		

		end_of_file = my_fcb->fileSize;
		allocated_size = (end_of_file / ClusterSize + (end_of_file % ClusterSize != 0 ? 1 : 0))*ClusterSize;
		index_1_no = my_fcb->index0;
		myFS->readClusterFromPart(index_1_no, (char*)index_1);

		file_name = fname;
		current_size = 0;

		read_cnt = 0;
		write_cnt = 0;
		KernelFile::my_fs = myFS;

	}

	//int ret = KernelFile::my_fs->readClusterFromPart()
}

int KernelFile::allocate_clusters(ClusterNo clusters_to_allocate)
{
	
	long total_clusters = clusters_to_allocate;
	//number of index2 clusters to allocate
	long index_2_clusters = clusters_to_allocate;
	//number of index1 clusters to allocate, that can hold index_2_clusters entries
	long index_1_clusters = index_2_clusters / (ClusterSize / 4) + (index_2_clusters % (ClusterSize / 4) != 0 ? 1 : 0);
	//not enough space
	if (index_1_clusters + index_1_entry > ClusterSize / 4) {
		return FILE_SIZE_OVERFLOW;
	}

	int changed = 0;
	//if current entry is not allocated, allocate it
	if (index_1[index_1_entry] == 0) {
		//allocate idnex_2
		index_1[index_1_entry] = KernelFile::my_fs->allocateCluster();
		index_2_entry = 0;
		changed = 1;
	}

	//allocate index_1 entries ; index_2 clusters
	if (index_1_clusters) {
		for (int i = 1; i < index_1_clusters; i++) {
			index_1[index_1_entry + i] = KernelFile::my_fs->allocateCluster();
			changed = 2;
		}
	}

	long cnt = index_2_entry;
	int index_1_entry_counter = 0;
	unsigned long index_2[512] = { 0 };
	int i = 0;
	//data clusters to allocate
	while (index_2_clusters) {
		int ret = KernelFile::my_fs->readClusterFromPart(index_1[index_1_entry + i], (char*)index_2);

		if (ret == 0) {
			return CLUSTER_READING_ERR;
		}
		long index_2_to_allocate = index_2_clusters + index_2_entry > ClusterSize / 4 ? ClusterSize / 4 : index_2_clusters + index_2_entry;
		for (long j = index_2_entry; j < index_2_to_allocate; j++) {
			index_2[j] = KernelFile::my_fs->allocateCluster();
			
			index_2_clusters--;
		}

		ret = KernelFile::my_fs->writeClusterToPart(index_1[index_1_entry + i], (char*)index_2);
		if (ret == 0) {
			return CLUSTER_WRITE_ERR;
		}


		if (index_2_clusters) {
			index_2_entry = 0;
			i++;

		}
	}

	index_2_entry = cnt;
	//end_of_file += clusters_to_allocate * ClusterSize;
	allocated_size += clusters_to_allocate * ClusterSize;

	return 1;
	
	
}
//writing when eof!=cursor
int KernelFile::write_in_file1(BytesCnt size, char *buffer)
{
	unsigned long index_2[512] = { 0 };
	if (!std::memcmp(index_1, index_2, 512 * sizeof(unsigned long))) {
		KernelFile::my_fs->readClusterFromPart(index_1_no, (char*)index_1);
	}
	BytesCnt temp = size;
	char *to_write = buffer;
	unsigned long data_cluster_entry = cursor % ClusterSize;
	int ret = 1;
	long j = 0;

	char data[ClusterSize] = { 0 };

	ret = KernelFile::my_fs->readClusterFromPart(index_1[index_1_entry], (char*)index_2);
	if (ret == 0) {

		std::cout << "ERROR READING INDEX2 CLUSTER  NO: " << index_1[index_1_entry] << std::endl;
		return CLUSTER_READING_ERR;
	}

	while (temp) {


		ret = KernelFile::my_fs->readClusterFromPart(index_2[index_2_entry], data);
		if (ret == 0) {

			std::cout << "ERROR READING DATA CLUSTER NO: " << index_2[index_2_entry] << std::endl;
			std::cout << "BYTES LEFT TO WRITE : " << temp << std::endl;

			return CLUSTER_READING_ERR;
		}
		//to_write_in_cluster_cnt shows higher bound of our for loop,

		long to_write_in_cluster_cnt = temp + data_cluster_entry > ClusterSize ? ClusterSize : temp + data_cluster_entry;

		for (long i = data_cluster_entry; i < to_write_in_cluster_cnt; i++) {
			data[i] = to_write[j++];
		}
		KernelFile::my_fs->writeClusterToPart(index_2[index_2_entry], data);
		temp -= (to_write_in_cluster_cnt - data_cluster_entry);
		//ovaj uslov ne valja!!
		if (to_write_in_cluster_cnt == ClusterSize) {
			data_cluster_entry = 0;
			index_2_entry++;
			if (index_2_entry == 512) {
				index_2_entry = 0;
				index_1_entry++;
				int ret = KernelFile::my_fs->readClusterFromPart(index_1[index_1_entry], (char*)index_2);
				if (ret == 0) {
					return 0;
				}
				
			}

		}
		

	}
	cursor += size;
	end_of_file = end_of_file > cursor ? end_of_file : cursor;
	return 1;
	



	
}

char * KernelFile::file_name_converting()
{
	char *file_name = new char[12];

	int i, j;
	
	for (i = 0; i < 8; i++) {
		if (! (my_fcb->name[i] != ' ' && my_fcb->name[i] != '\0')) {
			break;

		}
		
	}
	std::memcpy(file_name, my_fcb->name, i);
	file_name[i++] = '.';
	std::memcpy(file_name + i, my_fcb->ext, 3);
	
	int name_length = std::strlen(my_fcb->name);
	int ext_length = std::strlen(my_fcb->ext);
	int length =  name_length + ext_length  + 1;
	file_name[length + 1] = '\0';
	
	return file_name;
}

KernelFile::~KernelFile()
{
	this->close();
}

char KernelFile::close()
{
	WaitForSingleObject(KernelFile::my_fs->mutex, INFINITE);
	auto search = KernelFS::open_files_map.find(file_name);

	if (search == KernelFS::open_files_map.end()) {
		ReleaseSemaphore(KernelFile::my_fs->mutex, 1, NULL);
		return 0;
	}
	
	int name_length = file_name.length();
	char* name = new char[name_length + 1];

	std::strcpy(name, file_name.c_str());
	
	my_fcb->fileSize = this->end_of_file;

	int ret = KernelFile::my_fs->update_fcb(name, my_fcb);
	ret = KernelFile::my_fs->writeClusterToPart(index_1_no, (char*)index_1);
	search->second->cnt--;

	if (search->second->cnt) {
		if (my_fcb->empty[0] == 'r') {
			search->second->lock->releaseRWLockShared();
		}
		else {
			search->second->lock->releaseRWLockExclusive();
		}
	}
	else {
		KernelFS::open_files_map.erase(file_name);
	}

	delete name;
	
	ReleaseSemaphore(KernelFile::my_fs->mutex, 1, NULL);
	
	if (ret == 0) {
		return 0;
	}
	else {
		return 1;
	}	
}

char KernelFile::write(BytesCnt to_write, char * buffer)
{
	//syncrhonize
	//we cant allow 

	if (parent_thread_id != std::this_thread::get_id()) {
		//signal sem
		return 0;
	}

	WaitForSingleObject(KernelFile::my_fs->mutex, INFINITE);
	auto search = KernelFile::my_fs->open_files_map.find(this->file_name);
	

	if (search != KernelFile::my_fs->open_files_map.end()) {
		ReleaseSemaphore(KernelFile::my_fs->mutex, 1, NULL);
		//file is open and i cannot write
		if (my_fcb->empty[0] == 'r') {
			return 0;
		}
		else {
			//file is opened for writing and we can write in it
		
			if (cursor + to_write > allocated_size) {
				
				long to_allocate = cursor + to_write - allocated_size;//in bytes
				long clusters_to_allocate = to_allocate / ClusterSize + (to_allocate%ClusterSize != 0 ? 1 : 0);
				int ret = allocate_clusters(clusters_to_allocate);
				if (ret == 0) {
					
					return ALLOCATION_ERR;
				}
				
			}
			
			int ret = write_in_file1(to_write, buffer);
			
			//finish sync??

			return ret;
		}
	} else {
		ReleaseSemaphore(KernelFile::my_fs->mutex, 1, NULL);
		return NO_OPEN_FILE_ERR;
	}

	
}

BytesCnt KernelFile::read(BytesCnt to_read, char* buffer)
{

	WaitForSingleObject(KernelFile::my_fs->mutex, INFINITE);
	if (KernelFile::my_fs->open_files_map.find(this->file_name) == KernelFile::my_fs->open_files_map.end()) {
		ReleaseSemaphore(KernelFile::my_fs->mutex, 1, NULL);
		return 0;

	}
	ReleaseSemaphore(KernelFile::my_fs->mutex, 1, NULL);
	if (cursor == end_of_file) {
		return 0;
	}

	long to_read_in_file = (end_of_file - cursor > to_read ? to_read : end_of_file - cursor);
	long cursor_cluster = cursor / ClusterSize;
	//long index_2_entr_no = cursor_cluster / (ClusterSize / 4) + (cursor_cluster % (ClusterSize / 4) != 0 ? 1 : 0);
	long index_2_entr_no = cursor / ClusterSize ;
	long entry_2 = index_2_entr_no % 512;
	long entry_1 = index_2_entr_no / 512;
	

	long cursor_in_cluster = cursor % ClusterSize;

	
	long i = 0;
	unsigned long index_2[512] = { 0 };
	if (!std::memcmp(index_1, index_2, 512 * sizeof(unsigned long))) {
		KernelFile::my_fs->readClusterFromPart(index_1_no, (char*)index_1);
	}
	char temp_buffer[ClusterSize] = { 0 };
	int ret = 1;
	int k = 0;

	while (i < to_read_in_file) {
		//ovde treba jedna velika optimizacija
		//da se ovo izbaci van while-a i da se updajetuje index2 samo kad treba ne svaki put

		ret = KernelFile::my_fs->readClusterFromPart(index_1[entry_1], (char*)index_2);
		if (ret == 0) {
			
			return CLUSTER_READING_ERR;
		}
		ret = KernelFile::my_fs->readClusterFromPart(index_2[entry_2], temp_buffer);
		if (ret == 0) {
			
			return CLUSTER_READING_ERR;
		}
		
		long to_read_in_cluster = (ClusterSize - cursor_in_cluster > to_read_in_file - i ? to_read_in_file - i : ClusterSize - cursor_in_cluster);
		
		for (long j = cursor_in_cluster; j < cursor_in_cluster + to_read_in_cluster; j++) {
			buffer[i++] = temp_buffer[j];
			if (i == 78930) {
				i = 78930;
			}

			//std::cout << buffer[i];
		}
		
		
		
		if (i < to_read_in_file) {
			cursor_in_cluster = 0;
			entry_2++;
			if (entry_2 == 512) {
				entry_1++;
				entry_2 = 0;
			}
		}
	}
	cursor += to_read_in_file;
	

	return to_read_in_file;
 }

char KernelFile::seek(BytesCnt new_cursor)
{
	WaitForSingleObject(mutex, INFINITE);
	if (new_cursor > end_of_file) {
		ReleaseMutex(mutex);
		return 0;
	} 
	cursor = new_cursor;
	index_1_entry = (cursor / ClusterSize) / 512;
	index_2_entry = (cursor / ClusterSize) % 512;
	ReleaseMutex(mutex);
	return 1;
}

BytesCnt KernelFile::filePos()
{

	return cursor;
}

char KernelFile::eof()
{
	if (cursor > end_of_file) {
		return 1;
	}
	if (cursor == end_of_file) {
		return 2;
	}
	else {
		return 0;
	}
	
}

BytesCnt KernelFile::getFileSize()
{
	return end_of_file;
}

char KernelFile::truncate()
{
	WaitForSingleObject(mutex, INFINITE);
	if (end_of_file == 0) {
		ReleaseMutex(mutex);
		return 0;
	}
	long to_free = end_of_file - cursor;

	if (to_free == 0) {
		ReleaseMutex(mutex);
		return 0;
	}

	ClusterNo clusters_to_free = to_free / ClusterSize + (to_free % ClusterSize != 0 ? 1 : 0);
	
	ClusterNo cursor_cluster = cursor / ClusterSize;
	ClusterNo entry_2 = cursor_cluster / 512 + (cursor_cluster % 512 != 0 ? 1 : 0);
	ClusterNo entry_1 = entry_2 / 512 + (entry_2 % 512 != 0 ? 1 : 0);

	unsigned long i = 0;
	unsigned long index_2[512] = { 0 };
	int ret = 1;
	while (i < clusters_to_free) {
		ret = KernelFile::my_fs->readClusterFromPart(index_1[entry_1], (char*)index_2);
		if (ret == 0) {
			ReleaseMutex(mutex);
			return CLUSTER_READING_ERR;
		}
		long to_free_in_cluster = (clusters_to_free > 512 - entry_2 ? 512 - entry_2 : clusters_to_free);
		for (long j = entry_2; j < to_free_in_cluster; j++) {
			KernelFile::my_fs->deallocateCluster(index_2[j]);
			index_2[j] = 0;
			i++;
		}
		ret = KernelFile::my_fs->writeClusterToPart(index_1[entry_1], (char*)index_2);

		if (i < clusters_to_free) {
			entry_2 = 0;
			entry_1++;
		}
	}
	ReleaseMutex(mutex);
	return 1;
}
