#ifndef __EM_DENSE_MATRIX_H__
#define __EM_DENSE_MATRIX_H__

/*
 * Copyright 2014 Open Connectome Project (http://openconnecto.me)
 * Written by Da Zheng (zhengda1936@gmail.com)
 *
 * This file is part of FlashMatrix.
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

#include <memory>
#include <boost/format.hpp>

#include "log.h"

#include "bulk_operate.h"
#include "matrix_store.h"
#include "EM_object.h"
#include "mem_worker_thread.h"

namespace fm
{

namespace detail
{

class local_matrix_store;
class mem_matrix_store;

class EM_matrix_store: public matrix_store, public EM_object
{
	/*
	 * The difference between the two identifiers are:
	 * `mat_id' identifies the matrix data structure. Whenever the matrix
	 * is shallow copied or transposed, `mat_id' changes.
	 * `data_id' identifies the content in a matrix.
	 * So when a matrix is transposed or shallow copied, it should share
	 * the same data id.
	 */
	const size_t mat_id;
	const size_t data_id;

	matrix_layout_t layout;
	file_holder::ptr holder;
	io_set::ptr ios;

	/*
	 * This indicates whether or not we cache a portion in each worker thread.
	 * By default, this is enabled.
	 */
	bool cache_portion;

	/*
	 * These two fields are used for sub matrix.
	 * They indicates the actual number of rows and columns stored on disks.
	 * In contrast, get_num_rows() and get_num_cols() are #rows and columns
	 * exposed to users.
	 */
	size_t orig_num_rows;
	size_t orig_num_cols;

	size_t get_orig_num_rows() const {
		return orig_num_rows;
	}

	size_t get_orig_num_cols() const {
		return orig_num_cols;
	}

	EM_matrix_store(size_t nrow, size_t ncol, matrix_layout_t layout,
			const scalar_type &type, safs::safs_file_group::ptr group);
	EM_matrix_store(file_holder::ptr holder, io_set::ptr ios, size_t nrow,
			size_t ncol, size_t orig_nrow, size_t orig_ncol,
			matrix_layout_t layout, const scalar_type &type, size_t _data_id);
public:
	static const size_t CHUNK_SIZE;

	typedef std::shared_ptr<EM_matrix_store> ptr;
	typedef std::shared_ptr<const EM_matrix_store> const_ptr;

	static ptr create(const std::string &mat_file);

	static ptr create(size_t nrow, size_t ncol, matrix_layout_t layout,
			const scalar_type &type, safs::safs_file_group::ptr group = NULL) {
		return ptr(new EM_matrix_store(nrow, ncol, layout, type, group));
	}

	static ptr cast(matrix_store::ptr store) {
		return std::dynamic_pointer_cast<EM_matrix_store>(store);
	}

	static const_ptr cast(matrix_store::const_ptr store) {
		return std::dynamic_pointer_cast<const EM_matrix_store>(store);
	}

	virtual void set_cache_portion(bool cache_portion) {
		this->cache_portion = cache_portion;
	}

	bool is_cache_portion() const {
		return cache_portion;
	}

	virtual std::unordered_map<size_t, size_t> get_underlying_mats() const {
		std::unordered_map<size_t, size_t> ret;
		ret.insert(std::pair<size_t, size_t>(data_id,
					get_num_rows() * get_num_cols()));
		return ret;
	}
	virtual std::string get_name() const {
		return (boost::format("EM_mat-%1%(%2%,%3%)") % mat_id % get_num_rows()
			% get_num_cols()).str();
	}

	virtual void reset_data();
	virtual void set_data(const set_operate &op);

	virtual matrix_layout_t store_layout() const {
		return layout;
	}

	virtual matrix_store::const_ptr transpose() const;

	virtual std::vector<safs::io_interface::ptr> create_ios() const;

	virtual std::shared_ptr<const local_matrix_store> get_portion(
			size_t start_row, size_t start_col, size_t num_rows,
			size_t num_cols) const;
	virtual std::shared_ptr<local_matrix_store> get_portion(
			size_t start_row, size_t start_col, size_t num_rows,
			size_t num_cols);
	virtual int get_portion_node_id(size_t id) const {
		return -1;
	}

	virtual std::pair<size_t, size_t> get_portion_size() const;
	virtual async_cres_t get_portion_async(size_t start_row, size_t start_col,
			size_t num_rows, size_t num_cols,
			portion_compute::ptr compute) const;
	virtual async_res_t get_portion_async(size_t start_row, size_t start_col,
			size_t num_rows, size_t num_cols,
			portion_compute::ptr compute);
	virtual void write_portion_async(
			std::shared_ptr<const local_matrix_store> portion,
			off_t start_row, off_t start_col);

	virtual matrix_store::const_ptr get_cols(
			const std::vector<off_t> &idxs) const;
	virtual matrix_store::const_ptr get_rows(
			const std::vector<off_t> &idxs) const;
	virtual std::shared_ptr<const vec_store> get_col_vec(off_t idx) const;
	virtual std::shared_ptr<const vec_store> get_row_vec(off_t idx) const;

	/*
	 * Set this matrix persistent in SAFS, so that even if there isn't
	 * a reference to the matrix, its data still stored in SAFS.
	 * This method isn't thread-safe.
	 */
	bool set_persistent(const std::string &name) const;
	/*
	 * Unset the persistency of the matrix in SAFS, so that the matrix file
	 * is deleted after all references to the matrix are gone.
	 */
	void unset_persistent() const;
};

}

}

#endif
