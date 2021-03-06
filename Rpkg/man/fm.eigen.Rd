% Generated by roxygen2 (4.1.1): do not edit by hand
% Please edit documentation in R/flashmatrix.R
\name{fm.eigen}
\alias{fm.eigen}
\title{Eigensolver}
\usage{
fm.eigen(func, extra = NULL, sym = TRUE, options = NULL,
  env = parent.frame())
}
\arguments{
\item{func}{The function to perform the matrix-vector multiplication.}

\item{extra}{Extra argument to supply to `func'.}

\item{sym}{Logical scalar, whether the input matrix is symmetric.}

\item{options}{Options to Anasazi.

The `options' argument specifies what kind of computation to perform.
It is a list with the following members, which correspond directly to
Anasazi parameters:

nev Numeric scalar. The number of eigenvalues to be computed.

solver String. The name of the eigensolver to solve the eigenproblems.
                Currently, it supports three eigensolvers: KrylovSchur,
                Davidson and LOBPCG. KrylovSchur is the default eigensolver.

tol Numeric scalar. Stopping criterion: the relative accuracy of
                the Ritz value is considered acceptable if its error is less
                than `tol' times its estimated value.

block_size Numeric scalar. The eigensolvers use a block extension of an
                eigensolver algorithm. The block size determines the number
                of the vectors that operate together.

num_blocks Numeric scalar. The number of blocks to compute eigenpairs.

which Specify which eigenvalues/vectors to compute, character
             constant with exactly two characters.
Possible values for symmetric input matrices:
\itemize{
\item{"LA"}{Compute `nev' largest (algebraic) eigenvalues.}
\item{"SA"}{Compute `nev' smallest (algebraic) eigenvalues.}
\item{"LM"}{Compute `nev' largest (in magnitude) eigenvalues.}
\item{"SM"}{Compute `nev' smallest (in magnitude) eigenvalues.}
}}
}
\value{
A named list with the following members:
        values: Numeric vector, the desired eigenvalues.
        vectors: Numeric matrix, the desired eigenvectors as columns.
}
\description{
Compute eigenvalues/vectors of the adjacency matrix of an undirected graph.
}
\details{
This eigensolver is powered by Anasazi package of Trilinos.
}
\author{
Da Zheng <dzheng5@jhu.edu>
}

