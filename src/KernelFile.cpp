#include "stdafx.h"
#include "KernelFile.h"
#include <iostream>
#include "KernelFS.h"
#include <thread>

const int FILE_SIZE_OVERFLOW = 0;
const int CLUSTER_READING_ERR = 0;
const int ALLOCATION_ERR = 0;
const int NO_OPEN_FILE_ERR = 0;
const int CLUSTER_WRITE_ERR = 0;

KernelFS * KernelFile::my_fs = nullptr;

KernelFile::KernelFile(FCB *my_fcb, KernelFS *myFS, std::string fname) {
	this->my_fcb = my_fcb;
	cursor = 0;
	parent_thread_id = std::this_thread::get_id();
	last_cluster[512] = { 0 };
	last_cluster_no = 0;
	index_1_entry = 0;
	index_2_entry = 0;
	allocated_size = 0;

	end_of_file = my_fcb->fileSize;
	index_1_no = my_fcb->index0;
	file_name = fname;
	current_size = 0;


	
	
	read_cnt = 0;
	write_cnt = 0;
	KernelFile::my_fs = myFS;

	//int ret = KernelFile::my_fs->readClusterFromPart()
}

int KernelFile::allocate_clusters(long clusters_to_allocate)
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

	//if current entry is not allocated, allocate it
	if (index_1[index_1_entry] == 0) {
		//allocate idnex_2
		index_1[index_1_entry] = KernelFile::my_fs->allocateCluster();
		index_2_entry = 0;
	}

	//allocate index_1 entries ; index_2 clusters
	if (index_1_clusters) {
		for (int i = 1; i < index_1_clusters; i++) {
			index_1[index_1_entry + i] = KernelFile::my_fs->allocateCluster();
		}
	}

	long cnt = index_2_entry;
	int index_1_entry_counter = 0;
	unsigned long index_2[512] = { 0 };
	int i = 0;

	while (index_2_clusters) {
		int ret = KernelFile::my_fs->readClusterFromPart(index_1[index_1_entry + i], index_2);

		if (ret == 0) { 
			return CLUSTER_READING_ERR;
		}
		long index_2_to_allocate = index_2_clusters > ClusterSize / 4 ? ClusterSize / 4 : index_2_clusters;
		for (long j = index_2_entry; j < index_2_to_allocate ; j++) {
			index_2[j] = KernelFile::my_fs->allocateCluster();
			index_2_clusters--;
		}

		ret = KernelFile::my_fs->writeClusterToPart(index_1[index_1_entry + i], index_2);
		if (ret == 0) {
			return CLUSTER_WRITE_ERR;
		}
		

		if (index_2_clusters) {
			index_2_entry = 0;
			i++;
		}
	}

	index_2_entry = cnt;
	end_of_file += clusters_to_allocate * ClusterSize;

	return 1;
}

int KernelFile::write_in_file(BytesCnt size, char *buffer)
{
	WaitForSingleObject(mutex, INFINITE);
	
	BytesCnt temp = size;
	unsigned long data[512] = { 0 };
	unsigned long *to_write = (unsigned long*)buffer;
	unsigned long data_cluster_entry = cursor%ClusterSize;
	int ret = 1;
	long j = 0;
	while (temp) {
		

		unsigned long index_2[512] = { 0 };
		unsigned long data[512] = { 0 };

		ret = KernelFile::my_fs->readClusterFromPart(index_1[index_1_entry], index_2);
		if (ret == 0) {
			ReleaseMutex(mutex);
			return CLUSTER_READING_ERR;
		}

		ret = KernelFile::my_fs->readClusterFromPart(index_2[index_2_entry], data);
		if (ret == 0) {
			ReleaseMutex(mutex);
			return CLUSTER_READING_ERR;
		}

		long to_write_in_cluster_cnt = size / 4 > (ClusterSize / 4 - index_2_entry) ? ClusterSize / 4 : size / 4;

		for (long i = data_cluster_entry; i < to_write_in_cluster_cnt; i++) {
			data[i] = to_write[j++];
		}
		KernelFile::my_fs->writeClusterToPart(index_2[index_2_entry], data);
		temp -= to_write_in_cluster_cnt * 4;
		if (to_write_in_cluster_cnt + data_cluster_entry >= 512) {
			data_cluster_entry = 0;
			index_2_entry++;
		}
		if (temp) {
			if (index_2_entry == 512) {
				index_1_entry++;
				index_2_entry = 0;
			}
			
		}
	}
	cursor += size;
	ReleaseMutex(mutex);

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
	KernelFS::open_files_map.erase(search);
	ReleaseSemaphore(KernelFile::my_fs->mutex, 1, NULL);
	
	return 1;
}

char KernelFile::write(BytesCnt to_write, char * buffer)
{
	//syncrhonize
	//we cant allow 

	if (parent_thread_id != std::this_thread::get_id()) {
		//signal sem
		return 0;
	}

	
	auto search = KernelFile::my_fs->open_files_map.find(this->file_name);
	if (search != KernelFile::my_fs->open_files_map.end()) {
		//file is open and i cannot write
		if (my_fcb->empty[0] != 'w') {
			return 0;
		}
		else {
			//file is opened for writing and we can write in it
			if (cursor + to_write > allocated_size) {
				WaitForSingleObject(mutex, INFINITE);
				long to_allocate = cursor + to_write - allocated_size;//in bytes
				long clusters_to_allocate = to_allocate / ClusterSize + (to_allocate%ClusterSize != 0 ? 1 : 0);
				int ret = allocate_clusters(clusters_to_allocate);
				if (ret == 0) {
					ReleaseMutex(mutex);
					return ALLOCATION_ERR;
				}
				ReleaseMutex(mutex);
				
			}
			
			int ret = write_in_file(to_write, buffer);
			
			//finish sync??

			return ret;
		}
	} else {
		return NO_OPEN_FILE_ERR;
	}

	
}

BytesCnt KernelFile::read(BytesCnt to_read, char * buffer)
{
	//sync
	//mutex.wait();
	//if(writing) sem_read.wait();
	//reading++;

	WaitForSingleObject(KernelFile::my_fs->mutex, INFINITE);
	if (KernelFile::my_fs->open_files_map.find(this->file_name) == KernelFile::my_fs->open_files_map.end()) {
		ReleaseMutex(KernelFile::my_fs->mutex);
		return 0;
	}
	ReleaseMutex(KernelFile::my_fs->mutex);
	if (cursor == end_of_file) {
		return 0;
	}
	WaitForSingleObject(mutex, INFINITE);
	//find index_1 entry index_2 entry and position in cluster of cursor
	long to_read_in_file = (end_of_file - cursor > to_read ? to_read : end_of_file - cursor);
	long cursor_cluster = cursor / ClusterSize + (cursor% ClusterSize != 0 ? 1 : 0);//current cursor cluster of all clusters allocated for this file
	long entry_2 = cursor_cluster / (ClusterSize / 4) + (cursor_cluster % (ClusterSize / 4) != 0 ? 1 : 0);//index_2 entry for cursor
	long entry_1 = entry_2 / (ClusterSize / 4) + (entry_2 % (ClusterSize / 4) != 0 ? 1 : 0);
	long cursor_in_cluster = cursor % ClusterSize;

	long i = 0;
	unsigned long index_2[512] = { 0 };
	char temp_buffer[ClusterSize] = { 0 };
	int ret = 1;
	while (i < to_read_in_file) {
		
		ret = KernelFile::my_fs->readClusterFromPart(index_1[entry_1], index_2);
		if (ret == 0) {
			ReleaseMutex(mutex);
			return CLUSTER_READING_ERR;
		}
		ret = KernelFile::my_fs->readClusterFromPart(index_2[entry_2], (unsigned long*)temp_buffer);
		if (ret == 0) {
			ReleaseMutex(mutex);
			return CLUSTER_READING_ERR;
		}
		long to_read_in_cluster = (ClusterSize - cursor_in_cluster > to_read_in_file ? to_read_in_file : ClusterSize - cursor_in_cluster);
		for (long j = cursor_in_cluster; j < to_read_in_cluster; j++) {
			buffer[i++] = temp_buffer[j];
		}
		if (i < to_read_in_file) {
			cursor_in_cluster = 0;
			entry_2++;
			if (entry_2 > 512) {
				entry_1++;
				entry_2 = 0;
			}
		}
	}
	ReleaseMutex(mutex);

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
		ret = KernelFile::my_fs->readClusterFromPart(index_1[entry_1], index_2);
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
		ret = KernelFile::my_fs->writeClusterToPart(index_1[entry_1], index_2);

		if (i < clusters_to_free) {
			entry_2 = 0;
			entry_1++;
		}
	}
	ReleaseMutex(mutex);
	return 1;
}
