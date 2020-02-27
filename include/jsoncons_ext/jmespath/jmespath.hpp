// Copyright 2020 Daniel Parker
// Distributed under the Boost license, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// See https://github.com/danielaparker/jsoncons for latest version

#ifndef JSONCONS_JMESPATH_JMESPATH_HPP
#define JSONCONS_JMESPATH_JMESPATH_HPP

#include <array> // std::array
#include <string>
#include <vector>
#include <memory>
#include <type_traits> // std::is_const
#include <limits> // std::numeric_limits
#include <utility> // std::move
#include <regex>
#include <set> // std::set
#include <iterator> // std::make_move_iterator
#include <functional> // 
#include <algorithm> // std::sort
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jmespath/jmespath_error.hpp>

namespace jsoncons { namespace jmespath {

JSONCONS_STRING_LITERAL(sort_by,'s','o','r','t','-','b','y')

struct slice
{
    int64_t start_;
    optional<int64_t> end_;
    int64_t step_;

    slice()
        : start_(0), end_(), step_(1)
    {
    }

    slice(int64_t start, const optional<int64_t>& end, int64_t step) 
        : start_(start), end_(end), step_(step)
    {
    }

    slice(const slice& other)
        : start_(other.start_), end_(other.end_), step_(other.step_)
    {
    }

    slice& operator=(const slice& rhs) 
    {
        if (this != &rhs)
        {
            start_ = rhs.start_;
            if (rhs.end_)
            {
                end_ = rhs.end_;
            }
            else
            {
                end_.reset();
            }
            step_ = rhs.step_;
        }
        return *this;
    }

    int64_t get_start(std::size_t size) const
    {
        return start_ >= 0 ? start_ : (static_cast<int64_t>(size) - start_);
    }

    int64_t get_end(std::size_t size) const
    {
        if (end_)
        {
            auto len = end_.value() >= 0 ? end_.value() : (static_cast<int64_t>(size) - end_.value());
            return len <= static_cast<int64_t>(size) ? len : static_cast<int64_t>(size);
        }
        else
        {
            return static_cast<int64_t>(size);
        }
    }

    int64_t step() const
    {
        return step_; // Allow negative
    }
};

namespace detail {
template <class Json,
    class JsonReference>
    class jmespath_evaluator;
}

template<class Json>
Json search(const Json& root, const typename Json::string_view_type& path)
{
    jsoncons::jmespath::detail::jmespath_evaluator<Json,const Json&> evaluator;
    return evaluator.evaluate(root, path);
}

template<class Json>
Json search(const Json& root, const typename Json::string_view_type& path, std::error_code& ec)
{
    jsoncons::jmespath::detail::jmespath_evaluator<Json,const Json&> evaluator;
    return evaluator.evaluate(root, path, ec);
}

namespace detail {
 
enum class path_state 
{
    start,

    quoted_string,
    raw_string,
    json_value,
    key_expr,
    val_expr,
    identifier_or_function_expr,
    arg_or_right_paren,
    unquoted_string,
    expression,
    key_val_expr,
    compound_expression,
    number,
    digit,
    bracket_specifier9,
    bracket_specifier,
    multi_select_hash,
    bracket_specifier2,
    bracket_specifier3,
    bracket_specifier4,
    expect_dot,
    expect_filter_right_bracket,
    expect_right_bracket4,
    expect_right_brace,
    expect_colon,
    comparator,
    cmp_lt_or_lte,
    cmp_eq,
    cmp_gt_or_gte,
    cmp_ne
};

template<class Json,
         class JsonReference>
class jmespath_evaluator : public ser_context
{
    typedef typename Json::char_type char_type;
    typedef typename Json::char_traits_type char_traits_type;
    typedef std::basic_string<char_type,char_traits_type> string_type;
    typedef typename Json::string_view_type string_view_type;
    typedef JsonReference reference;
    using pointer = typename std::conditional<std::is_const<typename std::remove_reference<JsonReference>::type>::value,typename Json::const_pointer,typename Json::pointer>::type;
    typedef typename Json::const_pointer const_pointer;

    class jmespath_context
    {
        std::vector<std::unique_ptr<Json>> temp_storage_;
    public:

        template <typename... Args>
        Json* new_instance(Args&& ... args)
        {
            auto temp = jsoncons::make_unique<Json>(std::forward<Args>(args)...);
            Json* ptr = temp.get();
            temp_storage_.emplace_back(std::move(temp));
            return ptr;
        }
    };

    // selector
class selector_base
    {
        bool expression_type_;
    public:
        selector_base()
            : expression_type_(false)
        {
        }

        bool expression_type() const
        {
            return expression_type_;
        }

        void expression_type(bool value)
        {
            expression_type_ = value;
        }

        virtual ~selector_base()
        {
        }

        virtual void add_selector(std::unique_ptr<selector_base>&&) = 0;

        virtual reference select(jmespath_context&, reference val, std::error_code& ec) = 0;

        virtual string_type to_string() const
        {
            return string_type("to_string not implemented");
        }
    };

    // function

    static reference sort_by(jmespath_context& context,
                             reference val,
                             const std::vector<std::unique_ptr<selector_base>>& selectors,
                             std::error_code& ec)
    {
        if (selectors.size() != 2)
        {
            ec = jmespath_errc::invalid_argument;
            return Json::null();
        }

        reference u = selectors[0]->select(context, val, ec);

        if (!u.is_array())
        {
            ec = jmespath_errc::invalid_argument;
            return Json::null();
        }
        auto& key_selector = selectors[1];

        auto v = context.new_instance(u);
        std::sort((v->array_range()).begin(), (v->array_range()).end(),
            [&context,&key_selector,&ec](reference lhs, reference rhs) -> bool
        {
            reference key1 = key_selector->select(context, lhs, ec);
            reference key2 = key_selector->select(context, rhs, ec);

            return key1 < key2;
        });
        return *v;
    }

    typedef std::function<reference(jmespath_context& context, reference, const std::vector<std::unique_ptr<selector_base>>&, std::error_code&)> function_type;

    std::unordered_map<string_type,function_type> functions_ =
    {
        {string_type("sort_by"),sort_by}
    };

    class function_selector : public selector_base
    {
        function_type& f_;
        std::vector<std::unique_ptr<selector_base>> selectors_;
    public:
        function_selector(function_type& f)
            : f_(f)
        {
        }

        void add_selector(std::unique_ptr<selector_base>&& selector) override 
        {
            selectors_.emplace_back(std::move(selector));
        }

        reference select(jmespath_context& context, reference val, std::error_code& ec) override
        {
            return f_(context, val, selectors_, ec);
        }
    };

    class compound_expression final : public selector_base
    {
    public:
        std::vector<std::unique_ptr<selector_base>> selectors_;

        compound_expression()
        {
        }

        void add_selector(std::unique_ptr<selector_base>&& selector) override 
        {
            selectors_.emplace_back(std::move(selector));
        }

        reference select(jmespath_context& context, reference val, std::error_code& ec) override
        {
            pointer ptr = std::addressof(val);
            for (auto& selector : selectors_)
            {
                ptr = std::addressof(selector->select(context, *ptr, ec)        );
            }
            return *ptr;
        }
    };

    class name_expression_selector final : public selector_base
    {
    public:
        std::basic_string<char_type> name_;
        std::unique_ptr<selector_base> selector_;

        name_expression_selector(std::basic_string<char_type>&& name,
                                 std::unique_ptr<selector_base>&& selector)
            : name_(std::move(name), selector_(std::move(selector)))
        {
        }

        void add_selector(std::unique_ptr<selector_base>&& selector) override 
        {
        }

        reference select(jmespath_context& context, reference val, std::error_code& ec) override
        {
            auto key_valuep = context.new_instance(json_object_arg);
            key_valuep->try_emplace(name_,selector_->select(context, val, ec)        );
            return *key_valuep;
        }
    };

    class list_projection final : public selector_base
    {
    public:
        std::unique_ptr<selector_base> lhs_selector_;
        compound_expression rhs_selector_;

        list_projection(std::unique_ptr<selector_base>&& lhs_selector)
            : lhs_selector_(std::move(lhs_selector))
        {
        }

        void add_selector(std::unique_ptr<selector_base>&& selector) override 
        {
            rhs_selector_.add_selector(std::move(selector));
        }

        reference select(jmespath_context& context, reference val, std::error_code& ec) override
        {
            reference lhs = lhs_selector_->select(context, val, ec);
            if (!lhs.is_array())
            {
                return Json::null();
            }

            auto resultp = context.new_instance(json_array_arg);
            for (reference item : lhs.array_range())
            {
                reference j = rhs_selector_.select(context, item, ec);
                if (ec)
                {
                    return Json::null();
                }
                if (!j.is_null())
                {
                    resultp->push_back(j);
                }
            }
            return *resultp;
        }
    };

    class list_projection2 final : public selector_base
    {
    public:
        compound_expression rhs_selector_;

        list_projection2()
        {
        }

        void add_selector(std::unique_ptr<selector_base>&& selector) override 
        {
            rhs_selector_.add_selector(std::move(selector));
        }

        reference select(jmespath_context& context, reference val, std::error_code& ec) override
        {
            if (!val.is_array())
            {
                return Json::null();
            }

            auto resultp = context.new_instance(json_array_arg);
            for (reference item : val.array_range())
            {
                reference j = rhs_selector_.select(context, item, ec);
                if (ec)
                {
                    return Json::null();
                }
                if (!j.is_null())
                {
                    resultp->push_back(j);
                }
            }
            return *resultp;
        }
    };

    class pipe_selector final : public selector_base
    {
    public:
        std::unique_ptr<selector_base> lhs_selector_;
        std::vector<std::unique_ptr<selector_base>> rhs_selectors_;

        pipe_selector(std::unique_ptr<selector_base>&& lhs_selector)
            : lhs_selector_(std::move(lhs_selector))
        {
        }

        void add_selector(std::unique_ptr<selector_base>&& rhs_selectors) override 
        {
            rhs_selectors_.emplace_back(std::move(rhs_selectors));
        }

        reference select(jmespath_context& context, reference val, std::error_code& ec) override
        {
            reference lhs = lhs_selector_->select(context, val, ec);
            if (!lhs.is_array())
            {
                return Json::null();
            }

            pointer ptr = std::addressof(lhs);
            for (auto& selector : rhs_selectors_)
            {
                ptr = std::addressof(selector->select(context, *ptr, ec)        );
            }
            return *ptr;
        }
    };

    class flatten_projection final : public selector_base
    {
    public:
        std::unique_ptr<selector_base> lhs_selector_;
        std::vector<std::unique_ptr<selector_base>> rhs_selectors_;

        flatten_projection(std::unique_ptr<selector_base>&& lhs_selector)
        {
            lhs_selector_ = std::move(lhs_selector);
        }

        void add_selector(std::unique_ptr<selector_base>&& rhs_selectors) override 
        {
            rhs_selectors_.emplace_back(std::move(rhs_selectors));
        }

        reference select(jmespath_context& context, reference val, std::error_code& ec) override
        {
            reference lhs = lhs_selector_->select(context, val, ec);
            if (!lhs.is_array())
            {
                return Json::null();
            }

            auto currentp = context.new_instance(json_array_arg);
            for (reference item : lhs.array_range())
            {
                if (item.is_array())
                {
                    for (reference item_of_item : item.array_range())
                    {
                        currentp->push_back(item_of_item);
                    }
                }
                else
                {
                    currentp->push_back(item);
                }
            }

            auto resultp = context.new_instance(json_array_arg);
            for (reference item : currentp->array_range())
            {
                pointer ptr = std::addressof(item);

                for (auto& selector : rhs_selectors_)
                {
                    ptr = std::addressof(selector->select(context, *ptr, ec)        );
                }
                if (!ptr->is_null())
                {
                    resultp->push_back(*ptr);
                }
            }
            return *resultp;
        }
    };

    class object_projection final : public selector_base
    {
    public:
        std::unique_ptr<selector_base> lhs_selector_;
        std::vector<std::unique_ptr<selector_base>> rhs_selectors_;

        object_projection(std::unique_ptr<selector_base>&& lhs_selector)
        {
            lhs_selector_ = std::move(lhs_selector);
        }

        void add_selector(std::unique_ptr<selector_base>&& rhs_selectors) override 
        {
            rhs_selectors_.emplace_back(std::move(rhs_selectors));
        }

        reference select(jmespath_context& context, reference val, std::error_code& ec) override
        {
            reference lhs = lhs_selector_->select(context, val, ec);
            if (!lhs.is_object())
            {
                return Json::null();
            }

            auto resultp = context.new_instance(json_array_arg);
            for (auto& item : lhs.object_range())
            {
                pointer ptr = std::addressof(item.value());
                for (auto& selector : rhs_selectors_)
                {
                    ptr = std::addressof(selector->select(context, *ptr, ec)        );
                }
                if (!ptr->is_null())
                {
                    resultp->push_back(*ptr);
                }
            }
            return *resultp;
        }
    };

    class identifier_selector final : public selector_base
    {
    private:
        string_type identifier_;
    public:
        identifier_selector(const string_view_type& name)
            : identifier_(name)
        {
        }

        void add_selector(std::unique_ptr<selector_base>&&) override
        {
            // Error
        }

        reference select(jmespath_context&, reference val, std::error_code&) override
        {
            if (val.is_object() && val.contains(identifier_))
            {
                return val.at(identifier_);
            }
            else 
            {
                return Json::null();
            }
        }
    };

    class json_value_selector final : public selector_base
    {
    private:
        Json j_;
    public:
        json_value_selector(const Json& j)
            : j_(j)
        {
        }
        json_value_selector(Json&& j)
            : j_(std::move(j))
        {
        }

        void add_selector(std::unique_ptr<selector_base>&&) override
        {
            // noop
        }

        reference select(jmespath_context&, reference, std::error_code&)             override
        {
            return j_;
        }
    };

    class index_selector final : public selector_base
    {
    private:
        int64_t index_;
    public:
        index_selector(int64_t index)
            : index_(index)
        {
        }

        void add_selector(std::unique_ptr<selector_base>&&) override 
        {
            // noop
        }

        reference select(jmespath_context&, reference val, std::error_code&) override
        {
            if (val.is_array())
            {
                int64_t slen = static_cast<int64_t>(val.size());
                if (index_ >= 0 && index_ < slen)
                {
                    std::size_t index = static_cast<std::size_t>(index_);
                    return val.at(index);
                }
                else if (index_ < 0 && (slen+index_) < slen)
                {
                    std::size_t index = static_cast<std::size_t>(slen + index_);
                    return val.at(index);
                }
                else
                {
                    return Json::null();
                }
            }
            return Json::null();
        }
    };

    class slice_projection final : public selector_base
    {
    private:
        std::unique_ptr<selector_base> lhs_selector_;
        slice slice_;
        std::vector<std::unique_ptr<selector_base>> rhs_selectors_;
    public:
        slice_projection(std::unique_ptr<selector_base>&& lhs_selector,
                         const slice& a_slice)
            : lhs_selector_(std::move(lhs_selector)), slice_(a_slice)
        {
        }

        void add_selector(std::unique_ptr<selector_base>&& selector) override
        {
            rhs_selectors_.emplace_back(std::move(selector));
        }

        reference select(jmespath_context& context, reference val, std::error_code& ec) override
        {
            reference lhs = lhs_selector_->select(context, val, ec);
            if (!lhs.is_array())
            {
                return Json::null();
            }

            auto tempp = context.new_instance(json_array_arg);

            auto start = slice_.get_start(lhs.size());
            auto end = slice_.get_end(lhs.size());
            auto step = slice_.step();
            if (step >= 0)
            {
                for (int64_t j = start; j < end; j += step)
                {
                    tempp->emplace_back(lhs[static_cast<std::size_t>(j)]);
                }
            }
            else
            {
                for (int64_t j = end-1; j >= start; j += step)
                {
                    tempp->emplace_back(lhs[static_cast<std::size_t>(j)]);
                }
            }

            auto resultp = context.new_instance(json_array_arg);
            for (reference item : tempp->array_range())
            {
                pointer ptr = std::addressof(item);
                for (auto& selector : rhs_selectors_)
                {
                    ptr = std::addressof(selector->select(context, *ptr, ec));
                }
                if (!ptr->is_null())
                {
                    resultp->push_back(*ptr);
                }
            }
            return *resultp;
        }
    };

    struct cmp_eq
    {
        optional<bool> operator()(const Json& lhs, const Json& rhs)
        {
            return lhs == rhs ? true : false;
        }
    };

    struct cmp_lt
    {
        optional<bool> operator()(const Json& lhs, const Json& rhs)
        {
            return (lhs.is_number() && rhs.is_number()) ? (lhs < rhs? true : false) : optional<bool>();
        }
    };

    struct cmp_lte
    {
        optional<bool> operator()(const Json& lhs, const Json& rhs)
        {
            return (lhs.is_number() && rhs.is_number()) ? (lhs <= rhs? true : false) : optional<bool>();
        }
    };

    struct cmp_gt
    {
        optional<bool> operator()(const Json& lhs, const Json& rhs)
        {
            return (lhs.is_number() && rhs.is_number()) ? (lhs > rhs? true : false) : optional<bool>();
        }
    };

    struct cmp_gte
    {
        optional<bool> operator()(const Json& lhs, const Json& rhs)
        {
            return (lhs.is_number() && rhs.is_number()) ? (lhs >= rhs? true : false) : optional<bool>();
        }
    };

    struct cmp_ne
    {
        optional<bool> operator()(const Json& lhs, const Json& rhs)
        {
            return lhs != rhs ? true : false;
        }
    };

    class filter_expression final : public selector_base
    {
    public:
        std::unique_ptr<selector_base> lhs_selector_;
        std::unique_ptr<selector_base> cmp_selector_;
        compound_expression rhs_selector_;

        filter_expression(std::unique_ptr<selector_base>&& lhs_selector,
                          std::unique_ptr<selector_base>&& cmp_selector)
            : lhs_selector_(std::move(lhs_selector)),
              cmp_selector_(std::move(cmp_selector))
        {
        }

        void add_selector(std::unique_ptr<selector_base>&& selector) override 
        {
            rhs_selector_.add_selector(std::move(selector));
        }

        reference select(jmespath_context& context, reference val, std::error_code& ec) override
        {
            reference lhs = lhs_selector_->select(context, val, ec);
            if (!lhs.is_array())
            {
                return Json::null();
            }

            auto tempp = context.new_instance(json_array_arg);
            for (auto& item : lhs.array_range())
            {
                reference x = cmp_selector_->select(context, item, ec);
                if (x.template as<bool>())
                {
                    tempp->emplace_back(item);
                }
            }

            auto resultp = context.new_instance(json_array_arg);
            for (auto& item : tempp->array_range())
            {
                reference v = rhs_selector_.select(context, item, ec);
                if (!v.is_null())
                {
                    resultp->emplace_back(v);
                }
            }
            return *resultp;
        }
    };

    template <typename Comparator>
    class comparator_selector final : public selector_base
    {
    public:
        std::unique_ptr<selector_base> lhs_selector_;
        compound_expression rhs_selector_;
        Comparator cmp_;

        comparator_selector(std::unique_ptr<selector_base>&& lhs_selector)
            : lhs_selector_(std::move(lhs_selector))
        {
        }

        void add_selector(std::unique_ptr<selector_base>&& selector) override 
        {
            rhs_selector_.add_selector(std::move(selector));
        }

        reference select(jmespath_context& context, reference val, std::error_code& ec) override
        {
            static const Json t(true, semantic_tag::none);
            static const Json f(false, semantic_tag::none);

            reference lhs = lhs_selector_->select(context, val, ec);
            if (ec)
            {
                return Json::null();
            }
            reference rhs = rhs_selector_.select(context, val, ec);
            if (ec)
            {
                return Json::null();
            }
            auto r = cmp_(lhs,rhs);
            return (r && r.value()) ? t : f;
        }
    };

    class multi_select_list final : public selector_base
    {
    public:
        std::vector<std::unique_ptr<selector_base>> selectors_;

        multi_select_list(std::vector<std::unique_ptr<selector_base>>&& selectors)
            : selectors_(std::move(selectors))
        {
        }

        void add_selector(std::unique_ptr<selector_base>&&) override
        {
        }

        reference select(jmespath_context& context, reference val, std::error_code& ec) override
        {
            if (!val.is_object())
            {
                return Json::null();
            }

            auto resultp = context.new_instance(json_array_arg);
            resultp->reserve(selectors_.size());
            for (auto& selector : selectors_)
            {
                resultp->push_back(selector->select(context, val, ec)        );
            }
            return *resultp;
        }
    };

    struct key_selector
    {
        string_type key;
        std::unique_ptr<selector_base> selector;

        key_selector(string_type&& key)
            : key(std::move(key))
        {
        }

        key_selector(std::unique_ptr<selector_base>&& selector)
            : selector(std::move(selector))
        {
        }

        key_selector(const key_selector&) = delete;
        key_selector& operator=(const key_selector&) = delete;
        key_selector(key_selector&&) = default;
        key_selector& operator=(key_selector&&) = default;
    };

    class multi_select_hash final : public selector_base
    {
    public:
        std::vector<key_selector> key_selectors_;

        multi_select_hash(std::vector<key_selector>&& key_selectors)
            : key_selectors_(std::move(key_selectors))
        {
        }

        void add_selector(std::unique_ptr<selector_base>&&) override 
        {
        }

        reference select(jmespath_context& context, reference val, std::error_code& ec) override
        {
            if (!val.is_object())
            {
                return Json::null();
            }

            auto resultp = context.new_instance(json_object_arg);
            resultp->reserve(key_selectors_.size());
            for (auto& key_selector : key_selectors_)
            {
                resultp->try_emplace(key_selector.key,key_selector.selector->select(context, val, ec)        );
            }
            return *resultp;
        }
    };

    std::size_t line_;
    std::size_t column_;
    const char_type* begin_input_;
    const char_type* end_input_;
    const char_type* p_;

    std::vector<path_state> state_stack_;
    std::vector<std::size_t> structure_offset_stack_;
    std::vector<key_selector> key_selector_stack_;
    jmespath_context temp_factory_;

public:
    jmespath_evaluator()
        : line_(1), column_(1),
          begin_input_(nullptr), end_input_(nullptr),
          p_(nullptr)
    {
        key_selector_stack_.emplace_back(jsoncons::make_unique<compound_expression>());
    }

    std::size_t line() const
    {
        return line_;
    }

    std::size_t column() const
    {
        return column_;
    }

    reference evaluate(reference root, const string_view_type& path)
    {
        std::error_code ec;
        reference j = evaluate(root, path.data(), path.length(), ec);
        if (ec)
        {
            JSONCONS_THROW(jmespath_error(ec, line_, column_));
        }
        return j;
    }

    reference evaluate(reference root, const string_view_type& path, std::error_code& ec)
    {
        JSONCONS_TRY
        {
            return evaluate(root, path.data(), path.length(), ec);
        }
        JSONCONS_CATCH(...)
        {
            ec = jmespath_errc::unknown_error;
        }
    }
 
    reference evaluate(reference root, 
                       const char_type* path, 
                       std::size_t length,
                       std::error_code& ec)
    {
        state_stack_.emplace_back(path_state::start);

        string_type buffer;
 
        begin_input_ = path;
        end_input_ = path + length;
        p_ = begin_input_;

        slice a_slice{};

        while (p_ < end_input_)
        {
            switch (state_stack_.back())
            {
                case path_state::start: 
                {
                    state_stack_.back() = path_state::compound_expression;
                    state_stack_.emplace_back(path_state::expression);
                    break;
                }
                case path_state::expression: 
                {
                    switch (*p_)
                    {
                        case ' ':case '\t':case '\r':case '\n':
                            advance_past_space_character();
                            break;
                        case '\"':
                            state_stack_.pop_back();
                            state_stack_.emplace_back(path_state::val_expr);
                            state_stack_.emplace_back(path_state::quoted_string);
                            ++p_;
                            ++column_;
                            break;
                        case '\'':
                            state_stack_.pop_back();
                            state_stack_.emplace_back(path_state::raw_string);
                            ++p_;
                            ++column_;
                            break;
                        case '`':
                            state_stack_.pop_back();
                            state_stack_.emplace_back(path_state::json_value);
                            ++p_;
                            ++column_;
                            break;
                        case '[':
                            state_stack_.pop_back();
                            state_stack_.emplace_back(path_state::bracket_specifier);
                            ++p_;
                            ++column_;
                            break;
                        case '{':
                            state_stack_.pop_back();
                            state_stack_.emplace_back(path_state::multi_select_hash);
                            ++p_;
                            ++column_;
                            break;
                        case '*':
                            //state_stack_.pop_back();
                            key_selector_stack_.back() = key_selector(jsoncons::make_unique<object_projection>(std::move(key_selector_stack_.back().selector)));
                            state_stack_.emplace_back(path_state::expect_dot);
                            ++p_;
                            ++column_;
                            break;
                        case '&':
                            key_selector_stack_.back().selector->expression_type(true);
                            ++p_;
                            ++column_;
                            break;                        
                        default:
                            if ((*p_ >= 'A' && *p_ <= 'Z') || (*p_ >= 'a' && *p_ <= 'z') || (*p_ == '_'))
                            {
                                state_stack_.pop_back();
                                state_stack_.emplace_back(path_state::identifier_or_function_expr);
                                state_stack_.emplace_back(path_state::unquoted_string);
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                            }
                            else
                            {
                                ec = jmespath_errc::expected_identifier;
                                return Json::null();
                            }
                            break;
                    };
                    break;
                }
                case path_state::key_expr:
                    key_selector_stack_.back().key = std::move(buffer);
                    buffer.clear(); 
                    state_stack_.pop_back(); 
                    break;
                case path_state::val_expr:
                    switch(*p_)
                    {
                        case '\"':
                            ++p_;
                            ++column_;
                            key_selector_stack_.back().selector->add_selector(jsoncons::make_unique<identifier_selector>(buffer));
                            buffer.clear();
                            state_stack_.pop_back(); 
                            break;
                        default:
                            key_selector_stack_.back().selector->add_selector(jsoncons::make_unique<identifier_selector>(buffer));
                            buffer.clear();
                            state_stack_.pop_back(); 
                            break;
                    }
                    break;
                case path_state::identifier_or_function_expr:
                    switch(*p_)
                    {
                        case '(':
                        {
                            auto it = functions_.find(buffer);
                            buffer.clear();
                            if (it == functions_.end())
                            {
                                ec = jmespath_errc::function_name_not_found;
                                return Json::null();
                            }
                            key_selector_stack_.emplace_back(jsoncons::make_unique<function_selector>(it->second));
                            structure_offset_stack_.push_back(key_selector_stack_.size());
                            key_selector_stack_.emplace_back(jsoncons::make_unique<compound_expression>());
                            state_stack_.back() = path_state::arg_or_right_paren;
                            state_stack_.emplace_back(path_state::expression);
                            ++p_;
                            ++column_;
                            break;
                        }
                        default:
                        {
                            key_selector_stack_.back().selector->add_selector(jsoncons::make_unique<identifier_selector>(buffer));
                            buffer.clear();
                            state_stack_.pop_back(); 
                            break;
                        }
                    }
                    break;

                case path_state::arg_or_right_paren:
                    switch (*p_)
                    {
                        case ' ':case '\t':case '\r':case '\n':
                            advance_past_space_character();
                            break;
                        case ',':
                            key_selector_stack_.emplace_back(key_selector(jsoncons::make_unique<compound_expression>()));
                            state_stack_.emplace_back(path_state::expression);
                            ++p_;
                            ++column_;
                            break;
                        case ')':
                        {
                            size_t pos = structure_offset_stack_.back();
                            structure_offset_stack_.pop_back();
                            auto& selector = key_selector_stack_[pos-1].selector;
                            for (size_t i = pos; i < key_selector_stack_.size(); ++i)
                            {
                                selector->add_selector(std::move(key_selector_stack_[i].selector));
                            }
                            key_selector_stack_.erase(key_selector_stack_.begin()+pos, key_selector_stack_.end());
                            state_stack_.pop_back(); 
                            ++p_;
                            ++column_;
                            break;
                        }
                        default:
                            break;
                    }
                    break;

                case path_state::quoted_string: 
                    switch (*p_)
                    {
                        case '\"':
                            state_stack_.pop_back(); // quoted_string
                            break;
                        case '\\':
                            if (p_+1 < end_input_)
                            {
                                ++p_;
                                ++column_;
                                buffer.push_back(*p_);
                            }
                            else
                            {
                                ec = jmespath_errc::unexpected_end_of_input;
                                return Json::null();
                            }
                            ++p_;
                            ++column_;
                            break;
                        default:
                            buffer.push_back(*p_);
                            ++p_;
                            ++column_;
                            break;
                    };
                    break;

                case path_state::unquoted_string: 
                    switch (*p_)
                    {
                        case ' ':case '\t':case '\r':case '\n':
                            state_stack_.pop_back(); // unquoted_string
                            advance_past_space_character();
                            break;
                        default:
                            if ((*p_ >= '0' && *p_ <= '9') || (*p_ >= 'A' && *p_ <= 'Z') || (*p_ >= 'a' && *p_ <= 'z') || (*p_ == '_'))
                            {
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                            }
                            else
                            {
                                state_stack_.pop_back(); // unquoted_string
                            }
                            break;
                    };
                    break;
                case path_state::raw_string: 
                    switch (*p_)
                    {
                        case '\'':
                        {
                            key_selector_stack_.back().selector->add_selector(jsoncons::make_unique<json_value_selector>(Json(buffer)));
                            buffer.clear();
                            state_stack_.pop_back(); // raw_string
                            ++p_;
                            ++column_;
                            break;
                        }
                        case '\\':
                            ++p_;
                            ++column_;
                            break;
                        default:
                            buffer.push_back(*p_);
                            ++p_;
                            ++column_;
                            break;
                    };
                    break;
                case path_state::json_value: 
                    switch (*p_)
                    {
                        case '`':
                        {
                            auto j = Json::parse(buffer);
                            key_selector_stack_.back().selector->add_selector(jsoncons::make_unique<json_value_selector>(std::move(j)));
                            buffer.clear();
                            state_stack_.pop_back(); // json_value
                            ++p_;
                            ++column_;
                            break;
                        }
                        case '\\':
                            break;
                        default:
                            buffer.push_back(*p_);
                            ++p_;
                            ++column_;
                            break;
                    };
                    break;
                case path_state::number:
                    switch(*p_)
                    {
                        case '-':
                            buffer.push_back(*p_);
                            state_stack_.back() = path_state::digit;
                            ++p_;
                            ++column_;
                            break;
                        default:
                            state_stack_.back() = path_state::digit;
                            break;
                    }
                    break;
                case path_state::digit:
                    switch(*p_)
                    {
                        case '0':case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':
                            buffer.push_back(*p_);
                            ++p_;
                            ++column_;
                            break;
                        default:
                            state_stack_.pop_back(); // digit
                            break;
                    }
                    break;
                case path_state::compound_expression:
                    switch(*p_)
                    {
                        case ' ':case '\t':case '\r':case '\n':
                            advance_past_space_character();
                            break;
                        case '.': 
                            ++p_;
                            ++column_;
                            state_stack_.emplace_back(path_state::expression);
                            break;
                        case '|':
                        {
                            ++p_;
                            ++column_;
                            key_selector_stack_.back() = key_selector(jsoncons::make_unique<pipe_selector>(std::move(key_selector_stack_.back().selector)));
                            state_stack_.emplace_back(path_state::expression);
                            break;
                        }
                        case '[':
                        case '{':
                            state_stack_.emplace_back(path_state::expression);
                            break;
                        default:
                            ec = jmespath_errc::expected_index;
                            return Json::null();
                    }
                    break;

                case path_state::bracket_specifier:
                    switch(*p_)
                    {
                        case '*':
                            key_selector_stack_.emplace_back(jsoncons::make_unique<list_projection2>());
                            state_stack_.back() = path_state::bracket_specifier4;
                            ++p_;
                            ++column_;
                            break;
                        case ']':
                            key_selector_stack_.back() = key_selector(jsoncons::make_unique<flatten_projection>(std::move(key_selector_stack_.back().selector)));
                            state_stack_.pop_back(); // bracket_specifier
                            ++p_;
                            ++column_;
                            break;
                        case '?':
                            structure_offset_stack_.push_back(key_selector_stack_.size());
                            key_selector_stack_.emplace_back(jsoncons::make_unique<compound_expression>());
                            state_stack_.back() = path_state::comparator;
                            state_stack_.emplace_back(path_state::expression);
                            ++p_;
                            ++column_;
                            break;
                        case ':':
                            state_stack_.back() = path_state::bracket_specifier2;
                            state_stack_.emplace_back(path_state::number);
                            ++p_;
                            ++column_;
                            break;
                        case '-':case '0':case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':
                            state_stack_.back() = path_state::bracket_specifier9;
                            state_stack_.emplace_back(path_state::number);
                            break;
                        default:
                            key_selector_stack_.back() = key_selector(jsoncons::make_unique<list_projection>(std::move(key_selector_stack_.back().selector)));

                            structure_offset_stack_.push_back(key_selector_stack_.size());
                            key_selector_stack_.emplace_back(jsoncons::make_unique<compound_expression>());
                            state_stack_.back() = path_state::expect_right_bracket4;
                            state_stack_.emplace_back(path_state::expression);
                            break;
                    }
                    break;

                case path_state::multi_select_hash:
                    switch(*p_)
                    {
                        case '*':
                        case ']':
                        case '?':
                        case ':':
                        case '-':case '0':case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':
                            break;
                        default:
                            key_selector_stack_.back() = key_selector(jsoncons::make_unique<list_projection>(std::move(key_selector_stack_.back().selector)));

                            structure_offset_stack_.push_back(key_selector_stack_.size());
                            key_selector_stack_.emplace_back(jsoncons::make_unique<compound_expression>());
                            state_stack_.back() = path_state::key_val_expr;
                            break;
                    }
                    break;

                case path_state::bracket_specifier9:
                    switch(*p_)
                    {
                        case ']':
                        {
                            if (buffer.empty())
                            {
                                key_selector_stack_.back() = key_selector(jsoncons::make_unique<flatten_projection>(std::move(key_selector_stack_.back().selector)));
                            }
                            else
                            {
                                auto r = jsoncons::detail::to_integer<int64_t>(buffer.data(), buffer.size());
                                if (!r)
                                {
                                    ec = jmespath_errc::invalid_number;
                                    return Json::null();
                                }
                                key_selector_stack_.back().selector->add_selector(jsoncons::make_unique<index_selector>(r.value()));
                                buffer.clear();
                            }
                            state_stack_.pop_back(); // bracket_specifier
                            ++p_;
                            ++column_;
                            break;
                        }
                        case ':':
                        {
                            if (!buffer.empty())
                            {
                                auto r = jsoncons::detail::to_integer<int64_t>(buffer.data(), buffer.size());
                                if (!r)
                                {
                                    ec = jmespath_errc::invalid_number;
                                    return Json::null();
                                }
                                a_slice.start_ = r.value();
                                buffer.clear();
                            }
                            state_stack_.back() = path_state::bracket_specifier2;
                            state_stack_.emplace_back(path_state::number);
                            ++p_;
                            ++column_;
                            break;
                        }
                        default:
                            ec = jmespath_errc::expected_right_bracket;
                            return Json::null();
                    }
                    break;
                case path_state::bracket_specifier2:
                {
                    if (!buffer.empty())
                    {
                        auto r = jsoncons::detail::to_integer<int64_t>(buffer.data(), buffer.size());
                        if (!r)
                        {
                            ec = jmespath_errc::invalid_number;
                            return Json::null();
                        }
                        a_slice.end_ = optional<int64_t>(r.value());
                        buffer.clear();
                    }
                    switch(*p_)
                    {
                        case ']':
                            key_selector_stack_.back() = key_selector(jsoncons::make_unique<slice_projection>(std::move(key_selector_stack_.back().selector),a_slice));
                            a_slice = slice{};
                            state_stack_.pop_back(); // bracket_specifier2
                            ++p_;
                            ++column_;
                            break;
                        case ':':
                            state_stack_.back() = path_state::bracket_specifier3;
                            state_stack_.emplace_back(path_state::number);
                            ++p_;
                            ++column_;
                            break;
                        default:
                            ec = jmespath_errc::expected_right_bracket;
                            return Json::null();
                    }
                    break;
                }
                case path_state::bracket_specifier3:
                {
                    if (!buffer.empty())
                    {
                        auto r = jsoncons::detail::to_integer<int64_t>(buffer.data(), buffer.size());
                        if (!r)
                        {
                            ec = jmespath_errc::invalid_number;
                            return Json::null();
                        }
                        a_slice.step_ = r.value();
                        buffer.clear();
                    }
                    switch(*p_)
                    {
                        case ']':
                            key_selector_stack_.back() = key_selector(jsoncons::make_unique<slice_projection>(std::move(key_selector_stack_.back().selector),a_slice));
                            //key_selector_stack_.back().selector->add_selector(jsoncons::make_unique<slice_projection>(a_slice));
                            buffer.clear();
                            a_slice = slice{};
                            state_stack_.pop_back(); // bracket_specifier3
                            ++p_;
                            ++column_;
                            break;
                        default:
                            ec = jmespath_errc::expected_right_bracket;
                            return Json::null();
                    }
                    break;
                }
                case path_state::bracket_specifier4:
                {
                    switch(*p_)
                    {
                        case ']':
                            state_stack_.pop_back(); // bracket_specifier4
                            ++p_;
                            ++column_;
                            break;
                        default:
                            ec = jmespath_errc::expected_right_bracket;
                            return Json::null();
                    }
                    break;
                }
                case path_state::key_val_expr: 
                {
                    switch (*p_)
                    {
                        case ' ':case '\t':case '\r':case '\n':
                            advance_past_space_character();
                            break;
                        case '\"':
                            state_stack_.back() = path_state::expect_colon;
                            state_stack_.emplace_back(path_state::key_expr);
                            state_stack_.emplace_back(path_state::quoted_string);
                            ++p_;
                            ++column_;
                            break;
                        case '\'':
                            state_stack_.back() = path_state::expect_colon;
                            state_stack_.emplace_back(path_state::raw_string);
                            ++p_;
                            ++column_;
                            break;
                        default:
                            if ((*p_ >= 'A' && *p_ <= 'Z') || (*p_ >= 'a' && *p_ <= 'z') || (*p_ == '_'))
                            {
                                state_stack_.back() = path_state::expect_colon;
                                state_stack_.emplace_back(path_state::key_expr);
                                state_stack_.emplace_back(path_state::unquoted_string);
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                            }
                            else
                            {
                                ec = jmespath_errc::expected_key;
                                return Json::null();
                            }
                            break;
                    };
                    break;
                }
                case path_state::comparator:
                {
                    switch(*p_)
                    {
                        case '.': 
                            ++p_;
                            ++column_;
                            state_stack_.back() = path_state::comparator;
                            state_stack_.emplace_back(path_state::expression);
                            break;
                        case '<':
                            state_stack_.back() = path_state::cmp_lt_or_lte;
                            ++p_;
                            ++column_;
                            break;
                        case '=':
                            state_stack_.back() = path_state::cmp_eq;
                            ++p_;
                            ++column_;
                            break;
                        case '>':
                            state_stack_.back() = path_state::cmp_gt_or_gte;
                            ++p_;
                            ++column_;
                            break;
                        case '!':
                            state_stack_.back() = path_state::cmp_ne;
                            ++p_;
                            ++column_;
                            break;
                        default:
                            ec = jmespath_errc::expected_comparator;
                            return Json::null();
                    }
                    break;
                }
                case path_state::cmp_lt_or_lte:
                {
                    switch(*p_)
                    {
                        case '=':
                            key_selector_stack_.back() = key_selector(jsoncons::make_unique<comparator_selector<cmp_lte>>(std::move(key_selector_stack_.back().selector)));
                            state_stack_.back() = path_state::expect_filter_right_bracket;
                            state_stack_.emplace_back(path_state::expression);
                            ++p_;
                            ++column_;
                            break;
                        default:
                            key_selector_stack_.back() = key_selector(jsoncons::make_unique<comparator_selector<cmp_lt>>(std::move(key_selector_stack_.back().selector)));
                            state_stack_.back() = path_state::expect_filter_right_bracket;
                            state_stack_.emplace_back(path_state::expression);
                            break;
                    }
                    break;
                }
                case path_state::cmp_eq:
                {
                    switch(*p_)
                    {
                        case '=':
                            key_selector_stack_.back() = key_selector(jsoncons::make_unique<comparator_selector<cmp_eq>>(std::move(key_selector_stack_.back().selector)));
                            state_stack_.back() = path_state::expect_filter_right_bracket;
                            state_stack_.emplace_back(path_state::expression);
                            ++p_;
                            ++column_;
                            break;
                        default:
                            ec = jmespath_errc::expected_comparator;
                            return Json::null();
                    }
                    break;
                }
                case path_state::cmp_gt_or_gte:
                {
                    switch(*p_)
                    {
                        case '=':
                            key_selector_stack_.back() = key_selector(jsoncons::make_unique<comparator_selector<cmp_gte>>(std::move(key_selector_stack_.back().selector)));
                            state_stack_.back() = path_state::expect_filter_right_bracket;
                            state_stack_.emplace_back(path_state::expression);
                            ++p_;
                            ++column_;
                            break;
                        default:
                            key_selector_stack_.back() = key_selector(jsoncons::make_unique<comparator_selector<cmp_gt>>(std::move(key_selector_stack_.back().selector)));
                            state_stack_.back() = path_state::expect_filter_right_bracket;
                            state_stack_.emplace_back(path_state::expression);
                            break;
                    }
                    break;
                }
                case path_state::cmp_ne:
                {
                    switch(*p_)
                    {
                        case '=':
                            key_selector_stack_.back() = key_selector(jsoncons::make_unique<comparator_selector<cmp_ne>>(std::move(key_selector_stack_.back().selector)));
                            state_stack_.back() = path_state::expect_filter_right_bracket;
                            state_stack_.emplace_back(path_state::expression);
                            ++p_;
                            ++column_;
                            break;
                        default:
                            ec = jmespath_errc::expected_comparator;
                            return Json::null();
                    }
                    break;
                }
                case path_state::expect_dot:
                {
                    switch(*p_)
                    {
                        case ' ':case '\t':case '\r':case '\n':
                            advance_past_space_character();
                            break;
                        case '.':
                            state_stack_.pop_back(); // expect_dot
                            ++p_;
                            ++column_;
                            break;
                        default:
                            ec = jmespath_errc::expected_dot;
                            return Json::null();
                    }
                    break;
                }
                case path_state::expect_filter_right_bracket:
                {
                    switch(*p_)
                    {
                        case ' ':case '\t':case '\r':case '\n':
                            advance_past_space_character();
                            break;
                        case ']':
                        {
                            state_stack_.pop_back();
                            size_t pos = structure_offset_stack_.back();
                            structure_offset_stack_.pop_back();
                            auto p = std::move(key_selector_stack_[pos]);
                            for (size_t i = pos+1; i < key_selector_stack_.size(); ++i)
                            {
                                p.selector->add_selector(std::move(key_selector_stack_[i].selector));
                            }
                            key_selector_stack_.erase(key_selector_stack_.begin()+pos, key_selector_stack_.end());

                            auto q = jsoncons::make_unique<filter_expression>(std::move(key_selector_stack_.back().selector), 
                                                                    std::move(p.selector));
                            key_selector_stack_.back() = key_selector(std::move(q));                            
                            ++p_;
                            ++column_;
                            break;
                        }
                        default:
                            ec = jmespath_errc::expected_right_bracket;
                            return Json::null();
                    }
                    break;
                }
                case path_state::expect_right_bracket4:
                {
                    switch(*p_)
                    {
                        case ' ':case '\t':case '\r':case '\n':
                            advance_past_space_character();
                            break;
                        case ',':
                            key_selector_stack_.emplace_back(key_selector(jsoncons::make_unique<compound_expression>()));
                            state_stack_.back() = path_state::expect_right_bracket4;
                            state_stack_.emplace_back(path_state::expression);
                            ++p_;
                            ++column_;
                            break;
                        case '[':
                            state_stack_.back() = path_state::expect_right_bracket4;
                            state_stack_.emplace_back(path_state::expression);
                            break;
                        case '.':
                            state_stack_.back() = path_state::expect_right_bracket4;
                            state_stack_.emplace_back(path_state::expression);
                            ++p_;
                            ++column_;
                            break;
                        case '|':
                        {
                            ++p_;
                            ++column_;
                            key_selector_stack_.back() = key_selector(jsoncons::make_unique<pipe_selector>(std::move(key_selector_stack_.back().selector)));
                            state_stack_.back() = path_state::expect_right_bracket4;
                            state_stack_.emplace_back(path_state::expression);
                            break;
                        }
                        case ']':
                        {
                            state_stack_.pop_back();

                            size_t pos = structure_offset_stack_.back();
                            structure_offset_stack_.pop_back();

                            std::vector<std::unique_ptr<selector_base>> selectors;
                            selectors.reserve(key_selector_stack_.size()-pos);
                            for (size_t i = pos; i < key_selector_stack_.size(); ++i)
                            {
                                selectors.emplace_back(std::move(key_selector_stack_[i].selector));
                            }
                            key_selector_stack_.erase(key_selector_stack_.begin()+pos, key_selector_stack_.end());
                            key_selector_stack_.back().selector->add_selector(jsoncons::make_unique<multi_select_list>(std::move(selectors)));

                            ++p_;
                            ++column_;
                            break;
                        }
                        default:
                            ec = jmespath_errc::expected_right_bracket;
                            return Json::null();
                    }
                    break;
                }
                case path_state::expect_right_brace:
                {
                    switch(*p_)
                    {
                        case ' ':case '\t':case '\r':case '\n':
                            advance_past_space_character();
                            break;
                        case ',':
                            key_selector_stack_.emplace_back(jsoncons::make_unique<compound_expression>());
                            state_stack_.back() = path_state::key_val_expr; 
                            ++p_;
                            ++column_;
                            break;
                        case '[':
                        case '{':
                            state_stack_.back() = path_state::expect_right_brace;
                            state_stack_.emplace_back(path_state::expression);
                            break;
                        case '.':
                            state_stack_.back() = path_state::expect_right_brace;
                            state_stack_.emplace_back(path_state::expression);
                            ++p_;
                            ++column_;
                            break;
                        case '}':
                        {
                            state_stack_.pop_back();

                            size_t pos = structure_offset_stack_.back();
                            structure_offset_stack_.pop_back();
                            std::vector<key_selector> key_selectors;
                            key_selectors.reserve(key_selector_stack_.size()-pos);
                            for (size_t i = pos; i < key_selector_stack_.size(); ++i)
                            {
                                key_selectors.emplace_back(std::move(key_selector_stack_[i]));
                            }
                            key_selector_stack_.erase(key_selector_stack_.begin()+pos, key_selector_stack_.end());
                            key_selector_stack_.back().selector->add_selector(jsoncons::make_unique<multi_select_hash>(std::move(key_selectors)));

                            ++p_;
                            ++column_;
                            break;
                        }
                        default:
                            ec = jmespath_errc::expected_right_brace;
                            return Json::null();
                    }
                    break;
                }
                case path_state::expect_colon:
                {
                    switch(*p_)
                    {
                        case ' ':case '\t':case '\r':case '\n':
                            advance_past_space_character();
                            break;
                        case ':':
                            state_stack_.back() = path_state::expect_right_brace;
                            state_stack_.emplace_back(path_state::expression);
                            ++p_;
                            ++column_;
                            break;
                        default:
                            ec = jmespath_errc::expected_colon;
                            return Json::null();
                    }
                    break;
                }
            }
            
        }

        if (state_stack_.size() >= 3 && state_stack_.back() == path_state::unquoted_string)
        {
            state_stack_.pop_back(); // unquoted_string
            if (state_stack_.back() == path_state::val_expr || state_stack_.back() == path_state::identifier_or_function_expr)
            {
                key_selector_stack_.back().selector->add_selector(jsoncons::make_unique<identifier_selector>(buffer));
                buffer.clear();
                state_stack_.pop_back(); // val_expr
            }
        }

        for (size_t i = key_selector_stack_.size(); --i > 0;)
        {
            key_selector_stack_[i-1].selector->add_selector(std::move(key_selector_stack_[i].selector));
        }
        key_selector_stack_.erase(key_selector_stack_.begin()+1,key_selector_stack_.end());

        JSONCONS_ASSERT(state_stack_.size() == 1);
        JSONCONS_ASSERT(state_stack_.back() == path_state::expression ||
                        state_stack_.back() == path_state::compound_expression);
        state_stack_.pop_back();

        reference r = key_selector_stack_.back().selector->select(temp_factory_, root, ec);
        return r;
    }

    void advance_past_space_character()
    {
        switch (*p_)
        {
            case ' ':case '\t':
                ++p_;
                ++column_;
                break;
            case '\r':
                if (p_+1 < end_input_ && *(p_+1) == '\n')
                    ++p_;
                ++line_;
                column_ = 1;
                ++p_;
                break;
            case '\n':
                ++line_;
                column_ = 1;
                ++p_;
                break;
            default:
                break;
        }
    }
};

}

}}

#endif