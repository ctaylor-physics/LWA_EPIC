#ifndef OPTION_PARSER
#define OPTION_PARSER
#include <cxxopts.hpp>
#include <string>
#include "constants.h"
#include "host_helpers.h"
using namespace std::string_literals;


cxxopts::Options get_options(){
    cxxopts::Options options("epic++","EPIC dual-pol imager");

    options.add_options("Online data processing")
    ("addr", "F-Engine UDP Stream Address",cxxopts::value<std::string>())
    ("port", "F-Engine UDP Stream Port", cxxopts::value<int>()->default_value("4015"));
    //("utcstart", "F-Engine UDP Stream Start Time")

    options.add_options("Offline data processing")
    ("offline","Load numpy-TBN data from disk",cxxopts::value<bool>()->default_value("true"))
    ("npytbnfile","numpy-TBN data path", cxxopts::value<std::string>());


    options.add_options("Imaging options")
    ("imagesize","1-D image size (can only be 64 or 128)",cxxopts::value<int>()->default_value("128"))
    ("imageres","Pixel resolution in degrees",cxxopts::value<float>()->default_value("1.0"))
    ("nts","Number of timestamps per span",cxxopts::value<int>()->default_value("1000"))
    ("seq_accum","Duration of the sequence accumulation in milliseconds",cxxopts::value<int>()->default_value("40"))
    ("nimg_accum", "Number of images to accumulate before saving to the disk", cxxopts::value<int>()->default_value("1"))
    ("channels","Number of channels in the output image",cxxopts::value<int>()->default_value("128"))
    ("support","Support size of the kernel. Must be a non-zero power of 2",cxxopts::value<int>()->default_value("2"))
    ("aeff","Antenna effective area (experimental) in sq. m",cxxopts::value<float>()->default_value("25"))
    ("kernel_oversample","Factor to over sample the kernel. Must be a power of 2.",cxxopts::value<int>()->default_value("2"))
    ("accum_16bit", "Use 16-bit precision for on-chip memory accumulation. Faster but less precise.", cxxopts::value<bool>()->default_value("false"))
    ("chan_nbin","Binning factor for the number of channels", cxxopts::value<int>()->default_value("4"));

    options.add_options("Execution options")
    ("nstreams","Number of cuda streams to process images", cxxopts::value<int>()->default_value("8"))
    ("ngpus", "number of GPUs to simultaneously run EPIC", cxxopts::value<int>()->default_value("1"));

    options.add_options()
    ("h,help","Print usage");

    return options;

} 


std::optional<std::string> validate_options(cxxopts::ParseResult& result){

    int port = result["port"].as<int>();
    if(port<1 or port>32768){
        return "Invalid port number:  "s+std::to_string(port)+". Port must be a number in 1-32768"s;
    }
    

    int image_size=result["imagesize"].as<int>();
    if(!(image_size==64 || image_size==128)){
        return "Invalid image size: "s+std::to_string(image_size)+". Image size can only be 64 or 128"s;
    }

    int nts = result["nts"].as<int>() * 40e-3;
    int accumulate = result["seq_accum"].as<int>();

    if(accumulate<nts){
        return "Sequence accumulation time must be greater than the gulp size."s;
    }

    int channels = result["channels"].as<int>();
    if(channels<=0){
        return "The number of output channels must be at least 1"s;
    }

    if(channels>MAX_CHANNELS_4090){
        return "RTX 4090 only supports output channels upto "s+std::to_string(MAX_CHANNELS_4090);
    }

    float aeff = result["aeff"].as<float>();
    if(aeff<=0){
        return "Antenna effective cannot be smaller than or equal to zero:  "s+std::to_string(aeff);
    }

    int support = result["support"].as<int>();
    if(support<=0 || support>MAX_ALLOWED_SUPPORT_SIZE){
        return "Invalid support size: "s+std::to_string(support)+". Support can only be between 1-"s+std::to_string(MAX_ALLOWED_SUPPORT_SIZE);
    }

    int kos = result["kernel_oversample"].as<int>();
    if((kos & (kos-1))!=0){
        return "Kernel oversampling factor must be a power of 2";
    }

    int nbin = result["chan_nbin"].as<int>();
    int nchan = result["channels"].as<int>();

    if(nchan%nbin !=0){
        return "Number of channels must be an integral multiple of the binning factor.";
    }

    int nstreams = result["nstreams"].as<int>();

    if(nstreams<=0){
        return "The number of streams must be greater than 0";
    }

    int ndevices = result["ngpus"].as<int>();

    if(ndevices<=0){
        return "ngpus must be greater then 0";
    }

    if(ndevices > get_ngpus()){
        return "Y'all must be kidding. You said EPIC must be run on " + std::to_string(ndevices) + " gpu(s) but only " + std::to_string(get_ngpus()) + " available!";
    }


    return {};
}


#endif /* OPTION_PARSER */
