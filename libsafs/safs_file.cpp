/*
 * Copyright 2014 Open Connectome Project (http://openconnecto.me)
 * Written by Da Zheng (zhengda1936@gmail.com)
 *
 * This file is part of SAFSlib.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <boost/format.hpp>

#include <limits.h>

#include "log.h"
#include "native_file.h"
#include "safs_file.h"
#include "RAID_config.h"
#include "io_interface.h"

namespace safs
{

static std::vector<int> shuffle_disks(int num_disks)
{
	std::vector<int> permute(num_disks);
	for (size_t i = 0; i < permute.size(); i++)
		permute[i] = i;
	random_shuffle(permute.begin(), permute.end());
	return permute;
}

safs_file::safs_file(const RAID_config &conf, const std::string &file_name)
{
	native_dirs = conf.get_disks();
	for (unsigned i = 0; i < native_dirs.size(); i++)
		native_dirs[i] = part_file_info(
				native_dirs[i].get_file_name() + "/" + file_name,
				native_dirs[i].get_disk_id(), native_dirs[i].get_node_id());
	this->name = file_name;
}

std::vector<std::string> safs_file::erase_header_file(
		const std::vector<std::string> &files)
{
	std::vector<std::string> ret;
	for (auto it = files.begin(); it != files.end(); it++)
		if (*it != "header")
			ret.push_back(*it);
	return ret;
}

bool safs_file::exist() const
{
	std::set<int> part_ids;
	for (unsigned i = 0; i < native_dirs.size(); i++) {
		native_dir dir(native_dirs[i].get_file_name());
		if (!dir.exist())
			return false;
		std::vector<std::string> files;
		dir.read_all_files(files);
		if (files.size() > 1)
			files = erase_header_file(files);
		if (files.size() != 1) {
			fprintf(stderr, "%s doesn't have exactly one file\n",
					dir.get_name().c_str());
			return false;
		}
		part_ids.insert(atoi(files[0].c_str()));
	}
	if (part_ids.size() < native_dirs.size()) {
		fprintf(stderr, "there are duplicated partition ids in %s.\n",
				name.c_str());
		return false;
	}
	return true;
}

ssize_t safs_file::get_size() const
{
	if (!exist())
		return -1;
	size_t ret = 0;
	for (unsigned i = 0; i < native_dirs.size(); i++) {
		native_dir dir(native_dirs[i].get_file_name());
		std::vector<std::string> local_files;
		dir.read_all_files(local_files);
		if (local_files.size() > 1)
			local_files = erase_header_file(local_files);
		assert(local_files.size() == 1);
		native_file f(dir.get_name() + "/" + local_files[0]);
		ret += f.get_size();
	}
	return ret;
}

bool safs_file::rename(const std::string &new_name)
{
	if (!exist())
		return false;

	for (unsigned i = 0; i < native_dirs.size(); i++) {
		native_file f(native_dirs[i].get_file_name());
		if (!f.rename(f.get_dir_name() + "/" + new_name))
			return false;
	}
	name = new_name;
	for (unsigned i = 0; i < native_dirs.size(); i++) {
		native_file f(native_dirs[i].get_file_name());
		native_dirs[i] = part_file_info(f.get_dir_name() + "/" + new_name,
				native_dirs[i].get_disk_id(), native_dirs[i].get_node_id());
	}
	return true;
}

bool safs_file::create_file(size_t file_size, int block_size,
		int mapping_option, safs_file_group::ptr group)
{
	size_t size_per_disk = file_size / native_dirs.size();
	if (file_size % native_dirs.size() > 0)
		size_per_disk++;
	size_per_disk = ROUNDUP(size_per_disk, 512);

	// We use the random index to reorder the native directories.
	// So different files map their data chunks to disks in different order.
	// The benefit is that when we access data in the same location but from
	// different files, the data is likely fetched from different disks.
	// Thus, this leads to better I/O utilization.
	std::vector<int> dir_idxs;
	if (group == NULL)
		dir_idxs = shuffle_disks(native_dirs.size());
	else
		dir_idxs = group->add_file(*this);

	safs_header header(block_size, mapping_option, true, file_size);
	for (unsigned i = 0; i < native_dirs.size(); i++) {
		native_dir dir(native_dirs[dir_idxs[i]].get_file_name());
		bool ret = dir.create_dir(true);
		if (!ret)
			return false;
		// We store the metadata of the SAFS in the directory that
		// stores the first part.
		if (i == 0) {
			printf("the first part is in %s\n", dir.get_name().c_str());
			header_file = dir.get_name() + "/header";
			FILE *f = fopen(header_file.c_str(), "w");
			if (f == NULL) {
				fprintf(stderr, "fopen %s: %s\n", header_file.c_str(),
						strerror(errno));
				return false;
			}
			size_t num_writes = fwrite(&header, sizeof(header), 1, f);
			if (num_writes != 1) {
				perror("fwrite");
				return false;
			}
			int ret = fclose(f);
			assert(ret == 0);
		}
		native_file f(dir.get_name() + "/" + itoa(i));
		ret = f.create_file(size_per_disk);
		if (!ret)
			return false;
	}
	assert(!header_file.empty());
	return true;
}

bool safs_file::delete_file()
{
	for (unsigned i = 0; i < native_dirs.size(); i++) {
		native_dir dir(native_dirs[i].get_file_name());
		bool ret = dir.delete_dir(true);
		if (!ret)
			return false;
	}
	return true;
}

std::string safs_file::get_header_file() const
{
	if (!header_file.empty())
		return header_file;

	for (size_t i = 0; i < native_dirs.size(); i++) {
		std::string dir_str = native_dirs[i].get_file_name();
		if (file_exist(dir_str) && file_exist(dir_str + "/header")) {
			const_cast<safs_file *>(this)->header_file = dir_str + "/header";
			break;
		}
	}
	return header_file;
}

safs_header safs_file::get_header() const
{
	std::string header_file = get_header_file();
	if (!file_exist(header_file))
		return safs_header();
	FILE *f = fopen(header_file.c_str(), "r");
	if (f == NULL) {
		fprintf(stderr, "fopen %s: %s\n", header_file.c_str(), strerror(errno));
		return safs_header();
	}
	safs_header header;
	size_t num_reads = fread(&header, sizeof(header), 1, f);
	if (num_reads != 1) {
		perror("fread");
		return safs_header();
	}
	int ret = fclose(f);
	assert(ret == 0);
	return header;
}

bool safs_file::set_user_metadata(const std::vector<char> &data)
{
	std::string header_file = get_header_file();
	native_file native_f(header_file);
	assert(native_f.exist());

	FILE *f = fopen(header_file.c_str(), "r+");
	if (f == NULL) {
		fprintf(stderr, "fopen %s: %s\n", header_file.c_str(), strerror(errno));
		return false;
	}
	int ret = fseek(f, safs_header::get_header_size(), SEEK_SET);
	if (ret != 0) {
		perror("fseek");
		return false;
	}
	size_t num_writes = fwrite(data.data(), data.size(), 1, f);
	if (num_writes != 1) {
		perror("fwrite");
		return false;
	}
	ret = fclose(f);
	assert(ret == 0);
	return true;
}

std::vector<char> safs_file::get_user_metadata() const
{
	std::string header_file = get_header_file();
	native_file native_f(header_file);
	assert(native_f.exist());
	size_t file_size = native_f.get_size();
	assert(file_size >= safs_header::get_header_size());
	if (file_size == safs_header::get_header_size())
		return std::vector<char>();

	FILE *f = fopen(header_file.c_str(), "r");
	if (f == NULL) {
		fprintf(stderr, "fopen %s: %s\n", header_file.c_str(), strerror(errno));
		return std::vector<char>();
	}
	int ret = fseek(f, safs_header::get_header_size(), SEEK_SET);
	if (ret != 0) {
		perror("fseek");
		return std::vector<char>();
	}
	std::vector<char> data(file_size - safs_header::get_header_size());
	size_t num_reads = fread(data.data(), data.size(), 1, f);
	if (num_reads != 1) {
		perror("fread");
		return std::vector<char>();
	}
	ret = fclose(f);
	assert(ret == 0);
	return data;
}

size_t get_all_safs_files(std::set<std::string> &files)
{
	std::set<std::string> all_files;
	const RAID_config &conf = get_sys_RAID_conf();

	// First find all individual file names in the root directories.
	for (int i = 0; i < conf.get_num_disks(); i++) {
		std::string dir_name = conf.get_disk(i).get_file_name();
		native_dir dir(dir_name);
		std::vector<std::string> file_names;
		dir.read_all_files(file_names);
		all_files.insert(file_names.begin(), file_names.end());
	}

	for (std::set<std::string>::const_iterator it = all_files.begin();
			it != all_files.end(); it++) {
		safs_file file(conf, *it);
		if (file.exist()) {
			files.insert(*it);
		}
		else {
			BOOST_LOG_TRIVIAL(error) << boost::format("%1% is corrupted")
				% file.get_name();
		}
	}
	return 0;
}

class naive_file_group: public safs_file_group
{
	size_t num_files;
	size_t num_disks;
public:
	naive_file_group(const RAID_config &conf) {
		num_files = 0;
		num_disks = conf.get_num_disks();
	}
	std::vector<int> add_file(safs_file &file) {
		std::vector<int> ret(num_disks);
		for (size_t i = 0; i < num_disks; i++)
			ret[i] = i;
		num_files++;
		return ret;
	}
	std::string get_name() const {
		return "naive";
	}
};

class rotate_file_group: public safs_file_group
{
	size_t num_files;
	size_t num_disks;
public:
	rotate_file_group(const RAID_config &conf) {
		num_files = 0;
		num_disks = conf.get_num_disks();
	}
	std::vector<int> add_file(safs_file &file) {
		std::vector<int> ret(num_disks);
		for (size_t i = 0; i < num_disks; i++)
			ret[i] = (num_files + i) % num_disks;
		num_files++;
		return ret;
	}
	std::string get_name() const {
		return "rotate";
	}
};

class rand_rotate_file_group: public safs_file_group
{
	// The base permute is the permutation that other permutations are based
	// on. Other permutations just rotate the base permutation from a random
	// location. Every #disks files share the same base permutation.
	std::vector<std::vector<int> > base_permutes;
	std::vector<std::vector<int> > rand_rotates;
	size_t num_files;
public:
	rand_rotate_file_group(const RAID_config &conf);
	std::vector<int> add_file(safs_file &file);
	std::string get_name() const {
		return "rand_rotate";
	}
};

rand_rotate_file_group::rand_rotate_file_group(const RAID_config &conf)
{
	int num_disks = conf.get_num_disks();
	num_files = 0;
	base_permutes.push_back(shuffle_disks(num_disks));
	// Every #disks files share the same base permutation.
	rand_rotates.push_back(shuffle_disks(num_disks));
}

std::vector<int> rand_rotate_file_group::add_file(safs_file &file)
{
	size_t num_disks = base_permutes.front().size();
	size_t base_idx = num_files / num_disks;
	if (base_idx >= base_permutes.size()) {
		base_permutes.push_back(shuffle_disks(num_disks));
		rand_rotates.push_back(shuffle_disks(num_disks));
	}
	assert(base_permutes.size() > base_idx);

	std::vector<int> base = base_permutes[base_idx];
	std::vector<int> ret(num_disks);
	size_t rotate = rand_rotates[base_idx][num_files % num_disks];
	for (size_t i = 0; i < ret.size(); i++)
		ret[i] = base[(rotate + i) % num_disks];
	num_files++;
	return ret;
}

safs_file_group::ptr safs_file_group::create(const RAID_config &conf,
		group_t type)
{
	switch (type) {
		case group_t::NAIVE:
			return safs_file_group::ptr(new naive_file_group(conf));
		case group_t::ROTATE:
			return safs_file_group::ptr(new rotate_file_group(conf));
		case group_t::RAND_ROTATE:
			return safs_file_group::ptr(new rand_rotate_file_group(conf));
		default:
			fprintf(stderr, "unknow group type: %d\n", type);
			return safs_file_group::ptr();
	}
}

bool exist_safs_file(const std::string &name)
{
	if (!is_safs_init())
		return false;

	safs::safs_file mat_f(safs::get_sys_RAID_conf(), name);
	return mat_f.exist();
}

ssize_t get_safs_size(const std::string &name)
{
	if (!is_safs_init())
		return -1;

	safs::safs_file mat_f(safs::get_sys_RAID_conf(), name);
	return mat_f.get_size();
}

}
