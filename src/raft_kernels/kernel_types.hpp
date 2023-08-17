#ifndef KERNEL_TYPES
#define KERNEL_TYPES
#include "../ex/MOFF_correlator.hpp"
#include "../ex/option_parser.hpp"
#include <cmath>

#include "../ex/buffer.hpp"
#include "../ex/helper_traits.hpp"
#include "../ex/lf_buf_mngr.hpp"
#include "accumulator.cpp"
#include "chan_reducer.cpp"
#include "correlator.hpp"
#include "db_ingester.cpp"
#include "disk_saver.hpp"
// #include "dummy_kernel.hpp"
#include "dummy_packet_gen.hpp"
#include "index_fetcher.hpp"
#include "pixel_extractor.cpp"
#include <raftmanip>

enum EPICKernelID
{
    _PACK_GEN = 0,
    _DUMMY_PACK_GEN = 1,
    _CORRELATOR = 2,
    _CHAN_REDUCER = 3,
    _PIX_EXTRACTOR = 4,
    _IDX_FETCHER = 5,
    _DB_INGESTER = 6,
    _ACCUMULATOR = 7,
    _DISK_SAVER = 8
};

struct KernelTypeDefs
{
    using lbuf_mngr_u8_t = LFBufMngr<AlignedBuffer<uint8_t>>;
    using lbuf_mngr_float_t = LFBufMngr<AlignedBuffer<float>>;
    using moffcorr_t = MOFFCorrelator<uint8_t, lbuf_mngr_float_t>;
    using mbuf_u8_t = typename lbuf_mngr_u8_t::mbuf_t;
    using mbuf_float_t = typename lbuf_mngr_float_t::mbuf_t;
    using payload_u8_t = Payload<mbuf_u8_t>;
    using payload_float_t = Payload<mbuf_float_t>;
    using pixel_buf_t = LFBufMngr<EpicPixelTableDataRows<float>>;
    using pixel_buf_config_t = typename EpicPixelTableDataRows<float>::config_t;
    using pixel_pld_t = Payload<typename pixel_buf_t::mbuf_t>;
    using opt_t = cxxopts::ParseResult;
};

template<EPICKernelID _kID>
struct Kernel : KernelTypeDefs
{
    static_assert("Invalid EPIC kernel specified");
    using ktype = void;
    template<unsigned int _GpuId>
    ktype get_kernel();
};

template<>
struct Kernel<_DUMMY_PACK_GEN> : KernelTypeDefs
{
    using ktype = dummy_pkt_gen<payload_u8_t, LFBufMngr<AlignedBuffer<uint8_t>>>;
    template<unsigned int _GpuId>
    static ktype get_kernel(opt_t&)
    {
        return ktype(200);
    }
};
using DummyPktGen_kt = Kernel<_DUMMY_PACK_GEN>::ktype;
template<unsigned int _GpuId>
auto& get_dummy_pkt_gen_k = Kernel<_DUMMY_PACK_GEN>::get_kernel<_GpuId>;

template<>
struct Kernel<_CORRELATOR> : KernelTypeDefs
{
    using ktype = Correlator_rft<payload_u8_t, moffcorr_t>;
    template<unsigned int _GpuId>
    static ktype get_kernel(opt_t& options)
    {
        auto correlator_options = MOFFCorrelatorDesc();
        correlator_options.device_id = _GpuId;
        correlator_options.accum_time_ms = options["seq_accum"].as<int>();
        correlator_options.nseq_per_gulp = options["nts"].as<int>();
        correlator_options.nchan_out = options["channels"].as<int>();
        if (options["imagesize"].as<int>() == 64) {
            correlator_options.img_size = HALF; // defaults to full
        }
        correlator_options.grid_res_deg = options["imageres"].as<float>();
        correlator_options.support_size = options["support"].as<int>();
        correlator_options.gcf_kernel_dim = std::sqrt(options["aeff"].as<float>()) * 10; // radius of the kernel in decimeters
        correlator_options.kernel_oversampling_factor = options["kernel_oversample"].as<int>();
        correlator_options.use_bf16_accum = options["accum_16bit"].as<bool>();
        correlator_options.nstreams = options["nstreams"].as<int>();

        auto corr_ptr = std::make_unique<MOFFCorrelator_t>(correlator_options);
        return ktype(corr_ptr);
    }
};
using EPICCorrelator_kt = Kernel<_CORRELATOR>::ktype;
template<unsigned int _GpuId>
auto& get_epiccorr_k = Kernel<_CORRELATOR>::get_kernel<_GpuId>;

template<>
struct Kernel<_CHAN_REDUCER> : KernelTypeDefs
{
    using ktype = ChanReducer_rft<payload_float_t, lbuf_mngr_float_t>;
    template<unsigned int _GpuId>
    static ktype get_kernel(opt_t& options)
    {
        int imsize = options["imagesize"].as<int>();
        int im_nchan = options["channels"].as<int>();
        int chan_nbin = options["chan_nbin"].as<int>();
        return ktype(
          chan_nbin, imsize, imsize, im_nchan);
    }
};
using ChanReducer_kt = Kernel<_CHAN_REDUCER>::ktype;
template<unsigned int _GpuId>
auto& get_chan_reducer_k = Kernel<_CHAN_REDUCER>::get_kernel<_GpuId>;

template<>
struct Kernel<_PIX_EXTRACTOR> : KernelTypeDefs
{
    using ktype = PixelExtractor<payload_float_t, pixel_pld_t, pixel_buf_t, pixel_buf_config_t>;
    template<unsigned int _GpuId>
    static ktype get_kernel(opt_t& options)
    {
        KernelTypeDefs::pixel_buf_config_t config;
        int imsize = options["imagesize"].as<int>();
        int im_nchan = options["channels"].as<int>();
        int chan_nbin = options["chan_nbin"].as<int>();
        int reduced_nchan = im_nchan / chan_nbin;
        config.nchan = reduced_nchan;
        config.ncoords = 1;
        config.nsrcs = 1;

        // fetch intial pixel indices;
        auto dummy_meta = create_dummy_meta(imsize, imsize);
        return ktype(config, dummy_meta, imsize, imsize, reduced_nchan);
    }
};
using PixelExtractor_kt = Kernel<_PIX_EXTRACTOR>::ktype;
template<unsigned int _GpuId>
auto& get_pixel_extractor_k = Kernel<_PIX_EXTRACTOR>::get_kernel<_GpuId>;

template<>
struct Kernel<_IDX_FETCHER> : KernelTypeDefs
{
    using ktype = IndexFetcher_rft;
    template<unsigned int _GpuId>
    static ktype get_kernel(opt_t& )
    {
        return ktype();
    }
};
using IndexFetcher_kt = Kernel<_IDX_FETCHER>::ktype;
template<unsigned int _GpuId>
auto& get_index_fetcher_k = Kernel<_IDX_FETCHER>::get_kernel<_GpuId>;

template<>
struct Kernel<_DB_INGESTER> : KernelTypeDefs
{
    using ktype = DBIngester_rft<pixel_pld_t>;
    template<unsigned int _GpuId>
    static ktype get_kernel(opt_t& )
    {
        return ktype();
    }
};
using DBIngester_kt = Kernel<_DB_INGESTER>::ktype;
template<unsigned int _GpuId>
auto& get_db_ingester_k = Kernel<_DB_INGESTER>::get_kernel<_GpuId>;

template<>
struct Kernel<_ACCUMULATOR> : KernelTypeDefs
{
    using ktype = Accumulator_rft<payload_float_t>;
    template<unsigned int _GpuId>
    static ktype get_kernel(opt_t& options)
    {
        int imsize = options["imagesize"].as<int>();
        int im_nchan = options["channels"].as<int>();
        int chan_nbin = options["chan_nbin"].as<int>();
        int reduced_nchan = im_nchan / chan_nbin;
        int im_naccum = options["nimg_accum"].as<int>();

        return ktype(
          imsize, imsize, reduced_nchan, im_naccum);
    }
};
using Accumulator_kt = Kernel<_ACCUMULATOR>::ktype;
template<unsigned int _GpuId>
auto& get_accumulator_k = Kernel<_ACCUMULATOR>::get_kernel<_GpuId>;

template<>
struct Kernel<_DISK_SAVER> : KernelTypeDefs
{
    using ktype = DiskSaver_rft<payload_float_t>;
    template<unsigned int _GpuId>
    static ktype get_kernel(opt_t& )
    {
        return ktype();
    }
};
using DiskSaver_kt = Kernel<_DISK_SAVER>::ktype;
template<unsigned int _GpuId>
auto& get_disk_saver_k = Kernel<_DISK_SAVER>::get_kernel<_GpuId>;

#endif /* KERNEL_TYPES */
