#pragma once

#include "../errors.h"
#include <sqlite3.h>
#include <optional>
#include <variant>

namespace sqlite::utility {
	template<typename ...Options>
	struct VariantFirstNullable {
		using type = void;
	};
	template<typename T, typename ...Options>
	struct VariantFirstNullable<T, Options...> {
		using type = typename VariantFirstNullable<Options...>::type;
	};
#ifdef MODERN_SQLITE_STD_OPTIONAL_SUPPORT
	template<typename T, typename ...Options>
	struct VariantFirstNullable<std::optional<T>, Options...> {
		using type = std::optional<T>;
	};
#endif
	template<typename T, typename ...Options>
	struct VariantFirstNullable<std::unique_ptr<T>, Options...> {
		using type = std::unique_ptr<T>;
	};
	template<typename ...Options>
	struct VariantFirstNullable<std::nullptr_t, Options...> {
		using type = std::nullptr_t;
	};
	template<typename Callback, typename ...Options>
	inline void variant_select_null(Callback&&callback) {
		if constexpr(std::is_same_v<typename VariantFirstNullable<Options...>::type, void>) {
			throw errors::mismatch("NULL is unsupported by this variant.", "", SQLITE_MISMATCH);
		} else {
			std::forward<Callback>(callback)(typename VariantFirstNullable<Options...>::type());
		}
	}

	template<typename ...Options>
	struct VariantFirstIntegerable {
		using type = void;
	};
	template<typename T, typename ...Options>
	struct VariantFirstIntegerable<T, Options...> {
		using type = typename VariantFirstIntegerable<Options...>::type;
	};
#ifdef MODERN_SQLITE_STD_OPTIONAL_SUPPORT
	template<typename T, typename ...Options>
	struct VariantFirstIntegerable<std::optional<T>, Options...> {
		using type = std::conditional_t<std::is_same_v<typename VariantFirstIntegerable<T, Options...>::type, T>, std::optional<T>, typename VariantFirstIntegerable<Options...>::type>;
	};
#endif
	template<typename T, typename ...Options>
	struct VariantFirstIntegerable<std::enable_if_t<std::is_same_v<typename VariantFirstIntegerable<T, Options...>::type, T>>, std::unique_ptr<T>, Options...> {
		using type = std::conditional_t<std::is_same_v<typename VariantFirstIntegerable<T, Options...>::type, T>, std::unique_ptr<T>, typename VariantFirstIntegerable<Options...>::type>;
	};
	template<typename ...Options>
	struct VariantFirstIntegerable<int, Options...> {
		using type = int;
	};
	template<typename ...Options>
	struct VariantFirstIntegerable<sqlite_int64, Options...> {
		using type = sqlite_int64;
	};
	template<typename Callback, typename ...Options>
	inline auto variant_select_integer(Callback&&callback) {
		if constexpr(std::is_same_v<typename VariantFirstIntegerable<Options...>::type, void>) {
			throw errors::mismatch("Integer is unsupported by this variant.", "", SQLITE_MISMATCH);
		} else {
			std::forward<Callback>(callback)(typename VariantFirstIntegerable<Options...>::type());
		}
	}

	template<typename ...Options>
	struct VariantFirstFloatable {
		using type = void;
	};
	template<typename T, typename ...Options>
	struct VariantFirstFloatable<T, Options...> {
		using type = typename VariantFirstFloatable<Options...>::type;
	};
#ifdef MODERN_SQLITE_STD_OPTIONAL_SUPPORT
	template<typename T, typename ...Options>
	struct VariantFirstFloatable<std::optional<T>, Options...> {
		using type = std::conditional_t<std::is_same_v<typename VariantFirstFloatable<T, Options...>::type, T>, std::optional<T>, typename VariantFirstFloatable<Options...>::type>;
	};
#endif
	template<typename T, typename ...Options>
	struct VariantFirstFloatable<std::unique_ptr<T>, Options...> {
		using type = std::conditional_t<std::is_same_v<typename VariantFirstFloatable<T, Options...>::type, T>, std::unique_ptr<T>, typename VariantFirstFloatable<Options...>::type>;
	};
	template<typename ...Options>
	struct VariantFirstFloatable<float, Options...> {
		using type = float;
	};
	template<typename ...Options>
	struct VariantFirstFloatable<double, Options...> {
		using type = double;
	};
	template<typename Callback, typename ...Options>
	inline auto variant_select_float(Callback&&callback) {
		if constexpr(std::is_same_v<typename VariantFirstFloatable<Options...>::type, void>) {
			throw errors::mismatch("Real is unsupported by this variant.", "", SQLITE_MISMATCH);
		} else {
			std::forward<Callback>(callback)(typename VariantFirstFloatable<Options...>::type());
		}
	}

	template<typename ...Options>
	struct VariantFirstTextable {
		using type = void;
	};
	template<typename T, typename ...Options>
	struct VariantFirstTextable<T, Options...> {
		using type = typename VariantFirstTextable<void, Options...>::type;
	};
#ifdef MODERN_SQLITE_STD_OPTIONAL_SUPPORT
	template<typename T, typename ...Options>
	struct VariantFirstTextable<std::optional<T>, Options...> {
		using type = std::conditional_t<std::is_same_v<typename VariantFirstTextable<T, Options...>::type, T>, std::optional<T>, typename VariantFirstTextable<Options...>::type>;
	};
#endif
	template<typename T, typename ...Options>
	struct VariantFirstTextable<std::unique_ptr<T>, Options...> {
		using type = std::conditional_t<std::is_same_v<typename VariantFirstTextable<T, Options...>::type, T>, std::unique_ptr<T>, typename VariantFirstTextable<Options...>::type>;
	};
	template<typename ...Options>
	struct VariantFirstTextable<std::string, Options...> {
		using type = std::string;
	};
	template<typename ...Options>
	struct VariantFirstTextable<std::u16string, Options...> {
		using type = std::u16string;
	};
	template<typename Callback, typename ...Options>
	inline void variant_select_text(Callback&&callback) {
		if constexpr(std::is_same_v<typename VariantFirstTextable<Options...>::type, void>) {
			throw errors::mismatch("Text is unsupported by this variant.", "", SQLITE_MISMATCH);
		} else {
			std::forward<Callback>(callback)(typename VariantFirstTextable<Options...>::type());
		}
	}

	template<typename ...Options>
	struct VariantFirstBlobable {
		using type = void;
	};
	template<typename T, typename ...Options>
	struct VariantFirstBlobable<T, Options...> {
		using type = typename VariantFirstBlobable<Options...>::type;
	};
#ifdef MODERN_SQLITE_STD_OPTIONAL_SUPPORT
	template<typename T, typename ...Options>
	struct VariantFirstBlobable<std::optional<T>, Options...> {
		using type = std::conditional_t<std::is_same_v<typename VariantFirstBlobable<T, Options...>::type, T>, std::optional<T>, typename VariantFirstBlobable<Options...>::type>;
	};
#endif
	template<typename T, typename ...Options>
	struct VariantFirstBlobable<std::unique_ptr<T>, Options...> {
		using type = std::conditional_t<std::is_same_v<typename VariantFirstBlobable<T, Options...>::type, T>, std::unique_ptr<T>, typename VariantFirstBlobable<Options...>::type>;
	};
	template<typename T, typename A, typename ...Options>
	struct VariantFirstBlobable<std::enable_if_t<std::is_pod_v<T>>, std::vector<T, A>, Options...> {
		using type = std::vector<T, A>;
	};
	template<typename Callback, typename ...Options>
	inline auto variant_select_blob(Callback&&callback) {
		if constexpr(std::is_same_v<typename VariantFirstBlobable<Options...>::type, void>) {
			throw errors::mismatch("Blob is unsupported by this variant.", "", SQLITE_MISMATCH);
		} else {
			std::forward<Callback>(callback)(typename VariantFirstBlobable<Options...>::type());
		}
	}

	template<typename ...Options>
	inline auto variant_select(int type) {
		return [type](auto &&callback) {
			using Callback = decltype(callback);
			switch(type) {
				case SQLITE_NULL:
					variant_select_null<Callback, Options...>(std::forward<Callback>(callback));
					break;
				case SQLITE_INTEGER:
					variant_select_integer<Callback, Options...>(std::forward<Callback>(callback));
					break;
				case SQLITE_FLOAT:
					variant_select_float<Callback, Options...>(std::forward<Callback>(callback));
					break;
				case SQLITE_TEXT:
					variant_select_text<Callback, Options...>(std::forward<Callback>(callback));
					break;
				case SQLITE_BLOB:
					variant_select_blob<Callback, Options...>(std::forward<Callback>(callback));
					break;
				default:;
					/* assert(false); */
			}
		};
	}
}
