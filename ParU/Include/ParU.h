// ============================================================================/
// ======================= ParU.h =============================================/
// ============================================================================/

// ParU, Copyright (c) 2022-2024, Mohsen Aznaveh and Timothy A. Davis,
// All Rights Reserved.
// SPDX-License-Identifier: GPL-3.0-or-later

//------------------------------------------------------------------------------

// This is the ParU.h file. All user callable routines are in this file and
// all of them start with ParU_*. This file must be included in all user code
// that use ParU.
//
// ParU is a parallel sparse direct solver. This package uses OpenMP tasking
// for parallelism. ParU calls UMFPACK for symbolic analysis phase, after that
// some symbolic analysis is done by ParU itself and then numeric phase
// starts. The numeric computation is a task parallel phase using OpenMP and
// each task calls parallel BLAS; i.e. nested parallism.
//
// The performance of BLAS has a heavy impact on the performance of ParU.
// However, depending on the input problem performance of parallelism in BLAS
// sometimes does not have an effect on the ParU performance.
//
// General Usage for solving Ax = b, where A is a sparse matrix in a CHOLMOD
// sparse matrix data structure with double entries and b is a dense vector of
// double (or a dense matrix B for multiple rhs):
//
//      info = ParU_Analyze(A, &Sym, &Control);
//      info = ParU_Factorize(A, Sym, &Num, &Control);
//      info = ParU_Solve(Sym, Num, b, x, &Control);
//
// See paru_demo for more examples

#ifndef PARU_H
#define PARU_H

// ============================================================================/
// include files and ParU version
// ============================================================================/

#include "SuiteSparse_config.h"
#include "cholmod.h"
#include "umfpack.h"

typedef enum ParU_Info
{
    PARU_SUCCESS = 0,
    PARU_OUT_OF_MEMORY = -1,  
    PARU_INVALID = -2,
    PARU_SINGULAR = -3,
    PARU_TOO_LARGE = -4
} ParU_Info;

#define PARU_MEM_CHUNK (1024*1024)

#define PARU_DATE "Apr XX, 2024"
#define PARU_VERSION_MAJOR  1
#define PARU_VERSION_MINOR  0
#define PARU_VERSION_UPDATE 0

#define PARU__VERSION SUITESPARSE__VERCODE(1,0,0)
#if !defined (SUITESPARSE__VERSION) || \
    (SUITESPARSE__VERSION < SUITESPARSE__VERCODE(7,7,0))
#error "ParU 1.0.0 requires SuiteSparse_config 7.7.0 or later"
#endif

#if !defined (UMFPACK__VERSION) || \
    (UMFPACK__VERSION < SUITESPARSE__VERCODE(6,3,3))
#error "ParU 1.0.0 requires UMFPACK 6.3.3 or later"
#endif

#if !defined (CHOLMOD__VERSION) || \
    (CHOLMOD__VERSION < SUITESPARSE__VERCODE(5,2,1))
#error "ParU 1.0.0 requires CHOLMOD 5.2.1 or later"
#endif

//  the same values as UMFPACK_STRATEGY defined in UMFPACK/Include/umfpack.h
#define PARU_STRATEGY_AUTO 0         // decided to use sym. or unsym. strategy
#define PARU_STRATEGY_UNSYMMETRIC 1  // COLAMD(A), metis, ...
#define PARU_STRATEGY_SYMMETRIC 3    // prefer diagonal

// =============================================================================
// ParU C++ definitions ========================================================
// =============================================================================

#ifdef __cplusplus

// The following definitions are only available from C++:

// silence these diagnostics:
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++11-extensions"
#endif

// =============================================================================
// =========================== ParU_Symbolic ===================================
// =============================================================================
//
// The contents of this object do not change during numeric factorization.  The
// ParU_U_singleton and ParU_L_singleton are datastructures for singletons that
// has been borrowed from UMFPACK, but it is saved differently
//
//              ParU_L_singleton is CSC
//                                     |
//                                     v
//    ParU_U_singleton is CSR -> U U U U U U U U U
//                               . U U U U U U U U
//                               . . U U U U U U U
//                               . . . L . . . . .
//                               . . . L L . . . .
//                               . . . L L S S S S
//                               . . . L L S S S S
//                               . . . L L S S S S
//                               . . . L L S S S S

struct ParU_U_singleton
{
    // CSR format for U singletons
    int64_t nnz;   // nnz in submatrix
    int64_t *Sup;  // size cs1
    int64_t *Suj;  // size is computed
};

struct ParU_L_singleton
{
    // CSC format for L singletons
    int64_t nnz;   // nnz in submatrix
    int64_t *Slp;  // size rs1
    int64_t *Sli;  // size is computed
};

struct ParU_Symbolic
{
    // -------------------------------------------------------------------------
    // row-form of the input matrix and its permutations
    // -------------------------------------------------------------------------

    // During symbolic analysis, the nonzero pattern of S = A(P,Q) is
    // constructed, where A is the user's input matrix.  Its numerical values
    // are not constructed.
    // The matrix S is stored in row-oriented form.  The rows of S are
    // sorted according to their leftmost column index (via Pinv).  Column
    // indices in each row of S are in strictly ascending order, even though
    // the input matrix A need not be sorted. User can look inside S matrix.

    int64_t m, n, anz;  // S is m-by-n with anz entries

    int64_t snz;  // nnz in submatrix
    int64_t *Sp;  // size m+1-n1, row pointers of S
    int64_t *Sj;  // size snz = Sp [n], column indices of S

    // Usingletons and Lsingltons
    ParU_U_singleton ustons;
    ParU_L_singleton lstons;

    int64_t *Qfill;  // size n, fill-reducing column permutation.
    // Qfill [k] = j if column k of A is column j of S.

    int64_t *Pinit;  // size m, row permutation.
    // UMFPACK computes it and Pinv  is its inverse
    int64_t *Pinv;  // Inverse of Pinit; it is used to make S

    int64_t *Diag_map;  // size n,
    // UMFPACK computes it and I use it to find original diags out of it

    int64_t *Sleft;  // size n-n1+2.  The list of rows of S whose
    // leftmost column index is j is given by
    // Sleft [j] ... Sleft [j+1]-1.  This can be empty (that is, Sleft
    // [j] can equal Sleft [j+1]).  Sleft [n] is the number of
    // non-empty rows of S, and Sleft [n+1] == m.  That is, Sleft [n]
    // ... Sleft [n+1]-1 gives the empty rows of S, if any.

    int64_t strategy;  // the strategy that is actually used by umfpack
    // symmetric or unsymmetric

    // -------------------------------------------------------------------------
    // frontal matrices: pattern and tree
    // -------------------------------------------------------------------------

    // Each frontal matrix is fm-by-fn, with fnpiv pivot columns.  The fn
    // column indices are given by a set of size fnpiv pivot columns

    int64_t nf;  // number of frontal matrices; nf <= MIN (m,n)
    int64_t n1;  // number of singletons in the matrix
    // the matrix S is the one without any singletons
    int64_t rs1, cs1;  // number of row and column singletons, n1 = rs1+cs1;

    // parent, child, and childp define the row merge tree or etree (A'A)
    int64_t *Parent;  // size nf+1  Add another node just to make the forest a
    int64_t *Child;   // size nf+1      tree
    int64_t *Childp;  // size nf+2

    int64_t *Depth;  // size nf distance of each node from the root

    // The parent of a front f is Parent [f], or EMPTY if f=nf.
    // A list of children of f can be obtained in the list
    // Child [Childp [f] ... Childp [f+1]-1].

    // Node nf in the tree is a placeholder; it does not represent a frontal
    // matrix.  All roots of the frontal "tree" (may be a forest) have the
    // placeholder node nf as their parent.  Thus, the tree of nodes 0:nf is
    // truly a tree, with just one parent (node nf).

    int64_t *aParent;  // size m+nf
    int64_t *aChild;   // size m+nf+1
    int64_t *aChildp;  // size m+nf+2
    int64_t *first;    // size nf+1 first successor of front in the tree;
    // all successors are between first[f]...f-1

    // pivot column in the front F.  This refers to a column of S.  The
    // number of expected pivot columns in F is thus
    // Super [f+1] - Super [f].

    // Upper bound number of rows for each front
    int64_t *Fm;  // size nf+1

    // Upper bound  number of rows in the contribution block of each front
    int64_t *Cm;  // size nf+1

    int64_t *Super;  // size nf+1.  Super [f] gives the first
    // pivot column in the front F.  This refers to a column of S.  The
    // number of expected pivot columns in F is thus
    // Super [f+1] - Super [f].

    int64_t *row2atree;    // Mapping from rows to augmented tree size m
    int64_t *super2atree;  // Mapping from super nodes to augmented tree size nf

    int64_t *Chain_start;  // size = n_col +1;  actual size = nfr+1
    // The kth frontal matrix chain consists of frontal
    // matrices Chain_start [k] through Chain_start [k+1]-1.
    // Thus, Chain_start [0] is always 0 and
    // Chain_start[nchains] is the total number of frontal
    // matrices, nfr. For two adjacent fornts f and f+1
    // within a single chian, f+1 is always the parent of f
    // (that is, Front_parent [f] = f+1).

    int64_t *Chain_maxrows;  // size = n_col +1;  actual size = nfr+1
    int64_t *Chain_maxcols;  // The kth frontal matrix chain requires a single
    // working array of dimension Chain_maxrows [k] by
    // Chain_maxcols [k], for the unifrontal technique that
    // factorizes the frontal matrix chain. Since the
    // symbolic factorization only provides

    // only used for statistics when debugging is enabled:
    int64_t Us_bound_size;   // Upper bound on size of all Us, sum all fp*fn
    int64_t LUs_bound_size;  // Upper bound on size of all LUs, sum all fp*fm
    int64_t row_Int_bound;   // Upper bound on size of all ints for rows
    int64_t col_Int_bound;   // Upper bound on size of all ints for cols

    double *front_flop_bound;  // bound on m*n*k for each front size nf+1
    double *stree_flop_bound;  // flop bound for front and descendents size nf+1

    // data structure related to ParU tasks
    int64_t ntasks;        // number of tasks; at most nf
    int64_t *task_map;     // each task does the fronts
                       // from task_map[i]+1 to task_map[i+1]; task_map[0] is -1
    int64_t *task_parent;  // tree data structure for tasks
    int64_t *task_num_child;  // number of children of each task
    int64_t *task_depth;      // max depth of each task
};

// =============================================================================
// =========================== ParU_Control ====================================
// =============================================================================
// The default value of some control options can be found here. All user
// callable functions use ParU_Control; some controls are used only in symbolic
// phase and some controls are only used in numeric phase.

struct ParU_Control
{
    int64_t mem_chunk = PARU_MEM_CHUNK ;  // chunk size for memset and memcpy

    // Symbolic controls
    int64_t umfpack_ordering = UMFPACK_ORDERING_METIS;
    int64_t umfpack_strategy = UMFPACK_STRATEGY_AUTO; //symmetric or unsymmetric
    int64_t umfpack_default_singleton = 1; //filter singletons if true

    int64_t relaxed_amalgamation_threshold =
        32;  // symbolic analysis tries that each front have more pivot columns
             // than this threshold

    // Numeric controls
    int64_t scale = 1;         // if 1 matrix will be scaled using max_row
    int64_t panel_width = 32;  // width of panel for dense factorizaiton
    int64_t paru_strategy = PARU_STRATEGY_AUTO;//the same strategy umfpack used

    double piv_toler = 0.1;     // tolerance for accepting sparse pivots
    double diag_toler = 0.001;  // tolerance for accepting symmetric pivots
    int64_t trivial = 4; //dgemms with sizes less than trivial doesn't call BLAS
    int64_t worthwhile_dgemm = 512;  // dgemms bigger than worthwhile are tasked
    int64_t worthwhile_trsm = 4096;  // trsm bigger than worthwhile are tasked
    int32_t paru_max_threads = 0;  //It will be initialized with omp_max_threads
                                 // if the user do not provide a smaller number
};

// =============================================================================
// =========================== ParU_Numeric ====================================
// =============================================================================
// ParU_Numeric contains all the numeric information that user needs for solving
// a system. The factors are saved as a seried of dense matrices. User can check
// the ParU_Info to see if the factorization is successful. sizes of
// ParU_Numeric is size of S matrix in Symbolic analysis.

struct ParU_Factors
{               // dense factorized part pointer
    int64_t m, n;   //  mxn dense matrix
    double *p;  //  point to factorized parts
};

struct ParU_Numeric
{
    int64_t m, n;   // size of the sumbatrix(S) that is factorized
    int64_t sym_m;  // number of rows of original matrix; a copy of Sym->m

    int64_t nf;      // number of fronts copy of Sym->nf
    double *Rs;  // the array for row scaling based on original matrix
                 // size = m

    // Permutations are computed after all the factorization
    int64_t *Ps;  // size m, row permutation.
    // Permutation from S to LU. needed for lsolve and usolve
    int64_t *Pfin;  // size m, row permutation.
    // ParU final permutation.

    int64_t snz;     // nnz in S; copy of Sym->snz
    double *Sx;  // size snz = Sp [n], numeric values of (scaled) S;
    // Sp and Sj must be initialized in Symbolic phase
    int64_t sunz;
    double *Sux;  // Numeric values u singletons, Sup Suj are in symbolic
    int64_t slnz;
    double *Slx;  // Numeric values l singletons, Slp Sli are in symbolic

    ParU_Control *Control;  // a copy of controls for internal use
    // it is freed after factorize

    // Computed parts of each front
    int64_t *frowCount;  // size nf   size(CB) = rowCount[f]x
    int64_t *fcolCount;  // size nf                        colCount[f]
    int64_t **frowList;  // size nf   frowList[f] is rows of the matrix S
    int64_t **fcolList;  // size nf   colList[f] is non pivotal cols of the
    //   matrix S
    ParU_Factors *partial_Us;   // size nf   size(Us)= fp*colCount[f]
    ParU_Factors *partial_LUs;  // size nf   size(LUs)= rowCount[f]*fp

    int64_t max_row_count;  // maximum number of rows/cols for all the fronts
    int64_t max_col_count;  // it is initalized after factorization

    double rcond;
    double min_udiag;
    double max_udiag;
    ParU_Info res;  // returning value of numeric phase
};

//------------------------------------------------------------------------------
// ParU_Version:
//------------------------------------------------------------------------------

// return the version and date of the ParU library.

ParU_Info ParU_Version (int ver [3], char date [128]);

//------------------------------------------------------------------------------
// ParU_Analyze: Symbolic analysis is done in this routine. UMFPACK is called
// here and after that some more specialized symbolic computation is done for
// ParU. ParU_Analyze can be called once and can be used for different
// ParU_Factorize calls.
//------------------------------------------------------------------------------

ParU_Info ParU_Analyze
(
    // input:
    cholmod_sparse *A,  // input matrix to analyze of size n-by-n
    // output:
    ParU_Symbolic **Sym_handle,  // output, symbolic analysis
    // control:
    ParU_Control *Control
) ;

//------------------------------------------------------------------------------
// ParU_Factorize: Numeric factorization is done in this routine. Scaling and
// making Sx matrix, computing factors and permutations is here. ParU_Symbolic
// structure is computed ParU_Analyze and is an input in this routine.
//------------------------------------------------------------------------------

ParU_Info ParU_Factorize
(
    // input:
    cholmod_sparse *A,  // input matrix to factorize
    ParU_Symbolic *Sym, // symbolic analsys from ParU_Analyze
    // output:
    ParU_Numeric **Num_handle,
    // control:
    ParU_Control *Control
) ;

//------------------------------------------------------------------------------
//--------------------- Solve routines -----------------------------------------
//------------------------------------------------------------------------------

// In all the solve routines Num structure must come with the same Sym struct
// that comes from ParU_Factorize

// The vectors x and b have length n, where the matrix factorized is n-by-n.
// The matrices X and B have size n-by-nrhs, and are held in column-major
// storage.

//-------- x = A\x -------------------------------------------------------------
ParU_Info ParU_Solve
(
    // input:
    ParU_Symbolic *Sym,     // symbolic analysis from ParU_Analyze
    ParU_Numeric *Num,      // numeric factorization from ParU_Factorize
    // input/output:
    double *x,              // vector of size n-by-1; right-hand on input,
                            // solution on output
    // control:
    ParU_Control *Control
) ;

//-------- x = A\b -------------------------------------------------------------
ParU_Info ParU_Solve
(
    // input:
    ParU_Symbolic *Sym,     // symbolic analysis from ParU_Analyze
    ParU_Numeric *Num,      // numeric factorization from ParU_Factorize
    double *b,              // vector of size n-by-1
    // output
    double *x,              // vector of size n-by-1
    // control:
    ParU_Control *Control
) ;

//-------- X = A\X -------------------------------------------------------------
ParU_Info ParU_Solve
(
    // input
    ParU_Symbolic *Sym,     // symbolic analysis from ParU_Analyze
    ParU_Numeric *Num,      // numeric factorization from ParU_Factorize
    int64_t nrhs,           // # of right-hand sides
    // input/output:
    double *X,              // X is n-by-nrhs, where A is n-by-n;
                            // holds B on input, solution X on input
    // control:
    ParU_Control *Control
) ;

//-------- X = A\B -------------------------------------------------------------
ParU_Info ParU_Solve
(
    // input
    ParU_Symbolic *Sym,     // symbolic analysis from ParU_Analyze
    ParU_Numeric *Num,      // numeric factorization from ParU_Factorize
    int64_t nrhs,           // # of right-hand sides
    double *B,              // n-by-nrhs, in column-major storage
    // output:
    double *X,              // n-by-nrhs, in column-major storage
    // control:
    ParU_Control *Control
) ;

// Solve L*x=b where x and b are vectors (no scaling or permutations)
ParU_Info ParU_LSolve
(
    // input
    ParU_Symbolic *Sym,     // symbolic analysis from ParU_Analyze
    ParU_Numeric *Num,      // numeric factorization from ParU_Factorize
    // input/output:
    double *x,              // n-by-1, in column-major storage;
                            // holds b on input, solution x on input
    // control:
    ParU_Control *Control
) ;

// Solve L*X=B where X and B are matrices (no scaling or permutations)
ParU_Info ParU_LSolve
(
    // input
    ParU_Symbolic *Sym,     // symbolic analysis from ParU_Analyze
    ParU_Numeric *Num,      // numeric factorization from ParU_Factorize
    int64_t nrhs,           // # of right-hand-sides (# columns of X)
    // input/output:
    double *X,              // X is n-by-nrhs, where A is n-by-n;
                            // holds B on input, solution X on input
    // control:
    ParU_Control *Control
) ;

// Solve U*x=b where x and b are vectors (no scaling or permutations)
ParU_Info ParU_USolve
(
    // input
    ParU_Symbolic *Sym,     // symbolic analysis from ParU_Analyze
    ParU_Numeric *Num,      // numeric factorization from ParU_Factorize
    // input/output
    double *x,              // n-by-1, in column-major storage;
                            // holds b on input, solution x on input
    // control:
    ParU_Control *Control
) ;

// Solve U*X=B where X and B are matrices (no scaling or permutations)
ParU_Info ParU_USolve
(
    // input
    ParU_Symbolic *Sym,     // symbolic analysis from ParU_Analyze
    ParU_Numeric *Num,      // numeric factorization from ParU_Factorize
    int64_t nrhs,           // # of right-hand-sides (# columns of X)
    // input/output:
    double *X,              // X is n-by-nrhs, where A is n-by-n;
                            // holds B on input, solution X on input
    // control:
    ParU_Control *Control
) ;

//------------------------------------------------------------------------------
// permutation and inverse permutation, with optional scaling
//------------------------------------------------------------------------------

// apply inverse perm x(p) = b, or with scaling: x(p)=b ; x=x./s
ParU_Info ParU_InvPerm
(
    // inputs
    const int64_t *P,   // permutation vector of size n
    const double *s,    // vector of size n (optional)
    const double *b,    // vector of size n
    int64_t n,          // length of P, s, B, and X
    // output
    double *x,          // vector of size n
    // control:
    ParU_Control *Control
) ;

// apply inverse perm X(p,:) = B or with scaling: X(p,:)=B ; X = X./s
ParU_Info ParU_InvPerm
(
    // inputs
    const int64_t *P,   // permutation vector of size nrows
    const double *s,    // vector of size nrows (optional)
    const double *B,    // array of size nrows-by-ncols
    int64_t nrows,      // # of rows of X and B
    int64_t ncols,      // # of columns of X and B
    // output
    double *X,          // array of size nrows-by-ncols
    // control:
    ParU_Control *Control
) ;

// apply perm and scale x = b(P) / s
ParU_Info ParU_Perm
(
    // inputs
    const int64_t *P,   // permutation vector of size n
    const double *s,    // vector of size n (optional)
    const double *b,    // vector of size n
    int64_t n,          // length of P, s, B, and X
    // output
    double *x,          // vector of size n
    // control:
    ParU_Control *Control
) ;

// apply perm and scale X = B(P,:) / s
ParU_Info ParU_Perm
(
    // inputs
    const int64_t *P,   // permutation vector of size nrows
    const double *s,    // vector of size nrows (optional)
    const double *B,    // array of size nrows-by-ncols
    int64_t nrows,      // # of rows of X and B
    int64_t ncols,      // # of columns of X and B
    // output
    double *X,          // array of size nrows-by-ncols
    // control:
    ParU_Control *Control
) ;

//------------------------------------------------------------------------------
//-------------- computing residual --------------------------------------------
//------------------------------------------------------------------------------

// The user provide both x and b
// resid = norm1(b-A*x) / (norm1(A) * norm1 (x))
ParU_Info ParU_Residual
(
    // inputs:
    cholmod_sparse *A,  // an n-by-n sparse matrix
    double *x,          // vector of size n
    double *b,          // vector of size n
    // output:
    double &resid,      // residual: norm1(b-A*x) / (norm1(A) * norm1 (x))
    double &anorm,      // 1-norm of A
    double &xnorm,      // 1-norm of x
    // control:
    ParU_Control *Control
) ;

// resid = norm1(B-A*X) / (norm1(A) * norm1 (X))
// (multiple rhs)
ParU_Info ParU_Residual
(
    // inputs:
    cholmod_sparse *A,  // an n-by-n sparse matrix
    double *X,          // array of size n-by-nrhs
    double *B,          // array of size n-by-nrhs
    int64_t nrhs,
    // output:
    double &resid,      // residual: norm1(B-A*X) / (norm1(A) * norm1 (X))
    double &anorm,      // 1-norm of A
    double &xnorm,      // 1-norm of X
    // control:
    ParU_Control *Control
) ;

//------------------------------------------------------------------------------
//------------ Free routines----------------------------------------------------
//------------------------------------------------------------------------------

ParU_Info ParU_FreeNumeric
(
    // input/output:
    ParU_Numeric **Num_handle,  // numeric object to free
    // control:
    ParU_Control *Control
) ;

ParU_Info ParU_FreeSymbolic
(
    // input/output:
    ParU_Symbolic **Sym_handle, // symbolic object to free
    // control:
    ParU_Control *Control
) ;

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif

// =============================================================================
// ParU C definitions ==========================================================
// =============================================================================

// The following definitions are available in both C and C++:

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// =============================================================================
// ========================= ParU_C_Control ====================================
// =============================================================================

// Just like ParU_Control in the C++ interface.  The only difference is the
// initialization which is handled in the C interface, ParU_C_Init_Control.

typedef struct ParU_C_Control_struct
{
    int64_t mem_chunk;  // chunk size for memset and memcpy

    // Symbolic controls
    int64_t umfpack_ordering;
    int64_t umfpack_strategy;  // symmetric or unsymmetric
    int64_t umfpack_default_singleton; //filter singletons if true

    int64_t relaxed_amalgamation_threshold;
    // symbolic analysis tries that each front have more pivot columns
    // than this threshold

    // Numeric controls
    int64_t scale;         // if 1 matrix will be scaled using max_row
    int64_t panel_width;  // width of panel for dense factorizaiton
    int64_t paru_strategy;  // the same strategy umfpack used

    double piv_toler;     // tolerance for accepting sparse pivots
    double diag_toler;  // tolerance for accepting symmetric pivots
    int64_t trivial;  // dgemms with sizes less than trivial doesn't call BLAS
    int64_t worthwhile_dgemm;  // dgemms bigger than worthwhile are tasked
    int64_t worthwhile_trsm;  // trsm bigger than worthwhile are tasked
    int32_t paru_max_threads;    // It will be initialized with omp_max_threads
    // if the user do not provide a smaller number
} ParU_C_Control;

// =========================================================================
// ========================= ParU_C_Symbolic ===============================
// =========================================================================

// just a carrier for the C++ data structure

typedef struct ParU_C_Symbolic_struct
{
    int64_t m, n, anz;
    int64_t *Qfill ;        // a shallow pointer to the C++ Sym->Qfill
    void *sym_handle;
} ParU_C_Symbolic;

// =========================================================================
// ========================= ParU_C_Numeric ================================
// =========================================================================

// just a carrier for the C++ data structure

typedef struct ParU_C_Numeric_struct
{
    double rcond;
    int64_t *Pfin ;         // a shallow pointer to the C++ Num->Pfin
    double *Rs ;            // a shallow pointer to the C++ Num->Rs
    void *num_handle;
} ParU_C_Numeric;

//------------------------------------------------------------------------------
// ParU_Version: return the version and date of ParU
//------------------------------------------------------------------------------

ParU_Info ParU_C_Version (int ver [3], char date [128]);

//------------------------------------------------------------------------------
// ParU_C_Init_Control: initialize C data structure
//------------------------------------------------------------------------------

ParU_Info ParU_C_Init_Control (ParU_C_Control *Control_C) ;

//------------------------------------------------------------------------------
// ParU_C_Analyze: Symbolic analysis is done in this routine. UMFPACK is called
// here and after that some more speciaized symbolic computation is done for
// ParU. ParU_Analyze can be called once and can be used for different
// ParU_Factorize calls. 
//------------------------------------------------------------------------------

ParU_Info ParU_C_Analyze
(
    // input:
    cholmod_sparse *A,  // input matrix to analyze of size n-by-n
    // output:
    ParU_C_Symbolic **Sym_handle_C,  // output, symbolic analysis
    // control:
    ParU_C_Control *Control_C
) ;

//------------------------------------------------------------------------------
// ParU_C_Factorize: Numeric factorization is done in this routine. Scaling and
// making Sx matrix, computing factors and permutations is here.
// ParU_C_Symbolic structure is computed ParU_Analyze and is an input in this
// routine.
//------------------------------------------------------------------------------

ParU_Info ParU_C_Factorize
(
    // input:
    cholmod_sparse *A,          // input matrix to factorize of size n-by-n
    ParU_C_Symbolic *Sym_C,     // symbolic analysis from ParU_Analyze
    // output:
    ParU_C_Numeric **Num_handle_C,    // output numerical factorization
    // control:
    ParU_C_Control *Control_C
) ;

//------------------------------------------------------------------------------
//--------------------- Solve routines -----------------------------------------
//------------------------------------------------------------------------------

// In all the solve routines Num structure must come with the same Sym struct
// that comes from ParU_Factorize

// x = A\x, where right-hand side is overwritten with the solution x.
ParU_Info ParU_C_Solve_Axx
(
    // input:
    ParU_C_Symbolic *Sym_C, // symbolic analysis from ParU_C_Analyze
    ParU_C_Numeric *Num_C,  // numeric factorization from ParU_C_Factorize
    // input/output:
    double *x,              // vector of size n-by-1; right-hand on input,
                            // solution on output
    // control:
    ParU_C_Control *Control_C
) ;

// x = L\x, where right-hand side is overwritten with the solution x.
ParU_Info ParU_C_Solve_Lxx
(
    // input:
    ParU_C_Symbolic *Sym_C, // symbolic analysis from ParU_C_Analyze
    ParU_C_Numeric *Num_C,  // numeric factorization from ParU_C_Factorize
    // input/output:
    double *x,              // vector of size n-by-1; right-hand on input,
                            // solution on output
    // control:
    ParU_C_Control *Control_C
) ;

// x = U\x, where right-hand side is overwritten with the solution x.
ParU_Info ParU_C_Solve_Uxx
(
    // input:
    ParU_C_Symbolic *Sym_C, // symbolic analysis from ParU_C_Analyze
    ParU_C_Numeric *Num_C,  // numeric factorization from ParU_C_Factorize
    // input/output:
    double *x,              // vector of size n-by-1; right-hand on input,
                            // solution on output
    // control:
    ParU_C_Control *Control_C
) ;

// x = A\b, for vectors x and b
ParU_Info ParU_C_Solve_Axb
(
    // input:
    ParU_C_Symbolic *Sym_C, // symbolic analysis from ParU_C_Analyze
    ParU_C_Numeric *Num_C,  // numeric factorization from ParU_C_Factorize
    double *b,              // vector of size n-by-1
    // output
    double *x,              // vector of size n-by-1
    // control:
    ParU_C_Control *Control_C
) ;

// X = A\X, where right-hand side is overwritten with the solution X.
ParU_Info ParU_C_Solve_AXX
(
    // input
    ParU_C_Symbolic *Sym_C, // symbolic analysis from ParU_C_Analyze
    ParU_C_Numeric *Num_C,  // numeric factorization from ParU_C_Factorize
    int64_t nrhs,
    // input/output:
    double *X,              // array of size n-by-nrhs in column-major storage,
                            // right-hand-side on input, solution on output.
    // control:
    ParU_C_Control *Control_C
) ;

// X = L\X, where right-hand side is overwritten with the solution X.
ParU_Info ParU_C_Solve_LXX
(
    // input
    ParU_C_Symbolic *Sym_C, // symbolic analysis from ParU_C_Analyze
    ParU_C_Numeric *Num_C,  // numeric factorization from ParU_C_Factorize
    int64_t nrhs,
    // input/output:
    double *X,              // array of size n-by-nrhs in column-major storage,
                            // right-hand-side on input, solution on output.
    // control:
    ParU_C_Control *Control_C
) ;

// X = U\X, where right-hand side is overwritten with the solution X.
ParU_Info ParU_C_Solve_UXX
(
    // input
    ParU_C_Symbolic *Sym_C, // symbolic analysis from ParU_C_Analyze
    ParU_C_Numeric *Num_C,  // numeric factorization from ParU_C_Factorize
    int64_t nrhs,
    // input/output:
    double *X,              // array of size n-by-nrhs in column-major storage,
                            // right-hand-side on input, solution on output.
    // control:
    ParU_C_Control *Control_C
) ;

// X = A\B, for matrices X and B
ParU_Info ParU_C_Solve_AXB
(
    // input
    ParU_C_Symbolic *Sym_C, // symbolic analysis from ParU_C_Analyze
    ParU_C_Numeric *Num_C,  // numeric factorization from ParU_C_Factorize
    int64_t nrhs,
    double *B,              // array of size n-by-nrhs in column-major storage
    // output:
    double *X,              // array of size n-by-nrhs in column-major storage
    // control:
    ParU_C_Control *Control_C
) ;

//------------------------------------------------------------------------------
// Perm and InvPerm
//------------------------------------------------------------------------------

// apply permutation to a vector, x=b(p)./s
ParU_Info ParU_C_Perm
(
    // inputs
    const int64_t *P,   // permutation vector of size n
    const double *s,    // vector of size n (optional)
    const double *b,    // vector of size n
    int64_t n,          // length of P, s, B, and X
    // output
    double *x,          // vector of size n
    // control:
    ParU_C_Control *Control_C
) ;

// apply permutation to a matrix, X=B(p,:)./s
ParU_Info ParU_C_Perm_X
(
    // inputs
    const int64_t *P,   // permutation vector of size nrows
    const double *s,    // vector of size nrows (optional)
    const double *B,    // array of size nrows-by-ncols
    int64_t nrows,      // # of rows of X and B
    int64_t ncols,      // # of columns of X and B
    // output
    double *X,          // array of size nrows-by-ncols
    // control:
    ParU_C_Control *Control_C
) ;

// apply inverse permutation to a vector, x(p)=b, then scale x=x./s
ParU_Info ParU_C_InvPerm
(
    // inputs
    const int64_t *P,   // permutation vector of size n
    const double *s,    // vector of size n (optional)
    const double *b,    // vector of size n
    int64_t n,          // length of P, s, B, and X
    // output
    double *x,          // vector of size n
    // control
    ParU_C_Control *Control_C
) ;

// apply inverse permutation to a matrix, X(p,:)=b, then scale X=X./s
ParU_Info ParU_C_InvPerm_X
(
    // inputs
    const int64_t *P,   // permutation vector of size nrows
    const double *s,    // vector of size nrows (optional)
    const double *B,    // array of size nrows-by-ncols
    int64_t nrows,      // # of rows of X and B
    int64_t ncols,      // # of columns of X and B
    // output
    double *X,          // array of size nrows-by-ncols
    // control
    ParU_C_Control *Control_C
) ;

//------------------------------------------------------------------------------
//-------------- computing residual --------------------------------------------
//------------------------------------------------------------------------------

// resid = norm1(b-A*x) / (norm1(A) * norm1 (x))
ParU_Info ParU_C_Residual_bAx
(
    // inputs:
    cholmod_sparse *A,  // an n-by-n sparse matrix
    double *x,          // vector of size n
    double *b,          // vector of size n
    // output:
    double *residc,     // residual: norm1(b-A*x) / (norm1(A) * norm1 (x))
    double *anormc,     // 1-norm of A
    double *xnormc,     // 1-norm of x
    // control:
    ParU_C_Control *Control_C
) ;

// resid = norm1(B-A*X) / (norm1(A) * norm1 (X))
ParU_Info ParU_C_Residual_BAX
(
    // inputs:
    cholmod_sparse *A,  // an n-by-n sparse matrix
    double *X,          // array of size n-by-nrhs
    double *B,          // array of size n-by-nrhs
    int64_t nrhs,
    // output:
    double *residc,     // residual: norm1(B-A*X) / (norm1(A) * norm1 (X))
    double *anormc,     // 1-norm of A
    double *xnormc,     // 1-norm of X
    // control:
    ParU_C_Control *Control_C
) ;

//------------------------------------------------------------------------------
//------------ Free routines----------------------------------------------------
//------------------------------------------------------------------------------

ParU_Info ParU_C_FreeNumeric
(
    ParU_C_Numeric **Num_handle_C,    // numeric object to free
    // control:
    ParU_C_Control *Control_C
) ;

ParU_Info ParU_C_FreeSymbolic
(
    ParU_C_Symbolic **Sym_handle_C,   // symbolic object to free
    // control:
    ParU_C_Control *Control_C
) ;

#ifdef __cplusplus
}
#endif

#endif

