// Copyright 2014 Renato Tegon Forti, Antony Polukhin.
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_DLL_SHARED_LIBRARY_IMPL_HPP
#define BOOST_DLL_SHARED_LIBRARY_IMPL_HPP

#include <boost/config.hpp>
#include <boost/dll/shared_library_load_mode.hpp>
#include <boost/dll/detail/posix/path_from_handle.hpp>
#include <boost/dll/detail/posix/program_location_impl.hpp>

#include <boost/move/utility.hpp>
#include <boost/swap.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/utility/string_ref.hpp>

#include <dlfcn.h>
#include <link.h>

#ifdef BOOST_HAS_PRAGMA_ONCE
# pragma once
#endif

namespace boost { namespace dll {

class shared_library_impl {

    BOOST_MOVABLE_BUT_NOT_COPYABLE(shared_library_impl)

public:
    typedef void* native_handle_t;

    shared_library_impl() BOOST_NOEXCEPT
        : handle_(NULL)
    {}

    ~shared_library_impl() BOOST_NOEXCEPT {
        unload();
    }
    
    shared_library_impl(BOOST_RV_REF(shared_library_impl) sl)
    {  
        handle_ = sl.handle_; sl.handle_ = NULL;  
    }

    shared_library_impl & operator=(BOOST_RV_REF(shared_library_impl) sl)
    {  
        handle_ = sl.handle_; sl.handle_ = NULL; return *this;  
    }

    void load(const boost::filesystem::path &sl, load_mode::type mode, boost::system::error_code &ec) {
        unload();

        // Do not allow opening NULL paths. User must use load_self() instead
        if (sl.empty()) {
            ec = boost::system::error_code(
                boost::system::errc::bad_file_descriptor,
                boost::system::generic_category()
            );

            return;
        }

        // Fixing modes
        if (!(mode & RTLD_LAZY || mode & RTLD_NOW)) {
            mode |= load_mode::rtld_lazy;
        }

        if (!(mode & RTLD_LOCAL || mode & RTLD_GLOBAL)) {
            mode |= load_mode::rtld_local;
        }

        if (!(mode & load_mode::append_decorations)) {
            handle_ = dlopen(sl.c_str(), static_cast<int>(mode));
        } else {
            handle_ = dlopen(
                // Apple requires '.so' extension
                ((sl.parent_path() / "lib").native() + sl.filename().native() + ".so").c_str(),
                static_cast<int>(mode) & (~static_cast<int>(load_mode::append_decorations))
            );
        }

        if (handle_) {
            return;
        }

        ec = boost::system::error_code(
            boost::system::errc::bad_file_descriptor,
            boost::system::generic_category()
        );

        // Maybe user whanted to load the executable itself? Checking...
        // We assume that usualy user whants to load a dynamic library not the executable itself, that's why
        // we try this anly after traditional load fails.
        boost::system::error_code prog_loc_err;
        boost::filesystem::path loc = boost::dll::detail::program_location_impl(prog_loc_err);
        if (!prog_loc_err && boost::filesystem::equivalent(sl, loc, prog_loc_err) && !prog_loc_err) {
            // As is known the function dlopen() loads the dynamic library file 
            // named by the null-terminated string filename and returns an opaque 
            // "handle" for the dynamic library. If filename is NULL, then the 
            // returned handle is for the main program.
            handle_ = dlopen(NULL, static_cast<int>(mode) & (~static_cast<int>(load_mode::append_decorations)));
            ec.clear();
            if (!handle_) {
                ec = boost::system::error_code(
                    boost::system::errc::bad_file_descriptor,
                    boost::system::generic_category()
                );
            }
        }
    }

    bool is_loaded() const BOOST_NOEXCEPT {
        return (handle_ != 0);
    }

    void unload() BOOST_NOEXCEPT {
        if (!is_loaded()) {
            return;
        }

        dlclose(handle_);
        handle_ = 0;
    }

    void swap(shared_library_impl& rhs) BOOST_NOEXCEPT {
        boost::swap(handle_, rhs.handle_);
    }

    boost::filesystem::path full_module_path(boost::system::error_code &ec) const {
        return boost::dll::detail::path_from_handle(handle_, ec);
    }

    static boost::filesystem::path suffix() {
        // https://sourceforge.net/p/predef/wiki/OperatingSystems/
#if defined(__APPLE__)
        return ".dylib";
#else
        return ".so";
#endif
    }

    void* symbol_addr(boost::string_ref sb, boost::system::error_code &ec) const BOOST_NOEXCEPT {
        // dlsym - obtain the address of a symbol from a dlopen object
        void* const symbol = dlsym(handle_, sb.data());
        if (symbol == NULL) {
            ec = boost::system::error_code(
                boost::system::errc::invalid_seek,
                boost::system::generic_category()
            );
        }

        // If handle does not refer to a valid object opened by dlopen(),
        // or if the named symbol cannot be found within any of the objects
        // associated with handle, dlsym() shall return NULL.
        // More detailed diagnostic information shall be available through dlerror().

        return symbol;
    }

    native_handle_t native() const BOOST_NOEXCEPT {
        return handle_;
    }

private:
    native_handle_t handle_;
};

}} // boost::dll

#endif // BOOST_DLL_SHARED_LIBRARY_IMPL_HPP

