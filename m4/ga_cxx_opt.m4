# GA_CXX_OPT()
# ------------
# Determine TARGET-/compiler-specific CXXFLAGS for optimization.
AC_DEFUN([GA_CXX_OPT], [
AC_REQUIRE([GA_TARGET64])
AC_REQUIRE([GA_ENABLE_OPT])
AC_CACHE_CHECK([for specific C++ optimizations], [ga_cv_cxx_opt],
[AS_IF([test x$enable_opt = xno], [ga_cv_cxx_opt="-O0"],
[AS_CASE([$ga_cv_target:$ax_cv_cxx_compiler_vendor:$host_cpu],
[LINUX:*:*],                [ga_cv_cxx_opt="-O0"],
                            [ga_cv_cxx_opt="-O0"])])])
AC_SUBST([GA_CXXOPT],       [$ga_cv_cxx_opt])
])dnl