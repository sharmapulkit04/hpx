//  Copyright (c) 2007-2020 Hartmut Kaiser
//  Copyright (c) 2016 Thomas Heller
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <hpx/config.hpp>
#include <hpx/actions_base/action_priority.hpp>
#include <hpx/actions_base/basic_action_fwd.hpp>
#include <hpx/actions_base/continuation_fwd.hpp>
#include <hpx/actions_base/traits/action_remote_result.hpp>
#include <hpx/functional/serialization/serializable_unique_function.hpp>
#include <hpx/futures/traits/future_traits.hpp>
#include <hpx/modules/errors.hpp>
#include <hpx/modules/logging.hpp>
#include <hpx/naming_base/id_type.hpp>
#include <hpx/preprocessor/stringize.hpp>
#include <hpx/runtime/actions/trigger.hpp>
#include <hpx/runtime/agas/interface.hpp>
#include <hpx/runtime/trigger_lco_fwd.hpp>
#include <hpx/serialization/base_object.hpp>
#include <hpx/serialization/serialize.hpp>
#include <hpx/traits/is_continuation.hpp>

#include <exception>
#include <type_traits>
#include <utility>

#include <hpx/config/warnings_prefix.hpp>

///////////////////////////////////////////////////////////////////////////////
namespace hpx { namespace actions
{
    ///////////////////////////////////////////////////////////////////////////
    // Parcel continuations are polymorphic objects encapsulating the
    // id_type of the destination where the result has to be sent.
    class HPX_EXPORT continuation
    {
    public:
        typedef void continuation_tag;

        continuation();

        explicit continuation(naming::id_type const& gid);
        explicit continuation(naming::id_type && gid);

        continuation(naming::id_type const& gid, naming::address && addr);
        continuation(naming::id_type && gid, naming::address && addr);

        continuation(continuation&& o);
        continuation& operator=(continuation&& o);

        //
        void trigger_error(std::exception_ptr const& e);
        void trigger_error(std::exception_ptr && e);

        // serialization support
        void serialize(hpx::serialization::input_archive& ar, unsigned);
        void serialize(hpx::serialization::output_archive& ar, unsigned);

        naming::id_type const& get_id() const
        {
            return gid_;
        }

        naming::address get_addr() const
        {
            return addr_;
        }

    protected:
        naming::id_type gid_;
        naming::address addr_;
    };

    ///////////////////////////////////////////////////////////////////////////
    template <typename Result>
    struct typed_continuation<Result, Result> : continuation
    {
    private:
        typedef util::unique_function<
                void(naming::id_type, Result)
            > function_type;

    public:
        typedef Result result_type;

        typed_continuation()
        {}

        explicit typed_continuation(naming::id_type const& gid)
          : continuation(gid)
        {}

        explicit typed_continuation(naming::id_type && gid)
          : continuation(std::move(gid))
        {}

        template <typename F>
        typed_continuation(naming::id_type const& gid, F && f)
          : continuation(gid), f_(std::forward<F>(f))
        {}

        template <typename F>
        typed_continuation(naming::id_type && gid, F && f)
          : continuation(std::move(gid)), f_(std::forward<F>(f))
        {}

        typed_continuation(naming::id_type const& gid, naming::address && addr)
          : continuation(gid, std::move(addr))
        {}

        typed_continuation(naming::id_type && gid, naming::address && addr)
          : continuation(std::move(gid), std::move(addr))
        {}

        template <typename F>
        typed_continuation(naming::id_type const& gid, naming::address && addr,
                F && f)
          : continuation(gid, std::move(addr)),
            f_(std::forward<F>(f))
        {}

        template <typename F>
        typed_continuation(naming::id_type && gid, naming::address && addr,
                F && f)
          : continuation(std::move(gid), std::move(addr)),
            f_(std::forward<F>(f))
        {}

        template <typename F, typename Enable =
            typename std::enable_if<
               !std::is_same<
                    typename std::decay<F>::type, typed_continuation
                >::value
            >::type
        >
        explicit typed_continuation(F && f)
          : f_(std::forward<F>(f))
        {}

        // This is needed for some gcc versions
        // replace by typed_continuation(typed_continuation && o) = default;
        // when all compiler support it
        typed_continuation(typed_continuation && o)
          : continuation(std::move(o.gid_), std::move(o.addr_)),
            f_(std::move(o.f_))
        {}

        typed_continuation& operator=(typed_continuation&& o)
        {
            continuation::operator=(std::move(static_cast<continuation&&>(o)));
            f_ = std::move(o.f_);
            return *this;
        }

        void trigger_value(Result && result)
        {
            LLCO_(info)
                << "typed_continuation<Result>::trigger_value("
                << this->get_id() << ")";

            if (f_.empty()) {
                if (!this->get_id()) {
                    HPX_THROW_EXCEPTION(invalid_status,
                        "typed_continuation<Result>::trigger_value",
                        "attempt to trigger invalid LCO (the id is invalid)");
                    return;
                }
                hpx::set_lco_value(this->get_id(), this->get_addr(),
                    std::move(result));
            }
            else {
                f_(this->get_id(), std::move(result));
            }
        }

    private:
        /// serialization support
        friend class hpx::serialization::access;

        template <typename Archive>
        void serialize(Archive & ar, unsigned)
        {
            // serialize base class
            ar & hpx::serialization::base_object<continuation>(*this);
            ar & f_;
        }

    protected:
        function_type f_;
    };

    // This specialization is needed to call the right
    // base_lco_with_value action if the local Result is computed
    // via get_remote_result and differs from the actions original
    // local result type
    template <typename Result, typename RemoteResult>
    struct typed_continuation : typed_continuation<RemoteResult>
    {
    private:
        typedef typed_continuation<RemoteResult> base_type;
        typedef util::unique_function<
                void(naming::id_type, RemoteResult)
            > function_type;

    public:
        typed_continuation()
        {}

        explicit typed_continuation(naming::id_type const& gid)
          : base_type(gid)
        {}

        explicit typed_continuation(naming::id_type && gid)
          : base_type(std::move(gid))
        {}

        template <typename F>
        typed_continuation(naming::id_type const& gid, F && f)
          : base_type(gid, std::forward<F>(f))
        {}

        template <typename F>
        typed_continuation(naming::id_type && gid, F && f)
          : base_type(std::move(gid), std::forward<F>(f))
        {}

        typed_continuation(naming::id_type const& gid, naming::address && addr)
          : base_type(gid, std::move(addr))
        {}

        typed_continuation(naming::id_type && gid, naming::address && addr)
          : base_type(std::move(gid), std::move(addr))
        {}

        template <typename F>
        typed_continuation(naming::id_type const& gid,
                naming::address && addr, F && f)
          : base_type(gid, std::move(addr), std::forward<F>(f))
        {}

        template <typename F>
        typed_continuation(naming::id_type && gid,
                naming::address && addr, F && f)
          : base_type(std::move(gid), std::move(addr), std::forward<F>(f))
        {}

        template <typename F, typename Enable =
            typename std::enable_if<
               !std::is_same<
                    typename std::decay<F>::type, typed_continuation
                >::value
            >::type
        >
        explicit typed_continuation(F && f)
          : base_type(std::forward<F>(f))
        {}

        // This is needed for some gcc versions
        // replace by typed_continuation(typed_continuation && o) = default;
        // when all compiler support it
        typed_continuation(typed_continuation && o)
          : base_type(static_cast<base_type &&>(o))
        {}

        typed_continuation& operator=(typed_continuation&& o)
        {
            base_type::operator=(std::move(o));
            return *this;
        }

        void trigger_value(RemoteResult && result)
        {
            LLCO_(info)
                << "typed_continuation<RemoteResult>::trigger_value("
                << this->get_id() << ")";

            if (this->f_.empty()) {
                if (!this->get_id()) {
                    HPX_THROW_EXCEPTION(invalid_status,
                        "typed_continuation<Result>::trigger_value",
                        "attempt to trigger invalid LCO (the id is invalid)");
                    return;
                }
                hpx::set_lco_value(this->get_id(), this->get_addr(),
                    std::move(result));
            }
            else {
                this->f_(this->get_id(), std::move(result));
            }
        }

    private:
        /// serialization support
        friend class hpx::serialization::access;

        template <typename Archive>
        void serialize(Archive & ar, unsigned)
        {
            // serialize base class
            ar & hpx::serialization::base_object<
                typed_continuation<RemoteResult> >(*this);
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    template <>
    struct typed_continuation<void, util::unused_type> : continuation
    {
    private:
        typedef util::unique_function<void(naming::id_type)> function_type;

    public:
        typedef void result_type;

        typed_continuation()
        {}

        explicit typed_continuation(naming::id_type const& gid)
          : continuation(gid)
        {}

        explicit typed_continuation(naming::id_type && gid)
          : continuation(std::move(gid))
        {}

        template <typename F>
        typed_continuation(naming::id_type const& gid, F && f)
          : continuation(gid), f_(std::forward<F>(f))
        {}

        template <typename F>
        typed_continuation(naming::id_type && gid, F && f)
          : continuation(std::move(gid)), f_(std::forward<F>(f))
        {}

        typed_continuation(naming::id_type const& gid, naming::address && addr)
          : continuation(gid, std::move(addr))
        {}

        typed_continuation(naming::id_type && gid, naming::address && addr)
          : continuation(std::move(gid), std::move(addr))
        {}

        template <typename F>
        typed_continuation(naming::id_type const& gid, naming::address && addr,
                F && f)
          : continuation(gid, std::move(addr)),
            f_(std::forward<F>(f))
        {}

        template <typename F>
        typed_continuation(naming::id_type && gid, naming::address && addr,
                F && f)
          : continuation(std::move(gid), std::move(addr)),
            f_(std::forward<F>(f))
        {}

        template <typename F, typename Enable =
            typename std::enable_if<
               !std::is_same<
                    typename std::decay<F>::type, typed_continuation
                >::value
            >::type
        >
        explicit typed_continuation(F && f)
          : f_(std::forward<F>(f))
        {}

        // This is needed for some gcc versions
        // replace by typed_continuation(typed_continuation && o) = default;
        // when all compiler support it
        typed_continuation(typed_continuation && o)
          : continuation(std::move(o.gid_), std::move(o.addr_)),
            f_(std::move(o.f_))
        {}

        typed_continuation& operator=(typed_continuation&& o)
        {
            continuation::operator=(std::move(static_cast<continuation&&>(o)));
            f_ = std::move(o.f_);
            return *this;
        }

        void trigger()
        {
            LLCO_(info)
                << "typed_continuation<void>::trigger("
                << this->get_id() << ")";

            if (f_.empty()) {
                if (!this->get_id()) {
                    HPX_THROW_EXCEPTION(invalid_status,
                        "typed_continuation<void>::trigger",
                        "attempt to trigger invalid LCO (the id is invalid)");
                    return;
                }
                trigger_lco_event(this->get_id(), this->get_addr());
            }
            else {
                f_(this->get_id());
            }
        }

        void trigger_value(util::unused_type &&)
        {
            this->trigger();
        }

        void trigger_value(util::unused_type const&)
        {
            this->trigger();
        }

    private:
        /// serialization support
        friend class hpx::serialization::access;

        template <typename Archive>
        void serialize(Archive & ar, unsigned)
        {
            // serialize base class
            ar & hpx::serialization::base_object<continuation>(*this);

            ar & f_;
        }

        function_type f_;
    };
}}

#include <hpx/config/warnings_suffix.hpp>

// this file is intentionally #included last as it refers to functions defined
// here
#include <hpx/runtime/trigger_lco.hpp>

