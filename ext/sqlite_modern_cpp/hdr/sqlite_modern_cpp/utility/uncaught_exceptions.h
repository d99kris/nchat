#pragma once

#include <cassert>
#include <exception>
#include <iostream>

namespace sqlite {
	namespace utility {
#ifdef __cpp_lib_uncaught_exceptions
		class UncaughtExceptionDetector {
		public:
			operator bool() {
				return count != std::uncaught_exceptions();
			}
		private:
			int count = std::uncaught_exceptions();
		};
#else
		class UncaughtExceptionDetector {
		public:
			operator bool() {
				return std::uncaught_exception();
			}
		};
#endif
	}
}
