#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <memory>

#include <boost/python.hpp>

namespace mooncake_conductor {

class BoostPythonPickleSerializer {
private:
    static bool python_initialized;
    
    PyGILState_STATE gil_state;
    
    boost::python::object pickle_module;
    boost::python::object dumps_func;
    
    void initialize_python();
    void cleanup_python() noexcept;
    
    void handle_python_error(const std::string& operation, 
                            const char* file, int line);
    void handle_python_error(const std::string& operation);
    
public:
    BoostPythonPickleSerializer();
    ~BoostPythonPickleSerializer();

    BoostPythonPickleSerializer(const BoostPythonPickleSerializer&) = delete;
    BoostPythonPickleSerializer& operator=(const BoostPythonPickleSerializer&) = delete;

    BoostPythonPickleSerializer(BoostPythonPickleSerializer&& other) noexcept;
    BoostPythonPickleSerializer& operator=(BoostPythonPickleSerializer&& other) noexcept;

    std::vector<uint8_t> serialize(
        const std::vector<uint8_t>& parent_hash,
        const std::vector<int64_t>& token_ids,
        const std::vector<int64_t>* extra_keys = nullptr);

    static boost::python::object bytes_to_python(const std::vector<uint8_t>& data);
    static boost::python::object int64_vector_to_python_tuple(const std::vector<int64_t>& data);
    std::vector<uint8_t> python_to_bytes(const boost::python::object& obj);
};

}