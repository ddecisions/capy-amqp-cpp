//
// Created by denn nevera on 2019-05-31.
//

#pragma once

#include <iostream>
#include <exception>
#include <optional>

#include "capy/amqp_expected.h"
#include "nlohmann/json.h"

#define PUBLIC_ENUM(OriginalType) std::underlying_type_t<OriginalType>
#define EXTEND_ENUM(OriginalType,LAST) static_cast<std::underlying_type_t<OriginalType>>(OriginalType::LAST)

namespace capy {

    using namespace nonstd;

    typedef nlohmann::json json;

    /***
     * Common Error handler
     */
    struct Error {

        Error(const std::error_condition code, const std::optional<std::string>& message = std::nullopt);
        const int value() const;
        const std::string message() const;

        operator bool() const;

        friend std::ostream& operator<<(std::ostream& os, const Error& error) {
          os << error.value() << "/" << error.message();
          return os;
        }

    private:
        std::error_code code_;
        std::optional<std::string>  exception_message_;
    };

    /***
     * Synchronous expected result type
     */
    template <typename T>
    using Result = expected<T,Error>;

    /***
     * Common singleton interface
     * @tparam T
     */
    template <typename T>
    class Singleton
    {
    public:
        static T& Instance()
        {
          static T instance;
          return instance;
        }

    protected:
        Singleton() {}
        ~Singleton() {}
    public:
        Singleton(Singleton const &) = delete;
        Singleton& operator=(Singleton const &) = delete;
    };

    /***
    *
    * Formated error string
    *
    * @param format
    * @param ...
    * @return
    */
    const std::string error_string(const char* format, ...);

    static inline void _throw_abort(const char* file, int line, const char* msg)
    {
      std::cerr << "Capy logic error: assert failed:\t" << msg << "\n"
                << "Capy logic error: source:\t\t" << file << ", line " << line << "\n";
      abort();
    }

#ifndef NDEBUG
#   define throw_abort(Msg) _throw_abort( __FILE__, __LINE__, Msg)
#else
#   define M_Assert(Expr, Msg) ;
#endif


}

namespace capy::amqp {

    /***
     * Common error codes
     */
    enum class CommonError: int {

        /***
         * Skip the error
         */
        OK = 0,

        /***
         * not supported error
         */
        NOT_SUPPORTED = 300,

        /***
         * unknown error
         */
        UNKNOWN,

        /***
         * Resource not found
         */
        NOT_FOUND,

        /***
         * Collection range excaption
         */
        OUT_OF_RANGE,

        /**
         * the last error code
         */
        LAST
    };

    /***
     * Common Error category
     */
    class ErrorCategory: public std::error_category
    {
    public:
        virtual const char* name() const noexcept override ;
        virtual std::string message(int ev) const override ;
        virtual bool equivalent(const std::error_code& code, int condition) const noexcept override ;

    };

    const std::error_category& error_category();
    std::error_condition make_error_condition(capy::amqp::CommonError e);

}

namespace std {

    template <>
    struct is_error_condition_enum<capy::amqp::CommonError>
            : public true_type {};
}