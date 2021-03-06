// Copyright 2020 Daniel Parker
// Distributed under the Boost license, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// See https://github.com/danielaparker/jsoncons for latest version

#ifndef JSONCONS_JSONSCHEMA_SUBSCHEMA_HPP
#define JSONCONS_JSONSCHEMA_SUBSCHEMA_HPP

#include <jsoncons/config/jsoncons_config.hpp>
#include <jsoncons/uri.hpp>
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonpointer/jsonpointer.hpp>
#include <jsoncons_ext/jsonschema/jsonschema_error.hpp>

namespace jsoncons {
namespace jsonschema {

    class uri_wrapper
    {
        jsoncons::uri uri_;
        std::string identifier_;
    public:
        uri_wrapper()
        {
        }

        uri_wrapper(const std::string& uri)
        {
            auto pos = uri.find('#');
            if (pos != std::string::npos)
            {
                identifier_ = uri.substr(pos + 1); 
                unescape_percent(identifier_);
                //unescape_percent(identifier_);
                //uri_ = jsoncons::uri(uri.substr(0,pos));
            }
            else
            {
                //uri_ = jsoncons::uri(uri);
            }
            uri_ = jsoncons::uri(uri);
        }

        jsoncons::uri uri() const
        {
            return uri_;
        }

        bool has_json_pointer() const
        {
            return !identifier_.empty() && identifier_.front() == '/';
        }

        bool has_identifier() const
        {
            return !identifier_.empty() && identifier_.front() != '/';
        }

        jsoncons::string_view base() const
        {
            return uri_.base();
        }

        jsoncons::string_view path() const
        {
            return uri_.path();
        }

        std::string pointer() const
        {
            return identifier_;
        }

        std::string identifier() const
        {
            return identifier_;
        }

        std::string fragment() const
        {
            return identifier_;
        }

        uri_wrapper resolve(const uri_wrapper& uri) const
        {
            uri_wrapper new_uri;
            new_uri.identifier_ = identifier_;
            new_uri.uri_ = uri_.resolve(uri.uri_);
            return new_uri;
        }

        int compare(const uri_wrapper& other) const
        {
            int result = uri_.compare(other.uri_);
            if (result != 0) 
            {
                return result;
            }
            return result; 
        }

        uri_wrapper append(const std::string& field) const
        {
            if (has_identifier())
                return *this;

            jsoncons::jsonpointer::json_pointer pointer(std::string(uri_.fragment()));
            pointer /= field;

            jsoncons::uri new_uri(uri_.scheme(),
                                  uri_.userinfo(),
                                  uri_.host(),
                                  uri_.port(),
                                  uri_.path(),
                                  uri_.query(),
                                  pointer.string());

            uri_wrapper wrapper;
            wrapper.uri_ = new_uri;
            wrapper.identifier_ = pointer.string();

            return wrapper;
        }

        std::string string() const
        {
            std::string s = uri_.string();
            return s;
        }

        friend bool operator==(const uri_wrapper& lhs, const uri_wrapper& rhs)
        {
            return lhs.compare(rhs) == 0;
        }

        friend bool operator!=(const uri_wrapper& lhs, const uri_wrapper& rhs)
        {
            return lhs.compare(rhs) != 0;
        }

        friend bool operator<(const uri_wrapper& lhs, const uri_wrapper& rhs)
        {
            return lhs.compare(rhs) < 0;
        }

        friend bool operator<=(const uri_wrapper& lhs, const uri_wrapper& rhs)
        {
            return lhs.compare(rhs) <= 0;
        }

        friend bool operator>(const uri_wrapper& lhs, const uri_wrapper& rhs)
        {
            return lhs.compare(rhs) > 0;
        }

        friend bool operator>=(const uri_wrapper& lhs, const uri_wrapper& rhs)
        {
            return lhs.compare(rhs) >= 0;
        }
    private:
        static void unescape_percent(std::string& s)
        {
            if (s.size() >= 3)
            {
                std::size_t pos = s.size() - 2;
                while (pos-- >= 1)
                {
                    if (s[pos] == '%')
                    {
                        std::string hex = s.substr(pos + 1, 2);
                        char ch = (char) std::strtoul(hex.c_str(), nullptr, 16);
                        s.replace(pos, 3, 1, ch);
                    }
                }
            }
        }
    };

    // Interface for validation error handlers
    class error_reporter
    {
    public:
        virtual ~error_reporter() = default;

        void error(const validation_error& e)
        {
            do_error(e);
        }

    private:
        virtual void do_error(const validation_error& /* e */) = 0;
    };

    template <class Json>
    class subschema
    {
    public:
        using schema_pointer = subschema<Json>*;

        virtual ~subschema() = default;

        void validate(const jsoncons::jsonpointer::json_pointer &ptr, 
                      const Json& instance, 
                      error_reporter& reporter, 
                      Json& patch) const 
        {
            do_validate(ptr,instance,reporter,patch);
        }

        virtual jsoncons::optional<Json> get_default_value(const jsoncons::jsonpointer::json_pointer&, const Json&, error_reporter&) const
        {
            return jsoncons::optional<Json>();
        }

    private:
        virtual void do_validate(const jsoncons::jsonpointer::json_pointer &ptr, 
                                 const Json& instance, 
                                 error_reporter& reporter, 
                                 Json& patch) const = 0;
    };

} // namespace jsonschema
} // namespace jsoncons

#endif // JSONCONS_JSONSCHEMA_RULE_HPP
