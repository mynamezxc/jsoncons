// Copyright 2020 Daniel Parker
// Distributed under the Boost license, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// See https://github.com/danielaparker/jsoncons for latest version

#ifndef JSONCONS_JSONSCHEMA_JSON_VALIDATOR_HPP
#define JSONCONS_JSONSCHEMA_JSON_VALIDATOR_HPP

#include <jsoncons/config/jsoncons_config.hpp>
#include <jsoncons/uri.hpp>
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonpointer/jsonpointer.hpp>
#include <jsoncons_ext/jsonschema/schema_loader.hpp>
#include <cassert>
#include <set>
#include <sstream>
#include <iostream>
#include <cassert>
#include <functional>

namespace jsoncons {
namespace jsonschema {

    struct default_error_reporter : public error_reporter
    {
    private:
        void do_error(const validation_error& e) override
        {
            JSONCONS_THROW(e);
        }
    };

    using error_reporter_t = std::function<void(const validation_error& e)>;

    struct error_reporter_adaptor : public error_reporter
    {
        error_reporter_t reporter_;

        error_reporter_adaptor(const error_reporter_t& reporter)
            : reporter_(reporter)
        {
        }
    private:
        void do_error(const validation_error& e) override
        {
            reporter_(e);
        }
    };

    template <class Json>
    class json_validator
    {
        std::shared_ptr<json_schema<Json>> root_;

    public:
        json_validator(std::shared_ptr<json_schema<Json>> root)
            : root_(root)
        {
        }

        json_validator(json_validator &&) = default;
        json_validator &operator=(json_validator &&) = default;

        json_validator(json_validator const &) = delete;
        json_validator &operator=(json_validator const &) = delete;

        ~json_validator() = default;

        // Validate input JSON against a JSON Schema with a default throwing error reporter
        Json validate(const Json& instance) const
        {
            default_error_reporter reporter;
            jsoncons::jsonpointer::json_pointer ptr;
            Json patch(json_array_arg);

            root_->validate(ptr, instance, reporter, patch);
            return patch;
        }

        // Validate input JSON against a JSON Schema with a provided error reporter
        template <class Reporter>
        typename std::enable_if<jsoncons::detail::is_function_object_exact<Reporter,void,validation_error>::value,Json>::type
        validate(const Json& instance, const Reporter& reporter) const
        {
            jsoncons::jsonpointer::json_pointer ptr;
            Json patch(json_array_arg);

            error_reporter_adaptor adaptor(reporter);
            root_->validate(ptr, instance, adaptor, patch);
            return patch;
        }
    };

} // namespace jsonschema
} // namespace jsoncons

#endif // JSONCONS_JSONSCHEMA_JSON_VALIDATOR_HPP
