#include "prompt/core/python.hpp"
#include "prompt/core/fs.hpp"
#include <Python.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <expected>

namespace prompt::python {

namespace {
    std::once_flag python_init_flag;
    bool python_available = false;
    std::string python_error;
    std::string repo_root_storage;
    
    std::string const& repo_root() noexcept {
        return repo_root_storage;
    }
    
    // Locate the repo root that contains bin/tse by walking up from the
    // executable (falls back to cwd's git root). The binary may run from
    // any working directory, so exe-relative discovery is the reliable one.
    std::filesystem::path find_tse_repo_root() {
        std::error_code ec;
        auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            auto dir = exe.parent_path();
            while (true) {
                if (std::filesystem::exists(dir / "bin" / "tse")) {
                    // cpp_prompt/build -> cpp_prompt -> repo root
                    return dir;
                }
                auto parent = dir.parent_path();
                if (parent == dir || parent.empty()) break;
                dir = parent;
            }
        }
        if (auto git_root = prompt::fs::find_git_root()) {
            if (std::filesystem::exists(*git_root / "bin" / "tse")) {
                return *git_root;
            }
        }
        return {};
    }
    
    // Load a module from a file path that may lack the .py extension
    // (e.g. bin/tse). Registers it in sys.modules under `module_name`.
    // Returns false and sets err on failure.
    bool load_file_module(char const* module_name, std::string const& path,
                          std::string& err) noexcept {
        std::string code;
        code += "import importlib.machinery, importlib.util, sys\n";
        code += "_loader = importlib.machinery.SourceFileLoader('";
        code += module_name;
        code += "', r'";
        code += path;
        code += "')\n";
        code += "_spec = importlib.util.spec_from_loader('";
        code += module_name;
        code += "', _loader)\n";
        code += "_mod = importlib.util.module_from_spec(_spec)\n";
        code += "sys.modules['";
        code += module_name;
        code += "'] = _mod\n";
        code += "_loader.exec_module(_mod)\n";
        
        if (PyRun_SimpleString(code.c_str()) != 0) {
            if (PyErr_Occurred()) {
                PyObject* ptype = nullptr, *pvalue = nullptr, *ptrace = nullptr;
                PyErr_Fetch(&ptype, &pvalue, &ptrace);
                if (pvalue) {
                    char const* msg = PyUnicode_AsUTF8(pvalue);
                    if (msg) err = msg;
                }
                Py_XDECREF(ptype);
                Py_XDECREF(pvalue);
                Py_XDECREF(ptrace);
            }
            if (err.empty()) err = "failed to load " + path;
            // Roll back the partial sys.modules entry
            std::string cleanup = "sys.modules.pop('" + std::string(module_name) + "', None)\n";
            PyRun_SimpleString(cleanup.c_str());
            return false;
        }
        return true;
    }
}

void init() noexcept {
    std::call_once(python_init_flag, []() {
        Py_InitializeEx(0);
        if (!Py_IsInitialized()) {
            python_error = "Py_InitializeEx failed";
            return;
        }
        python_available = true;
        
        // Make bin/tse importable as bin.tse (the file has no .py extension).
        // Failure here is not fatal — Python stays available for other users
        // (e.g. spp); tse-dependent prompts surface the error per call.
        std::string repo_root;
        if (char const* env_root = std::getenv("PROMPT_REPO_ROOT")) {
            repo_root = env_root;
        } else {
            auto found = find_tse_repo_root();
            if (!found.empty()) repo_root = found.string();
        }
        repo_root_storage = repo_root;
        
        if (!repo_root.empty()) {
            std::string path_code = "import sys; sys.path.insert(0, r'";
            path_code += repo_root;
            path_code += "')";
            PyRun_SimpleString(path_code.c_str());
            
            std::string tse_err;
            if (!load_file_module("bin.tse", repo_root + "/bin/tse", tse_err)) {
                python_error = "Failed to import bin.tse: " + tse_err;
            } else {
                // Install string-typed shims: python::call only passes str
                // args, but collect() needs a Client() instance + int/bool
                // and markdown() needs a dict (call() passes JSON text).
                PyRun_SimpleString(R"PY(
import json as _json, sys as _sys
_m = _sys.modules['bin.tse']
if not hasattr(_m, 'collect_embed'):
    def _collect_embed(symbol, days, top, with_codal, adjusted):
        return _m.collect(
            _m.Client(), symbol, int(days), int(top),
            str(with_codal).lower() in ('1', 'true', 'yes'),
            str(adjusted).lower() in ('1', 'true', 'yes'))
    def _markdown_embed(data):
        if isinstance(data, str):
            data = _json.loads(data)
        return _m.markdown(data)
    _m.collect_embed = _collect_embed
    _m.markdown_embed = _markdown_embed
)PY");
            }
        } else {
            python_error = "Failed to locate repo root for bin.tse";
        }
    });
}

bool available() noexcept {
    init();
    return python_available;
}

std::expected<std::string, std::string> call(
    std::string_view module,
    std::string_view function,
    std::span<std::string_view const> args) noexcept {
    
    init();
    if (!python_available) {
        return std::unexpected(python_error.empty() ? "Python not initialized" : python_error);
    }
    
    std::string module_str(module);
    PyObject* mod = PyImport_ImportModule(module_str.c_str());
    if (!mod && module_str.starts_with("bin.") && !repo_root().empty()) {
        // Repo scripts (bin/tse, bin/spp) have no .py extension — load them
        // explicitly via SourceFileLoader and retry.
        PyErr_Clear();
        std::string file_path = repo_root() + "/" + module_str;
        for (std::size_t i = repo_root().size() + 1; i < file_path.size(); ++i) {
            if (file_path[i] == '.') file_path[i] = '/';
        }
        std::string load_err;
        if (load_file_module(module_str.c_str(), file_path, load_err)) {
            mod = PyImport_ImportModule(module_str.c_str());
        } else {
            return std::unexpected("Failed to import module: " + module_str + ": " + load_err);
        }
    }
    if (!mod) {
        std::string import_err;
        if (PyErr_Occurred()) {
            PyObject* ptype = nullptr, *pvalue = nullptr, *ptrace = nullptr;
            PyErr_Fetch(&ptype, &pvalue, &ptrace);
            if (pvalue) {
                char const* msg = PyUnicode_AsUTF8(pvalue);
                if (msg) import_err = msg;
            }
            Py_XDECREF(ptype);
            Py_XDECREF(pvalue);
            Py_XDECREF(ptrace);
        }
        if (import_err.empty()) import_err = python_error;
        return std::unexpected("Failed to import module: " + module_str +
                               (import_err.empty() ? "" : ": " + import_err));
    }
    
    std::string func_str(function);
    PyObject* func = PyObject_GetAttrString(mod, func_str.c_str());
    Py_DECREF(mod);
    if (!func || !PyCallable_Check(func)) {
        if (func) Py_DECREF(func);
        return std::unexpected("Function not found: " + func_str);
    }
    
    PyObject* args_tuple = PyTuple_New(args.size());
    for (std::size_t i = 0; i < args.size(); ++i) {
        PyObject* arg = PyUnicode_FromStringAndSize(args[i].data(), args[i].size());
        PyTuple_SET_ITEM(args_tuple, i, arg);
    }
    
    PyObject* result = PyObject_CallObject(func, args_tuple);
    Py_DECREF(args_tuple);
    Py_DECREF(func);
    
    if (!result) {
        std::string call_err;
        if (PyErr_Occurred()) {
            PyObject* ptype = nullptr, *pvalue = nullptr, *ptrace = nullptr;
            PyErr_Fetch(&ptype, &pvalue, &ptrace);
            if (pvalue) {
                char const* msg = PyUnicode_AsUTF8(pvalue);
                if (msg) call_err = msg;
            }
            Py_XDECREF(ptype);
            Py_XDECREF(pvalue);
            Py_XDECREF(ptrace);
        }
        return std::unexpected("Python call failed: " + func_str +
                               (call_err.empty() ? "" : ": " + call_err));
    }
    
    PyObject* json_module = PyImport_ImportModule("json");
    if (!json_module) {
        Py_DECREF(result);
        return std::unexpected("Failed to import json module");
    }
    
    PyObject* dumps = PyObject_GetAttrString(json_module, "dumps");
    Py_DECREF(json_module);
    if (!dumps) {
        Py_DECREF(result);
        return std::unexpected("Failed to get json.dumps");
    }
    
    PyObject* json_args = PyTuple_Pack(1, result);
    Py_DECREF(result);
    PyObject* json_result = PyObject_CallObject(dumps, json_args);
    Py_DECREF(dumps);
    Py_DECREF(json_args);
    
    if (!json_result) {
        PyErr_Print();
        return std::unexpected("json.dumps failed");
    }
    
    char const* json_str = PyUnicode_AsUTF8(json_result);
    std::string output = json_str ? json_str : "";
    Py_DECREF(json_result);
    
    return output;
}

} // namespace prompt::python
