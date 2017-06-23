    { "max_alloc"  , HAS_ARG,  {.func_arg = mk_opt_max_alloc},     "set maximum size of a single allocated block", "bytes" },
    { "cpuflags"   , HAS_ARG | OPT_EXPERT, { .func_arg = mk_opt_cpuflags }, "force specific cpu flags", "flags" },
#if CONFIG_OPENCL
    { "opencl_bench", OPT_EXIT, {.func_arg = mk_opt_opencl_bench}, "run benchmark on all OpenCL devices and show results" },
    { "opencl_options", HAS_ARG, {.func_arg = mk_opt_opencl},      "set OpenCL environment options" },
#endif
