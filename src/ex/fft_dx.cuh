#ifndef FFTDX_CUH
#define FFTDX_CUH
#include "constants.h"
// #include "data_copier.cuh"
#include "cu_helpers.cuh"
#include "gridder.cuh"
#include "types.hpp"
#include <cooperative_groups.h>
#include <cuda_fp16.h>
#include <cufftdx.hpp>
#include <type_traits>

using namespace cufftdx;
namespace cg = cooperative_groups;

/**
 * @brief Block fft specialization for half precision
 *
 * For half-precision, the data will be stored as a complex<half2>
 * with an RRII (real real img img) layout.
 * This can be exploited to speedup the computation.
 * Here grid elements from two grids are coalesced and represented as a single
 * grid That means each complex type holds two half-precision complex numbers
 * from two images at the same grid point, respectively, and cuFFTDx takes care
 * of doing FFT/IFFT on individual grids. This enables storing data for both the
 * polarizations at a single place allowing fast computation of XX, YY, XY*, X*Y
 * products.
 *
 * @tparam FFT FFT type constructed using cufftdx
 * @tparam Order Packet data ordering type
 * @param f_eng_g Pointer to the F-Engine data in global memory
 * @param antpos_g Pointer to the antenna positions in global memory
 * @param phases_g Pointer to the phases array in global memory
 * @param nseq_per_gulp Number of sequences in each gulp
 * @param nchan Number of channels in each sequence
 * @param gcf_tex GCF texture object
 * @param output_g[out] Pointer to the output image. Must have dimensions of
 *                  \p nseq_per_gulp, \p nchan, \p grid_size, \p grid_size, 2.
 *                  The grid size will be automatically deduced from the FFT
 * object.
 * @param chan_offset Offset to the first channel in the current stream.
 * @param is_first_gulp Flag if the passed data belongs to the first gulp. It
 * determines whether to assign or increment the output image block.
 */
template <
    typename FFT, PKT_DATA_ORDER Order = TIME_MAJOR,
    std::enable_if_t<
        std::is_same<__half2, typename FFT::output_type::value_type>::value,
        bool> = true>
__launch_bounds__(FFT::max_threads_per_block) __global__
    void block_fft_kernel(const uint8_t *f_eng_g, const float *antpos_g,
                          const float *__restrict__ phases_g, int nseq_per_gulp,
                          int nchan, cudaTextureObject_t gcf_tex,
                          float *output_g, int chan_offset = 0,
                          bool is_first_gulp = true) {
  // printf("start\n");
  using complex_type = typename FFT::value_type;
  extern __shared__ complex_type shared_mem[];

  constexpr int support = 1;
  volatile int fft_steps = 2;

  constexpr int stride = size_of<FFT>::value / FFT::elements_per_thread;
  constexpr int row_size =
      size_of<FFT>::value; // blockDim.x * FFT::elements_per_thread;

  complex_type thread_data[FFT::elements_per_thread];
  float stokes_I[FFT::elements_per_thread] = {0};
  unsigned long long int valid_ants[4] = {0};
  int channel_idx = blockIdx.x + chan_offset;

  //   copy antenna positions into shared memory
  float3 *antpos_smem = reinterpret_cast<float3 *>(
      shared_mem + size_of<FFT>::value * size_of<FFT>::value / 2);

  auto *_antpos_g =
      reinterpret_cast<const float3 *>(get_ant_pos(antpos_g, channel_idx));

  auto tb = cg::this_thread_block();
  for (int i = tb.thread_rank(); i < LWA_SV_NSTANDS; i += tb.size()) {
    antpos_smem[i] = _antpos_g[i];
  }
   __syncthreads();

  // find what all antenna kernels overlap the pixels in this thread
  find_valid_antennas_lwasv<FFT, support>(
      reinterpret_cast<const float3 *>(get_ant_pos(antpos_g, channel_idx)),
      valid_ants);

  __syncthreads();

  // loop over each sequence in the gulp for channel_idx channel
  for (int seq_no = 0; seq_no < nseq_per_gulp; ++seq_no) {
    // volatile int idx = 200;
    for (int _reg = 0; _reg < FFT::elements_per_thread; ++_reg) {
      thread_data[_reg] = __half2half2(0);
    }

    grid_dual_pol_dx7<FFT, support, LWA_SV_NSTANDS>(
        thread_data,
        reinterpret_cast<const cnib2 *>(get_f_eng_sample<Order>(
            f_eng_g, seq_no, channel_idx, nseq_per_gulp, nchan)),
        // reinterpret_cast<const float3 *>(get_ant_pos(antpos_g,channel_idx)),
        antpos_smem,
        reinterpret_cast<const float4 *>(get_phases(phases_g, channel_idx)),
        gcf_tex, valid_ants);

     __syncthreads();

    // Execute IFFT (row-wise)
    for (int step = 0; step < fft_steps; ++step) {
      FFT().execute(thread_data, shared_mem /*, workspace*/);
      if (step == 0) {
        ///////////////////////////////////////
        // To complete the IFFT, transpose the grid and IFFT on it, which is
        //  equivalent to a column-wise IFFT.
        // Load everything into shared memory and normalize
        // this ensures there is no overflow
        transpose_tri<FFT>(thread_data, shared_mem,
                           /*_norm=*/half(4.) / half(row_size * row_size));
      }
       __syncthreads();
    }

    for (int _reg = 0; _reg < FFT::elements_per_thread; ++_reg) {
      auto xx_yy = thread_data[_reg].x * thread_data[_reg].x +
                   thread_data[_reg].y * thread_data[_reg].y;
      stokes_I[_reg] += float(xx_yy.x + xx_yy.y);
    }
    __syncthreads();
  }

  for (int _reg = 0; _reg < FFT::elements_per_thread; ++_reg) {
    auto index = (threadIdx.x + _reg * stride) + threadIdx.y * row_size;
    output_g[channel_idx * row_size * row_size + index] = stokes_I[_reg];
  }
}

using FFT64x64 =
    decltype(Size<64>() + Precision<half>() + Type<fft_type::c2c>() +
             Direction<fft_direction::inverse>() + SM<890>() +
             ElementsPerThread<4>() + FFTsPerBlock<128>() + Block());

using FFT128x128 =
    decltype(Size<128>() + Precision<half>() + Type<fft_type::c2c>() +
             Direction<fft_direction::inverse>() + SM<890>() +
             ElementsPerThread<16>() + FFTsPerBlock<256>() + Block());

using FFT81x81 =
    decltype(Size<81>() + Precision<half>() + Type<fft_type::c2c>() +
             Direction<fft_direction::inverse>() + SM<890>() +
              ElementsPerThread<9>() +FFTsPerBlock<162>() + Block());

using FFT100x100 =
    decltype(Size<100>() + Precision<half>() + Type<fft_type::c2c>() +
             Direction<fft_direction::inverse>() + SM<890>() +
             ElementsPerThread<10>() + FFTsPerBlock<200>() + Block());
#endif // FFTDX_CUH
