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

bool BoostPythonPickleSerializer::python_initialized = false;

void BoostPythonPickleSerializer::handle_python_error(
    const std::string& operation, const char* file, int line) {
    
    namespace py = boost::python;
    
    try {
        if (PyErr_Occurred()) {
            PyErr_Print();
            
            py::object sys_module = py::import("sys");
            py::object exc_info = sys_module.attr("exc_info")();
            py::object exc_type = exc_info[0];
            py::object exc_value = exc_info[1];
            py::object exc_traceback = exc_info[2];
            
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
    }
    return *this;
}

void BoostPythonPickleSerializer::initialize_python() {
    if (!python_initialized) {
        try {

            Py_Initialize();
            if (!Py_IsInitialized()) {
                throw std::runtime_error("Failed to initialize Python interpreter");
            }
            
            PyEval_InitThreads();
            
            boost::python::handle<> ignored(boost::python::allow_null((PyObject*)nullptr));
            boost::python::object main_module = boost::python::import("__main__");
            boost::python::object main_namespace = main_module.attr("__dict__");
            
            pickle_module = boost::python::import("pickle");
            dumps_func = pickle_module.attr("dumps");
            
            python_initialized = true;
            std::cout << "Python interpreter and Boost.Python initialized successfully\n";
            
        } catch (const boost::python::error_already_set&) {
            handle_python_error("initialize_python");
        }
    }
    
    gil_state = PyGILState_Ensure();
}

void BoostPythonPickleSerializer::cleanup_python() noexcept {
    try {
        if (Py_IsInitialized()) {
            PyGILState_Release(gil_state);
        }
    } catch (...) {

    }
}

std::vector<uint8_t> BoostPythonPickleSerializer::serialize(
    const std::vector<uint8_t>& parent_hash,
    const std::vector<int64_t>& token_ids,
    const std::vector<int64_t>* extra_keys) {
    
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
        reinterpret_cast<const char*>(data.data()), data.size());
    
    if (!py_bytes) {
        throw std::runtime_error("Failed to create Python bytes object");
    }
    
    boost::python::handle<> bytes_handle(py_bytes);
    return boost::python::object(bytes_handle);
}

boost::python::object BoostPythonPickleSerializer::int64_vector_to_python_tuple(
    const std::vector<int64_t>& data) {
    
    boost::python::list py_list;
    for (auto value : data) {
        py_list.append(value);
    }
    
    boost::python::object py_tuple = boost::python::tuple(py_list);
    return py_tuple;
}

std::vector<uint8_t> BoostPythonPickleSerializer::python_to_bytes(const boost::python::object& obj) {
    PyObject* py_obj = obj.ptr();
    if (!PyBytes_Check(py_obj)) 
        throw std::runtime_error("Object is not a bytes type");
    
    char* buffer;
    Py_ssize_t length;
    if (PyBytes_AsStringAndSize(py_obj, &buffer, &length) == -1)
        handle_python_error("PyBytes_AsStringAndSize");
    
    return {reinterpret_cast<uint8_t*>(buffer), reinterpret_cast<uint8_t*>(buffer) + length};
}

}