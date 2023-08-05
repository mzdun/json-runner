// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <chaiscript/chaiscript.hpp>
#include <span>


namespace chaiscript::bootstrap::standard_library {
	template <typename ContainerType>
	void random_access_container_type_2(const std::string& /*type*/,
	                                    Module& m) {
		// In the interest of runtime safety for the m, we prefer the at()
		// method for [] access, to throw an exception in an out of bounds
		// condition.
		m.add(
		    fun([](ContainerType& c,
		           int index) -> typename ContainerType::reference {
			    /// \todo we are prefering to keep the key as 'int' to avoid
			    /// runtime conversions during dispatch. reevaluate
			    return c[static_cast<typename ContainerType::size_type>(index)];
		    }),
		    "[]");

		m.add(
		    fun([](const ContainerType& c,
		           int index) -> typename ContainerType::const_reference {
			    /// \todo we are prefering to keep the key as 'int' to avoid
			    /// runtime conversions during dispatch. reevaluate
			    return c[static_cast<typename ContainerType::size_type>(index)];
		    }),
		    "[]");
	}

	template <typename ContainerType>
	void container_type_2(const std::string& /*type*/, Module& m) {
		m.add(fun([](const ContainerType* a) { return a->size(); }), "size");
		m.add(fun([](const ContainerType* a) { return a->empty(); }), "empty");
	}

	template <typename SpanType>
	void span_type(const std::string& type, Module& m) {
		m.add(user_type<SpanType>(), type);

		m.add(fun([](SpanType& container) -> decltype(auto) {
			      if (container.empty()) {
				      throw std::range_error("Container empty");
			      } else {
				      return (container.front());
			      }
		      }),
		      "front");

		m.add(fun([](const SpanType& container) -> decltype(auto) {
			      if (container.empty()) {
				      throw std::range_error("Container empty");
			      } else {
				      return (container.front());
			      }
		      }),
		      "front");

		random_access_container_type_2<SpanType>(type, m);
		container_type_2<SpanType>(type, m);
		default_constructible_type<SpanType>(type, m);
		assignable_type<SpanType>(type, m);
		input_range_type<SpanType>(type, m);
	}

	template <typename SpanType>
	ModulePtr span_type(const std::string& type) {
		auto m = std::make_shared<Module>();
		span_type<SpanType>(type, *m);
		return m;
	}
}  // namespace chaiscript::bootstrap::standard_library
