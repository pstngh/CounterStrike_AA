#ifndef HK_VECTOR_FPU_INCLUDED
#define HK_VECTOR_FPU_INCLUDED

#include <cstdint>

#define hk_VecFPU_SIZE_DOUBLE 2
#define hk_VecFPU_MEM_MASK_DOUBLE (~uintptr_t(0xf)) //16Byte per Block
#define hk_VecFPU_MASK_DOUBLE 0xfffffffe
#define hk_VecFPU_MEMSHIFT_DOUBLE 3 //8 Bytes per Floating Point Number

#define hk_VecFPU_SIZE_FLOAT 4
#define hk_VecFPU_MEM_MASK_FLOAT (~uintptr_t(0xf)) //16Byte per Block
#define hk_VecFPU_MASK_FLOAT 0xfffffffc
#define hk_VecFPU_MEMSHIFT_FLOAT 2 //4 Bytes per Floating Point Number



// assumption: the memory is always allocated aligned to the block size
//             otherwise -> failure
// hk_BOOL adress_aligned is meant to indicate wether array is starting at aligned adress. If not,
// extra work has to be done
class hk_VecFPU {
public:
    static inline void fpu_add_multiple_row(hk_real *target_adress,hk_real *source_adress,hk_real factor,int size,hk_bool adress_aligned);
    static inline void fpu_multiply_row(hk_real *target_adress,hk_real factor,int size,hk_bool adress_aligned);
    static inline void fpu_exchange_rows(hk_real *target_adress1,hk_real *target_adress2,int size,hk_bool adress_aligned);
    static inline void fpu_copy_rows(hk_real *target_adress,hk_real *source_adress,int size,hk_bool adress_aligned);
    static inline void fpu_set_row_to_zero(hk_real *target_adress,int size,hk_bool adress_aligned);
    static inline hk_real fpu_large_dot_product(hk_real *base_a,hk_real *x,int size,hk_bool adress_aligned);
    static inline int calc_aligned_row_len(int unaligned_len,hk_real *dummy_type);


    static inline void fpu_add_multiple_row(hk_double *target_adress,hk_double *source_adress,hk_double factor,int size,hk_bool adress_aligned);
    static inline void fpu_multiply_row(hk_double *target_adress,hk_double factor,int size,hk_bool adress_aligned);
    static inline void fpu_exchange_rows(hk_double *target_adress1,hk_double *target_adress2,int size,hk_bool adress_aligned);
    static inline void fpu_copy_rows(hk_double *target_adress,hk_double *source_adress,int size,hk_bool adress_aligned);
    static inline void fpu_set_row_to_zero(hk_double *target_adress,int size,hk_bool adress_aligned);
    static inline hk_double fpu_large_dot_product(hk_double *base_a,hk_double *x,int size,hk_bool adress_aligned);
    static inline int calc_aligned_row_len(int unaligned_len,hk_double *dummy_type);
};

#include <hk_math/vector_fpu/vector_fpu.inl>

#endif //HK_VECTOR_FPU_INCLUDED
