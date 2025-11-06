#include "request_handler.h"

#include <unordered_map>
#include <string>
#include <glog/logging.h>


namespace mooncake_conductor {
RequestHandler::RequestHandler(std::string collector, std::string load_collector){
    std::cout << "Finish init Requesthandler!!";
}
std::string RequestHandler::handleRequest(const std::unordered_map<std::string_view, std::string_view>& request){
    return "it is a TEST";
}

}