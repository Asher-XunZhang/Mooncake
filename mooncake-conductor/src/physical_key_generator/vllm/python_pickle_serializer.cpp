#include "python_pickle_serializer.h"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace mooncake_conductor {

class GilGuard {
private:
    PyGILState_STATE state;
    
public:
    GilGuard() : state(PyGILState_Ensure()) {}
    ~GilGuard() { PyGILState_Release(state); }

    GilGuard(const GilGuard&) = delete;
    GilGuard& operator=(const GilGuard&) = delete;
};

std::atomic<bool> BoostPythonPickleSerializer::python_initialized{false};
std::mutex BoostPythonPickleSerializer::init_mutex;

void BoostPythonPickleSerializer::handle_python_error(
    const std::string& operation, const char* file, int line) {
    
    namespace py = boost::python;
    
    try {
        if (PyErr_Occurred()) {
            py::object sys_module = py::import("sys");
            py::object exc_info = sys_module.attr("exc_info")();
            py::object exc_type = exc_info[0];
            py::object exc_value = exc_info[1];
            
            std::string exc_type_str = py::extract<std::string>(
                py::str(exc_type.attr("__name__")));
            std::string exc_value_str = py::extract<std::string>(py::str(exc_value));
            
            std::ostringstream oss;
            oss << "Python error in '" << operation << "' at " << file << ":" << line 
                << " - " << exc_type_str << ": " << exc_value_str;
            throw std::runtime_error(oss.str());
        }
    } catch (const py::error_already_set&) {
        std::ostringstream oss;
        oss << "Python operation '" << operation << "' failed with unreadable error";
        throw std::runtime_error(oss.str());
    }
}

void BoostPythonPickleSerializer::handle_python_error(const std::string& operation) {
    handle_python_error(operation, __FILE__, __LINE__);
}

BoostPythonPickleSerializer::BoostPythonPickleSerializer() {
    initialize_python();
}

BoostPythonPickleSerializer::~BoostPythonPickleSerializer() {
    cleanup_python();
}

BoostPythonPickleSerializer::BoostPythonPickleSerializer(
    BoostPythonPickleSerializer&& other) noexcept
    : gil_state(other.gil_state)
    , pickle_module(std::move(other.pickle_module))
    , dumps_func(std::move(other.dumps_func)) {
    
    other.pickle_module = boost::python::object();
    other.dumps_func = boost::python::object();
    other.gil_state = static_cast<PyGILState_STATE>(-1);
}

BoostPythonPickleSerializer& BoostPythonPickleSerializer::operator=(
    BoostPythonPickleSerializer&& other) noexcept {
    
    if (this != &other) {
        cleanup_python();
        gil_state = other.gil_state;
        pickle_module = std::move(other.pickle_module);
        dumps_func = std::move(other.dumps_func);
        
        other.pickle_module = boost::python::object();
        other.dumps_func = boost::python::object();
        other.gil_state = static_cast<PyGILState_STATE>(-1);
    }
    return *this;
}

void BoostPythonPickleSerializer::initialize_python() {
    {
        std::lock_guard<std::mutex> lock(init_mutex);
        if (python_initialized.load()) {
            gil_state = PyGILState_Ensure();
            return;
        }
        
        try {
            if (!Py_IsInitialized()) {
                Py_Initialize();
                if (!Py_IsInitialized()) {
                    throw std::runtime_error("Failed to initialize Python interpreter");
                }
            }
            
            #if PY_VERSION_HEX < 0x03070000
                PyEval_InitThreads();
            #endif
            
            boost::python::object main_module = boost::python::import("__main__");
            
            pickle_module = boost::python::import("pickle");
            dumps_func = pickle_module.attr("dumps");
            
            python_initialized.store(true);
            std::cout << "Python interpreter and Boost.Python initialized successfully\n";
            
        } catch (const boost::python::error_already_set&) {
            handle_python_error("initialize_python");
        }
    }
    
    gil_state = PyGILState_Ensure();
}

void BoostPythonPickleSerializer::cleanup_python() noexcept {
    try {
        if (Py_IsInitialized() && 
            gil_state != static_cast<PyGILState_STATE>(-1)) {
            PyGILState_Release(gil_state);
            gil_state = static_cast<PyGILState_STATE>(-1);
        }
    } catch (...) {
    }
}

std::vector<uint8_t> BoostPythonPickleSerializer::serialize(
    const std::vector<uint8_t>& parent_hash,
    const std::vector<int64_t>& token_ids,
    std::optional<std::vector<int64_t>> extra_keys) {
    
    GilGuard gil_guard;
    
    try {
        boost::python::object py_parent_hash = bytes_to_python(parent_hash);
        boost::python::object py_token_ids = int64_vector_to_python_tuple(token_ids);
        
        boost::python::object py_extra_keys = extra_keys ? 
            int64_vector_to_python_tuple(*extra_keys) : 
            boost::python::object();
        
        boost::python::object input_tuple = boost::python::make_tuple(
            py_parent_hash, py_token_ids, py_extra_keys);
        
        boost::python::object serialized_obj = dumps_func(input_tuple, 5);
        
        return python_to_bytes(serialized_obj);
        
    } catch (const boost::python::error_already_set&) {
        handle_python_error("pickle.dumps");
        return {};
    }
}

boost::python::object BoostPythonPickleSerializer::bytes_to_python(
    const std::vector<uint8_t>& data) {
    PyObject* py_bytes = PyBytes_FromStringAndSize(
        reinterpret_cast<const char*>(data.data()), 
        static_cast<Py_ssize_t>(data.size())
    );
    
    if (!py_bytes) {
        handle_python_error("PyBytes_FromStringAndSize");
    }
    
    return boost::python::object(boost::python::handle<>(py_bytes));
}

boost::python::object BoostPythonPickleSerializer::int64_vector_to_python_tuple(
    const std::vector<int64_t>& data) {
    
    boost::python::list py_list;
    for (auto value : data) {
        py_list.append(value);
    }
    
    return boost::python::tuple(py_list);
}

std::vector<uint8_t> BoostPythonPickleSerializer::python_to_bytes(
    const boost::python::object& obj) {
    boost::python::object local_copy(obj); 
    
    PyObject* py_obj = local_copy.ptr();
    
    if (!PyBytes_Check(py_obj)) {
        throw std::runtime_error("Object is not a bytes type");
    }

    Py_ssize_t length = PyBytes_Size(py_obj);
    const char* buffer = PyBytes_AsString(py_obj);
    
    if (!buffer) {
        handle_python_error("PyBytes_AsString failed");
    }
    
    return std::vector<uint8_t>(
        reinterpret_cast<const uint8_t*>(buffer),
        reinterpret_cast<const uint8_t*>(buffer) + length
    );
}

}