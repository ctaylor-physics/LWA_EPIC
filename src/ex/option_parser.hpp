#include <cxxopts.hpp>
#include <string>
#include "constants.h"
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
    ("accumulate","Duration of the image accumulation in milliseconds",cxxopts::value<int>()->default_value("40"))
    ("channels","Number of channels in the output image",cxxopts::value<int>()->default_value("128"))
    ("support","Support size of the kernel. Must be a non-zero power of 2",cxxopts::value<int>()->default_value("2"))
    ("aeff","Antenna effective area (experimental) in sq. m",cxxopts::value<float>()->default_value("25"));

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
    int accumulate = result["accumulate"].as<int>();

    if(accumulate<nts){
        return "Image accumulation time must be greater than the gulp size."s;
    }

    int channels = result["channels"].as<int>();
    if(channels<=0){
        return "The number of output channels must be at least 1"s;
    }

    if(channels>channels>=MAX_CHANNELS_4090){
        return "RTX 4090 only supports output channels upto "s+std::to_string(MAX_CHANNELS_4090);
    }

    float aeff = result["aeff"].as<float>();
    if(aeff<=0){
        return "Antenna effective cannot be smaller than or equal to zero:  "s+std::to_string(aeff);
    }

    int support = result["support"].as<int>();
    if((support & (support-1))!=0 || support>8){
        return "Invalid support size: "s+std::to_string(support)+". Supported sizes are 2, 4, 8."s;
    }

    return {};
}