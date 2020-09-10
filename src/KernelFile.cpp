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



KernelFile::KernelFile(FCB *my_fcb, KernelFS *myFS, std::string fname) {
	if (my_fcb != nullptr) {
		this->my_fcb = my_fcb;
		cursor = 0;
		parent_thread_id = std::this_thread::get_id();
		last_cluster[2048] = { 0 };
		index_1[512] = { 0 };
		last_index2[512] = { 0 };
		last_index2_no = 0;
		
		last_cluster_no = 0;
		dirty = false;
		index_1_entry = 0;
		index_2_entry = 0;
		dirty_index2 = false;
		

		end_of_file = my_fcb->fileSize;
		allocated_size = (end_of_file / ClusterSize + (end_of_file % ClusterSize != 0 ? 1 : 0))*ClusterSize;
		index_1_no = my_fcb->index0;
		myFS->readClusterFromPart(index_1_no, (char*)index_1);

		file_name = fname;
		current_size = 0;

		read_cnt = 0;
		write_cnt = 0;
		my_fs = myFS;

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
		index_1[index_1_entry] = my_fs->allocateCluster();
		index_2_entry = 0;
		changed = 1;
	}

	//allocate index_1 entries ; index_2 clusters
	if (index_1_clusters) {
		for (int i = 1; i < index_1_clusters; i++) {
			index_1[index_1_entry + i] = my_fs->allocateCluster();
			changed = 2;
		}
	}

	long cnt = index_2_entry;
	int index_1_entry_counter = 0;
	unsigned long index_2[512] = { 0 };
	int i = 0;

	int first = 1;
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
	dirty_index2 = true;
	allocated_size += clusters_to_allocate * ClusterSize;

	return 1;
		
}

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
	//check if index2 is valid
	if (last_index2_no == 0 || dirty_index2) {
		ret = my_fs->readClusterFromPart(index_1[index_1_entry], (char*)index_2);
		if (ret == 0) {

			std::cout << "ERROR READING INDEX2 CLUSTER  NO: " << index_1[index_1_entry] << std::endl;
			return CLUSTER_READING_ERR;
		}
		last_index2_no = index_1[index_1_entry];
		std::memcpy(last_index2, index_2, 512 * sizeof(unsigned long));
		dirty_index2 = false;

	}
	else if (last_index2_no != index_1[index_1_entry] ) {
		ret = my_fs->readClusterFromPart(index_1[index_1_entry], (char*)last_index2);
		if (ret == 0) {

			std::cout << "ERROR READING INDEX2 CLUSTER  NO: " << index_1[index_1_entry] << std::endl;
			return CLUSTER_READING_ERR;
		}
		last_index2_no = index_1[index_1_entry];
		std::memcpy(index_2, last_index2, 512 * sizeof(unsigned long));
	}
	else {
		std::memcpy((char*)index_2, (char*)last_index2, ClusterSize * sizeof(char));
	}

	while (temp) {

 		ret = readCluster(index_2[index_2_entry], data);
		if (ret == 0) {

			std::cout << "ERROR READING DATA CLUSTER NO: " << index_2[index_2_entry] << std::endl;
			std::cout << "BYTES LEFT TO WRITE : " << temp << std::endl;

			return CLUSTER_READING_ERR;
		}
		
		long to_write_in_cluster_cnt = temp + data_cluster_entry > ClusterSize ? ClusterSize : temp + data_cluster_entry;

		for (long i = data_cluster_entry; i < to_write_in_cluster_cnt; i++) {
			data[i] = to_write[j++];
		}
		ret = writeCluster(index_2[index_2_entry], data);
		if (ret == 0) {
			return CLUSTER_WRITE_ERR;
		}
		temp -= (to_write_in_cluster_cnt - data_cluster_entry);
		
		if (to_write_in_cluster_cnt == ClusterSize ) {
			data_cluster_entry = 0;
			index_2_entry++;
			if (index_2_entry == 512) {
				index_2_entry = 0;
				index_1_entry++;
				if (index_1[index_1_entry]) {
					int ret = my_fs->readClusterFromPart(index_1[index_1_entry], (char*)index_2);
					if (ret == 0) {
						return 0;
					}
					last_index2_no = index_1[index_1_entry];
					std::memcpy(last_index2, index_2, 512 * sizeof(unsigned long));

				}
				else {
					dirty_index2 = true;
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

int KernelFile::readCluster(ClusterNo to_read, char *buffer)
{
	int ret = 1;
	if (last_cluster_no == 0) {
		ret = my_fs->readClusterFromPart(to_read, last_cluster);
		if (ret == 0) {
			return 0;
		}
		last_cluster_no = to_read;
		std::memcpy(buffer, last_cluster, ClusterSize * sizeof(char));
		return 1;
	}else
	if (last_cluster_no != to_read ) {
		if (dirty) {
			ret = my_fs->writeClusterToPart(last_cluster_no, last_cluster);
			if (ret == 0) {
				return 0;
			}
			dirty = false;
		}
		
		ret = my_fs->readClusterFromPart(to_read, last_cluster);
		if (ret == 0) {
			return 0;
		}
		last_cluster_no = to_read;
		std::memcpy(buffer, last_cluster, ClusterSize * sizeof(char));

		return 1;
	}else
	if (last_cluster_no == to_read) {
		std::memcpy(buffer, last_cluster, ClusterSize * sizeof(char));
		return 1;
	} 
	else {
		return 0;
	}
	
}

int KernelFile::writeCluster(ClusterNo to_write, char *buffer)
{
	int ret;
	if (last_cluster_no == to_write) {
		std::memcpy(last_cluster, buffer, ClusterSize * sizeof(char));
		dirty = true;
		return 1;
	}
	else {
		if (dirty) {
			ret = my_fs->writeClusterToPart(last_cluster_no, last_cluster);
			if (ret == 0) {
				return 0;
			}
			
		}
		std::memcpy(last_cluster, buffer, ClusterSize * sizeof(char));
		last_cluster_no = to_write;
		dirty = true;
		return 1;
	}
	
}

KernelFile::~KernelFile()
{
	this->close();
}

char KernelFile::close()
{
	WaitForSingleObject(my_fs->mutex, INFINITE);
	auto search = KernelFS::open_files_map.find(file_name);

	if (search == KernelFS::open_files_map.end()) {
		ReleaseSemaphore(my_fs->mutex, 1, NULL);
		return 0;
	}
	
	int name_length = file_name.length();
	char* name = new char[name_length + 1];

	std::strcpy(name, file_name.c_str());
	my_fcb->fileSize = this->end_of_file;

	int ret = my_fs->update_fcb(name, my_fcb);
	ret = my_fs->writeClusterToPart(index_1_no, (char*)index_1);
	if (dirty) {
		ret = my_fs->writeClusterToPart(last_cluster_no, (char*)last_cluster);
	}
	if (dirty_index2) {
		ret = my_fs->writeClusterToPart(last_index2_no, (char*)last_index2);
	}
	
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
		if (KernelFS::open_files_map.size() == 0) {
			if (my_fs->formatting) {
				ReleaseSemaphore(my_fs->format_sem, 1, NULL);
			}
			if (my_fs->unmounting) {
				ReleaseSemaphore(my_fs->unmount_sem, 1, NULL);
			}
		}
	}
	delete name;
	ReleaseSemaphore(my_fs->mutex, 1, NULL);

	if (ret == 0) {
		return 0;
	}
	else {
		return 1;
	}	
}

char KernelFile::write(BytesCnt to_write, char * buffer)
{
	//file is open and i cannot write
	if (my_fcb->empty[0] == 'r') {
		return 0;
	}
	else if (cursor + to_write > MAX_FILE_SIZE) {
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
		return ret;
	}	
}

BytesCnt KernelFile::read(BytesCnt to_read, char* buffer)
{

	WaitForSingleObject(my_fs->mutex, INFINITE);
	if (my_fs->open_files_map.find(this->file_name) == my_fs->open_files_map.end()) {
		ReleaseSemaphore(my_fs->mutex, 1, NULL);
		return 0;

	}
	ReleaseSemaphore(my_fs->mutex, 1, NULL);
	if (cursor == end_of_file) {
		return 0;
	}

	long to_read_in_file = (end_of_file - cursor > to_read ? to_read : end_of_file - cursor);
	
	long cursor_in_cluster = cursor % ClusterSize;

	
	long i = 0;
	unsigned long index_2[512] = { 0 };
	if (!std::memcmp(index_1, index_2, 512 * sizeof(unsigned long))) {
		my_fs->readClusterFromPart(index_1_no, (char*)index_1);
	}
	char temp_buffer[ClusterSize] = { 0 };
	int ret = 1;
	int k = 0;

	if (last_index2_no == 0 || dirty_index2) {
		ret = my_fs->readClusterFromPart(index_1[index_1_entry], (char*)index_2);
		if (ret == 0) {

			std::cout << "ERROR READING INDEX2 CLUSTER  NO: " << index_1[index_1_entry] << std::endl;
			return CLUSTER_READING_ERR;
		}
		last_index2_no = index_1[index_1_entry];
		std::memcpy(last_index2, index_2, 512 * sizeof(unsigned long));
		dirty_index2 = false;

	}
	else if (last_index2_no != index_1[index_1_entry]) {
		ret = my_fs->readClusterFromPart(index_1[index_1_entry], (char*)last_index2);
		if (ret == 0) {

			std::cout << "ERROR READING INDEX2 CLUSTER  NO: " << index_1[index_1_entry] << std::endl;
			return CLUSTER_READING_ERR;
		}
		last_index2_no = index_1[index_1_entry];
		std::memcpy(index_2, last_index2, 512 * sizeof(unsigned long));
	}
	else {
		std::memcpy((char*)index_2, (char*)last_index2, ClusterSize * sizeof(char));
	}

	while (i < to_read_in_file) {
		
		ret = readCluster(index_2[index_2_entry], temp_buffer);
		if (ret == 0) {
			
			return CLUSTER_READING_ERR;
		}
		
		long to_read_in_cluster = (ClusterSize - cursor_in_cluster > to_read_in_file - i ? to_read_in_file - i : ClusterSize - cursor_in_cluster);

		for (long j = cursor_in_cluster; j < cursor_in_cluster + to_read_in_cluster; j++) {
			buffer[i++] = temp_buffer[j];

		}
		
		if (i < to_read_in_file) {
			cursor_in_cluster = 0;
			index_2_entry++;
			if (index_2_entry == 512) {
				index_1_entry++;
				index_2_entry = 0;
				if (index_1[index_1_entry]) {
					ret = my_fs->readClusterFromPart(index_1[index_1_entry], (char*)index_2);
					std::memcpy(last_index2, index_2, 512 * sizeof(unsigned long));
					last_index2_no = index_1[index_1_entry];
					if (ret == 0) {
						return CLUSTER_READING_ERR;
					}
				}
				else {
					dirty_index2 = true;
				}
				
			}
		}
	}
	cursor += to_read_in_file;
	if (cursor%ClusterSize == 0) {
		index_2_entry++;
		if (index_2_entry == 512) {
			index_2_entry = 0;
			index_1_entry++;
			dirty_index2 = true;
		}
	}
	
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
