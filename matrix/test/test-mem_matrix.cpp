#include <stdio.h>
#include <eigen3/Eigen/Dense>

#include "common.h"

#include "mem_dense_matrix.h"

using namespace fm;

class set_col_operate: public type_set_operate<double>
{
	size_t num_cols;
public:
	set_col_operate(size_t num_cols) {
		this->num_cols = num_cols;
	}

	void set(double *arr, size_t num_eles, off_t row_idx, off_t col_idx) const {
		for (size_t i = 0; i < num_eles; i++) {
			arr[i] = (row_idx + i) * num_cols + col_idx;
		}
	}
};

/*
 * This function is to compare the performance of inner product between
 * in-memory column-wise dense matrix and Eigen matrix.
 * I assume Eigen matrix should give us the best performance.
 */
template<class Type>
void test1(size_t nrow, size_t ncol, size_t right_ncol)
{
	struct timeval start, end;

	gettimeofday(&start, NULL);
	typename type_mem_dense_matrix<Type>::ptr m1
		= type_mem_dense_matrix<Type>::create(nrow, ncol,
				matrix_layout_t::L_COL, set_col_operate(ncol));
	gettimeofday(&end, NULL);
	printf("It takes %.3f seconds to construct input column matrix\n",
			time_diff(start, end));
	typename type_mem_dense_matrix<Type>::ptr m2
		= type_mem_dense_matrix<Type>::create(ncol, right_ncol,
				matrix_layout_t::L_COL, set_col_operate(ncol));

	gettimeofday(&start, NULL);
	Eigen::Matrix<Type, Eigen::Dynamic, Eigen::Dynamic> eigen_m1(nrow, ncol);
#pragma omp parallel for
	for (size_t i = 0; i < nrow; i++) {
		for (size_t j = 0; j < ncol; j++)
			eigen_m1(i, j) = i * ncol + j;
	}
	gettimeofday(&end, NULL);
	printf("It takes %.3f seconds to construct input Eigen matrix\n",
			time_diff(start, end));

	Eigen::Matrix<Type, Eigen::Dynamic, Eigen::Dynamic> eigen_m2(ncol, right_ncol);
	for (size_t i = 0; i < ncol; i++) {
		for (size_t j = 0; j < right_ncol; j++)
			eigen_m2(i, j) = i * right_ncol + j;
	}

	start = end;
	typename type_mem_dense_matrix<Type>::ptr res1
		= multiply<Type, Type, Type>(*m1, *m2);
	gettimeofday(&end, NULL);
	printf("It takes %.3f seconds to multiply column matrix\n",
			time_diff(start, end));
	assert(res1->get_num_rows() == m1->get_num_rows());
	assert(res1->get_num_cols() == m2->get_num_cols());
	printf("The result matrix has %ld rows and %ld columns\n",
			res1->get_num_rows(), res1->get_num_cols());

	start = end;
	Eigen::Matrix<Type, Eigen::Dynamic, Eigen::Dynamic> eigen_res = eigen_m1 * eigen_m2;
	gettimeofday(&end, NULL);
	assert((size_t) eigen_res.rows() == res1->get_num_rows());
	assert((size_t) eigen_res.cols() == res1->get_num_cols());
	printf("It takes %.3f seconds to multiply Eigen matrix\n",
			time_diff(start, end));

#pragma omp parallel for
	for (size_t i = 0; i < res1->get_num_rows(); i++) {
		for (size_t j = 0; j < res1->get_num_cols(); j++) {
			assert(res1->get(i, j) == eigen_res(i, j));
		}
	}
}

int main()
{
	size_t nrow = 1024 * 1024 * 124;
	size_t ncol = 5;
	test1<double>(nrow, ncol, ncol);
}
