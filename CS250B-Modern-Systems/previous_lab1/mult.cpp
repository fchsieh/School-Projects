#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

extern "C" {
#include <immintrin.h>
}

void mult_original(float* a, float* b, float*c, int matrix_size, int thread_count) {
	for ( int i = 0; i < matrix_size; i++ ) {
		for ( int j = 0; j < matrix_size; j++ ) {
			c[i*matrix_size+j] = 0;
			for ( int k = 0; k < matrix_size; k++ ) {
				c[i*matrix_size+j] += a[i*matrix_size+k]*b[k*matrix_size+j];
			}
		}
	}
}

void mult_transpose(float* a, float* b, float*c, int matrix_size, int thread_count) {
	float* bt = (float*)malloc(sizeof(float)*matrix_size*matrix_size);
	for ( int i = 0; i < matrix_size; i++ ) {
		for ( int j = 0; j < matrix_size; j++ ) {
			bt[i*matrix_size+j] = b[j*matrix_size+i];
		}
	}

	for ( int i = 0; i < matrix_size; i++ ) {
		for ( int j = 0; j < matrix_size; j++ ) {
			c[i*matrix_size+j] = 0;
			for ( int k = 0; k < matrix_size; k++ ) {
				c[i*matrix_size+j] += a[i*matrix_size+k]*bt[j*matrix_size+k];
			}
		}
	}
	free(bt);
}

void mult_avx(float* a, float* b, float*c, int matrix_size, int thread_count) {
	float* bt = (float*)malloc(sizeof(float)*matrix_size*matrix_size);
	for ( int i = 0; i < matrix_size; i++ ) {
		for ( int j = 0; j < matrix_size; j++ ) {
			bt[i*matrix_size+j] = b[j*matrix_size+i];
		}
	}

	for ( int i = 0; i < matrix_size; i++ ) {
		for ( int j = 0; j < matrix_size; j++ ) {
			__m256 z = _mm256_set1_ps(0);
			for ( int k = 0; k < matrix_size/8; k++ ) {
				__m256 x = _mm256_loadu_ps(&a[i*matrix_size+k*8]);
				__m256 y = _mm256_loadu_ps(&bt[j*matrix_size+k*8]);
				z = _mm256_fmadd_ps(x,y,z);
			}
			c[i*matrix_size+j] = 0;
			for (int k = 0; k < 8; k++ ) {
				c[i*matrix_size+j] += ((float*)&z)[k];
			}

		}
	}

	free (bt);
}

void blocked_mult(float* a, float* b, float* c, int matrix_size, int block_size) {
	float* bt = (float*)malloc(sizeof(float)*matrix_size*matrix_size);
	for ( int i = 0; i < matrix_size; i++ ) {
		for ( int j = 0; j < matrix_size; j++ ) {
			bt[i*matrix_size+j] = b[j*matrix_size+i];
			c[i*matrix_size+j] = 0;
		}
	}
	int block_count = matrix_size/block_size;
	for ( int i = 0; i < block_count; i++ ) {
		for ( int j = 0; j < block_count; j++ ) {
			for ( int k = 0; k < block_count; k++ ) {
				for ( int ii = 0; ii < block_size; ii++ ) {
					for ( int jj = 0; jj < block_size; jj++ ) {
						for ( int kk = 0; kk < block_size; kk++ ) {
							int aidx = (i*block_size+ii)*matrix_size + (k*block_size+kk);
							int bidx = (j*block_size+jj)*matrix_size + (k*block_size+kk);
							int cidx = (i*block_size+ii)*matrix_size + (j*block_size+jj);
							c[cidx] += a[aidx]*bt[bidx];
						}
					}
				}
			}
		}
	}
	printf("%d %d\n", matrix_size, block_size);
	free(bt);
}

void block_mult(float* a, float* bt, float* ct, int matrix_size, int block_size, int i, int j, int k) {
	for ( int ii = 0; ii < block_size; ii++ ) {
		for ( int jj = 0; jj < block_size; jj++ ) {
			int cidx = ((i*block_size+ii)*matrix_size + (j*block_size+jj))*8;
			__m256 z = _mm256_loadu_ps(&ct[cidx]);

			for ( int kk = 0; kk < block_size/8; kk++ ) {
				int aidx = (i*block_size+ii)*matrix_size + (k*block_size+kk*8);
				int bidx = (j*block_size+jj)*matrix_size + (k*block_size+kk*8);
				__m256 x = _mm256_loadu_ps(&a[aidx]);
				__m256 y = _mm256_loadu_ps(&bt[bidx]);
				z = _mm256_fmadd_ps(x,y,z);
			}
			_mm256_storeu_ps(&ct[cidx], z);
		}
	}
}

void blocked_mult_avx_d(float* a, float* b, float* c, int matrix_size, int block_size) {
	float* bt = (float*)malloc(sizeof(float)*matrix_size*matrix_size);
	float* ct = (float*)malloc(sizeof(float)*matrix_size*matrix_size*8);

	for ( int i = 0; i < matrix_size; i++ ) {
		for ( int j = 0; j < matrix_size; j++ ) {
			bt[i*matrix_size+j] = b[j*matrix_size+i];
		}
	}

	int block_count = matrix_size/block_size;
	for ( int i = 0; i < block_count; i++ ) {
		for ( int j = 0; j < block_count; j++ ) {
			for ( int ii = 0; ii < block_size; ii++ ) {
				for ( int jj = 0; jj < block_size; jj++ ) {
					int cidx = ((i*block_size+ii)*matrix_size + (j*block_size+jj))*8;
					for ( int kk = 0; kk < 8; kk++ ) {
						ct[cidx+kk] = 0;
					}
				}
			}
			for ( int k = 0; k < block_count; k++ ) {
				block_mult(a,bt,ct, matrix_size, block_size, i, j, k);
			}
		}
	}
	
	for ( int i = 0; i < matrix_size; i++ ) {
		for ( int j = 0; j < matrix_size; j++ ) {
			c[i*matrix_size+j] = 0;
			for ( int k = 0; k < 8; k++ ) {
				c[i*matrix_size+j] += ct[(i*matrix_size+j)*8+k];
			}
		}
	}


	free (bt);
	free (ct);
	
}

struct mult_sub_thread_arg {
	float* a;
	float* bt;
	float* ct;
	int matrix_size;
	int block_size;
	int thread_count;
	int thread_idx;

	pthread_t tid;
};
static void * mult_sub_thread(void* arg_) {
	struct mult_sub_thread_arg* arg = (struct mult_sub_thread_arg*)arg_;
	float* a = arg->a;
	float* bt = arg->bt;
	float* ct = arg->ct;
	int block_size = arg->block_size;
	int matrix_size = arg->matrix_size;

	int block_count = arg->matrix_size/arg->block_size;
	for ( int i = arg->thread_idx; i < block_count; i+=arg->thread_count ) {
		for ( int j = 0; j < block_count; j++ ) {
			for ( int ii = 0; ii < block_size; ii++ ) {
				for ( int jj = 0; jj < block_size; jj++ ) {
					int cidx = ((i*block_size+ii)*matrix_size + (j*block_size+jj))*8;
					for ( int kk = 0; kk < 8; kk++ ) {
						ct[cidx+kk] = 0;
					}
				}
			}
			
			for ( int k = 0; k < block_count; k++ ) {
				block_mult(a,bt,ct,arg->matrix_size,arg->block_size,i,j,k);
			}
		}
	}

	return NULL;
}



void blocked_mult_avx(float* a, float* b, float* c, int matrix_size, int block_size) {
	float* bt = (float*)malloc(sizeof(float)*matrix_size*matrix_size);
	float* ct = (float*)malloc(sizeof(float)*matrix_size*matrix_size*8);
	for ( int i = 0; i < matrix_size; i++ ) {
		for ( int j = 0; j < matrix_size; j++ ) {
			bt[i*matrix_size+j] = b[j*matrix_size+i];
		}
	}

	int block_count = matrix_size/block_size;
	for ( int i = 0; i < block_count; i++ ) {
		for ( int j = 0; j < block_count; j++ ) {
			for ( int ii = 0; ii < block_size; ii++ ) {
				for ( int jj = 0; jj < block_size; jj++ ) {
					int cidx = ((i*block_size+ii)*matrix_size + (j*block_size+jj))*8;
					for ( int kk = 0; kk < 8; kk++ ) {
						ct[cidx+kk] = 0;
					}
				}
			}
			for ( int k = 0; k < block_count; k++ ) {
				for ( int ii = 0; ii < block_size; ii++ ) {
					for ( int jj = 0; jj < block_size; jj++ ) {
						int cidx = ((i*block_size+ii)*matrix_size + (j*block_size+jj))*8;

						//__m256 z = _mm256_set1_ps(0);
						__m256 z = _mm256_loadu_ps(&ct[cidx]);
						for ( int kk = 0; kk < block_size/8; kk++ ) {
							int aidx = (i*block_size+ii)*matrix_size + (k*block_size+kk*8);
							int bidx = (j*block_size+jj)*matrix_size + (k*block_size+kk*8);
							__m256 x = _mm256_loadu_ps(&a[aidx]);
							__m256 y = _mm256_loadu_ps(&bt[bidx]);
							z = _mm256_fmadd_ps(x,y,z);
						}
						_mm256_storeu_ps(&ct[cidx], z);
					}
				}
			}
		}
	}

	for ( int i = 0; i < matrix_size; i++ ) {
		for ( int j = 0; j < matrix_size; j++ ) {
			c[i*matrix_size+j] = 0;
			for ( int k = 0; k < 8; k++ ) {
				c[i*matrix_size+j] += ct[(i*matrix_size+j)*8+k];
			}
		}
	}


	free (bt);
	free(ct);
	
}

void blocked_mult_avx_thread(float* a, float* b, float* c, int matrix_size, int block_size, int thread_count) {
	float* bt = (float*)malloc(sizeof(float)*matrix_size*matrix_size);
	float* ct = (float*)malloc(sizeof(float)*matrix_size*matrix_size*8);
	struct mult_sub_thread_arg* thread_args = (struct mult_sub_thread_arg*)calloc(thread_count, sizeof(struct mult_sub_thread_arg));


	for ( int i = 0; i < matrix_size; i++ ) {
		for ( int j = 0; j < matrix_size; j++ ) {
			bt[i*matrix_size+j] = b[j*matrix_size+i];
		}
	}

	for ( int i = 0; i < thread_count; i++ ) {
		thread_args[i].a = a;
		thread_args[i].bt = bt;
		thread_args[i].ct = ct;
		thread_args[i].matrix_size = matrix_size;
		thread_args[i].block_size = block_size;
		thread_args[i].thread_count = thread_count;
		thread_args[i].thread_idx = i;


	}
	for ( int i = 0; i < thread_count; i++ ) {
		pthread_create(&thread_args[i].tid, NULL, mult_sub_thread, &thread_args[i]);
	}
	for ( int i = 0; i < thread_count; i++ ) {
		pthread_join(thread_args[i].tid, NULL);
	}

	
	for ( int i = 0; i < matrix_size; i++ ) {
		for ( int j = 0; j < matrix_size; j++ ) {
			c[i*matrix_size+j] = 0;
			for ( int k = 0; k < 8; k++ ) {
				c[i*matrix_size+j] += ct[(i*matrix_size+j)*8+k];
			}
		}
	}


	free (bt);
	free (ct);
	
}



void mult_sub(float* a, int axoff, int ayoff, float* b, int bxoff, int byoff, float* c, int cxoff, int cyoff, int size, int totalsize) {
	// assuming size is multiples of 8

	for ( int i = 0; i < size; i++ ) {
		for ( int j = 0; j < size; j++ ) {
			__m256 z = _mm256_set1_ps(0);
			for ( int k = 0; k < size/8; k++ ) {
				__m256 x = _mm256_loadu_ps(&a[(i+axoff)*totalsize+(k+ayoff)*8]);
				__m256 y = _mm256_loadu_ps(&b[(j+bxoff)*totalsize+(k+byoff)*8]);
				z = _mm256_fmadd_ps(x,y,z);
			}
			//c[(i+cxoff)*totalsize+(j+cyoff)] = 0;//sub-addition doesn't initialize
			for (int k = 0; k < 8; k++ ) {
				c[(i+cxoff)*totalsize+(j+cyoff)] += ((float*)&z)[k];
			}
		}
	}
}

void mult_sub_recursive(float* a, int axoff, int ayoff, float* b, int bxoff, int byoff, float* c, int cxoff, int cyoff, int size, int totalsize) {
	if ( size <= 16 ) {
		mult_sub(a,axoff,ayoff,b,bxoff,byoff,c,cxoff,cyoff,size,totalsize);
		return;
	}
	int subsize = size/2;

	mult_sub(a,axoff,ayoff,
		b,bxoff,byoff,
		c,cxoff,cyoff,
		subsize,totalsize);
	mult_sub(a,axoff,ayoff+subsize,
		b,bxoff,byoff+subsize,
		c,cxoff,cyoff,
		subsize,totalsize);
	
	mult_sub(a,axoff,ayoff,
		b,bxoff+subsize,byoff,
		c,cxoff,cyoff+subsize,
		subsize,totalsize);
	mult_sub(a,axoff,ayoff+subsize,
		b,bxoff+subsize,byoff+subsize,
		c,cxoff,cyoff+subsize,
		subsize,totalsize);

	mult_sub(a,axoff+subsize,ayoff,
		b,bxoff,byoff,
		c,cxoff+subsize,cyoff,
		subsize,totalsize);
	mult_sub(a,axoff+subsize,ayoff+subsize,
		b,bxoff,byoff+subsize,
		c,cxoff+subsize,cyoff,
		subsize,totalsize);
	
	mult_sub(a,axoff+subsize,ayoff,
		b,bxoff+subsize,byoff,
		c,cxoff+subsize,cyoff+subsize,
		subsize,totalsize);
	mult_sub(a,axoff+subsize,ayoff+subsize,
		b,bxoff+subsize,byoff+subsize,
		c,cxoff+subsize,cyoff+subsize,
		subsize,totalsize);
}

void mult_rec(float* a, float* b, float*c, int matrix_size, int thread_count) {
	float* bt = (float*)malloc(sizeof(float)*matrix_size*matrix_size);
	for ( int i = 0; i < matrix_size; i++ ) {
		for ( int j = 0; j < matrix_size; j++ ) {
			bt[i*matrix_size+j] = b[j*matrix_size+i];
			c[i*matrix_size+j] = 0;
		}
	}

	mult_sub_recursive(a,0,0, bt,0,0, c,0,0, matrix_size, matrix_size);

	free (bt);
}

void mult(float* a, float* b, float*c, int matrix_size, int thread_count) {
	//mult_transpose(a,b,c,matrix_size,thread_count);
	//blocked_mult(a,b,c,matrix_size,16);
	//mult_avx(a,b,c,matrix_size,thread_count);
	blocked_mult_avx(a,b,c,matrix_size,16);
	//blocked_mult_avx_d(a,b,c,matrix_size,64);
	//blocked_mult_avx_thread(a,b,c,matrix_size, 64, thread_count);
}
